#include "PanoramaFill.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace camera360 {
namespace detail {

void fillTopBottomBlur(cv::Mat&        pano,
                       const cv::Mat&  mask_in,
                       double          target_ratio,
                       int             source_px,
                       int             blur_kernel,
                       int             fade_px,
                       double          blur_ratio,
                       double          fade_power,
                       double          vertical_camera_deg)
{
    if (pano.empty()) {
        std::cerr << "Fill panorama: empty panorama, skipping\n";
        return;
    }

    const int W = pano.cols;
    int       H = pano.rows;   // mutable: strip may be cropped below

    // Local non-const mask view so the optional row-range slice below can
    // re-bind without touching the caller's reference.
    cv::Mat mask = mask_in;

    // ── Place the captured strip on the target equirect canvas ────────────
    //
    // The fill stage no longer just splits a symmetric "pad to target_ratio"
    // budget. Instead we treat the target_ratio canvas as a 180°-vertical
    // equirect canvas and position the captured cylindrical strip so its
    // vertical centre lands on the row that maps to the camera's optical
    // axis:
    //
    //   H_target     = round(W / target_ratio)
    //   elev_cam     = clamp(vertical_camera_deg − 90, −90, +90)   (degrees)
    //   pix_per_deg  = H_target / 180
    //   cam_axis_row = H_target/2 − elev_cam · pix_per_deg
    //
    // Any strip rows that overflow the canvas (camera looked far enough
    // off-horizon to capture below-nadir or above-zenith content) are
    // cropped on the over-extended side; the kept rows are padded on the
    // opposite side to reach H_target. At deg=90 the formula collapses to
    // the legacy symmetric `pad_top = pad_bot = (H_target − H)/2` split, no
    // crop — so vertical=90 captures are byte-for-byte identical to before.
    int pad_top = 0, pad_bot = 0;
    int crop_top = 0, crop_bot = 0;
    if (target_ratio > 0.0) {
        const int H_target = static_cast<int>(std::round(W / target_ratio));
        if (H_target > 0) {
            const double elev_cam = std::max(
                -90.0, std::min(90.0, vertical_camera_deg - 90.0));
            const double pix_per_deg  = static_cast<double>(H_target) / 180.0;
            const double horizon_row  = H_target / 2.0;
            const double cam_axis_row = horizon_row - elev_cam * pix_per_deg;
            const double strip_top_f  = cam_axis_row - H / 2.0;
            const double strip_bot_f  = cam_axis_row + H / 2.0;

            crop_top = std::max(0,
                std::min(H,
                         static_cast<int>(std::round(-strip_top_f))));
            crop_bot = std::max(0,
                std::min(H - crop_top,
                         static_cast<int>(std::round(strip_bot_f - H_target))));
            pad_top = std::max(0,
                static_cast<int>(std::round(strip_top_f)));

            if (crop_top > 0 || crop_bot > 0) {
                pano = pano.rowRange(crop_top, H - crop_bot).clone();
                if (!mask.empty() && mask.cols == W && mask.rows == H)
                    mask = mask.rowRange(crop_top, H - crop_bot).clone();
                H = pano.rows;
            }

            const int max_pad = std::max(0, H_target - H);
            if (pad_top > max_pad) pad_top = max_pad;
            pad_bot = std::max(0, H_target - H - pad_top);
        }
    }

    // ── Detect wedges from mask (per-column path trigger) ──────────────────
    const bool has_usable_mask = !mask.empty() &&
                                 mask.cols == W && mask.rows == H &&
                                 mask.type() == CV_8UC1;
    const bool has_wedges = has_usable_mask &&
        cv::countNonZero(mask) < static_cast<int>(mask.total());

    if (pad_top == 0 && pad_bot == 0 && !has_wedges) return;

    // ── Shared parameter sanitisation ──────────────────────────────────────
    const int S = std::max(1, std::min(source_px, H));
    int K = std::max(3, blur_kernel);
    if ((K & 1) == 0) ++K;
    const int    F  = std::max(0, fade_px);
    const double BR = std::min(1.0, std::max(0.0, blur_ratio));
    const double FP = std::max(1.0, fade_power);
    const float  fp = static_cast<float>(FP);

    // ──────────────────────────────────────────────────────────────────────
    // Legacy fast path: clean horizontal edges, only pad bands needed.
    // ──────────────────────────────────────────────────────────────────────
    if (!has_wedges) {
        auto sampleSide = [&](bool is_top, cv::Mat& edge_profile, cv::Mat& flat_row) {
            cv::Mat src_rows = is_top ? pano.rowRange(0, S)
                                      : pano.rowRange(H - S, H);
            cv::Mat mean_row_f;
            cv::reduce(src_rows, mean_row_f, 0, cv::REDUCE_AVG, CV_32FC3);
            cv::GaussianBlur(mean_row_f, edge_profile,
                             cv::Size(K, 1), 0, 0, cv::BORDER_REFLECT_101);
            const cv::Scalar mean_bgr = cv::mean(src_rows);
            flat_row = cv::Mat(1, W, CV_32FC3,
                               cv::Scalar(mean_bgr[0], mean_bgr[1], mean_bgr[2]));
        };

        auto buildBand = [&](bool is_top, int band_h,
                             const cv::Mat& edge_profile,
                             const cv::Mat& flat_row) -> cv::Mat {
            if (band_h <= 0) return cv::Mat();
            const float blur_rows_f = static_cast<float>(BR) * static_cast<float>(band_h);
            const int   blur_rows   = std::min(band_h, static_cast<int>(blur_rows_f));
            const float fade_denom  = static_cast<float>(std::max(band_h - blur_rows - 1, 1));

            cv::Mat band_f(band_h, W, CV_32FC3);
            for (int inner_r = 0; inner_r < band_h; ++inner_r) {
                float alpha;
                if (inner_r < blur_rows) {
                    alpha = 0.0f;
                } else {
                    float g = static_cast<float>(inner_r - blur_rows) / fade_denom;
                    if (g > 1.0f) g = 1.0f;
                    alpha = 1.0f - std::pow(1.0f - g, fp);
                }
                const int br = is_top ? (band_h - 1 - inner_r) : inner_r;
                cv::addWeighted(edge_profile, 1.0f - alpha,
                                flat_row,     alpha,
                                0.0, band_f.row(br));
            }

            cv::Mat band;
            band_f.convertTo(band, CV_8UC3);
            return band;
        };

        cv::Mat top_edge, top_flat, bot_edge, bot_flat;
        if (pad_top > 0) sampleSide(true,  top_edge, top_flat);
        if (pad_bot > 0) sampleSide(false, bot_edge, bot_flat);

        cv::Mat top_band = buildBand(true,  pad_top, top_edge, top_flat);
        cv::Mat bot_band = buildBand(false, pad_bot, bot_edge, bot_flat);

        auto featherPano = [&](bool is_top, const cv::Mat& edge_profile) {
            const int actual_F = std::min(F, H / 2);
            const float denom = static_cast<float>(std::max(actual_F - 1, 1));
            for (int r = 0; r < actual_F; ++r) {
                const int pano_r = is_top ? r : (H - 1 - r);
                const float t = (actual_F == 1) ? 1.0f
                              : static_cast<float>(r) / denom;
                cv::Mat pano_row_f;
                pano.row(pano_r).convertTo(pano_row_f, CV_32FC3);
                cv::Mat blended_f;
                cv::addWeighted(edge_profile, 1.0f - t,
                                pano_row_f,   t,
                                0.0, blended_f);
                cv::Mat blended_u8;
                blended_f.convertTo(blended_u8, CV_8UC3);
                blended_u8.copyTo(pano.row(pano_r));
            }
        };

        if (F > 0) {
            if (pad_top > 0) featherPano(true,  top_edge);
            if (pad_bot > 0) featherPano(false, bot_edge);
        }

        std::vector<cv::Mat> stack;
        stack.reserve(3);
        if (!top_band.empty()) stack.push_back(top_band);
        stack.push_back(pano);
        if (!bot_band.empty()) stack.push_back(bot_band);

        cv::Mat out;
        cv::vconcat(stack, out);

        std::cout << "Fill panorama: " << W << "x" << H
                  << "  ->  " << out.cols << "x" << out.rows
                  << "  (top_pad=" << pad_top << ", bot_pad=" << pad_bot
                  << ", crop_top=" << crop_top << ", crop_bot=" << crop_bot
                  << ", v_cam=" << std::fixed << std::setprecision(1)
                  << vertical_camera_deg << "deg"
                  << ", src=" << S << ", blur=" << K
                  << ", blur_ratio=" << std::fixed << std::setprecision(2) << BR
                  << ", fade=" << F
                  << ", fade_power=" << std::fixed << std::setprecision(2) << FP
                  << ")\n";

        pano = out;
        return;
    }

    // ──────────────────────────────────────────────────────────────────────
    // Per-column path: panorama has black wedges along the top/bottom.
    // The fill ramp follows the per-column real-content boundary.
    // ──────────────────────────────────────────────────────────────────────

    // ── 1. Per-column top/bottom join indices from the mask ───────────────
    std::vector<int> top_join(W, -1);
    std::vector<int> bot_join(W, -1);
    for (int x = 0; x < W; ++x) {
        int t = 0;
        while (t < H && mask.at<uchar>(t, x) == 0) ++t;
        if (t >= H) continue;
        int b = H - 1;
        while (b >= 0 && mask.at<uchar>(b, x) == 0) --b;
        top_join[x] = t;
        bot_join[x] = b;
    }

    // ── 1b. Cyclic-aware fill for fully-empty columns ─────────────────────
    //
    // The cylindrical warp + multi-band blend can leave a thin band of
    // completely-empty columns at the 360° wrap seam (small bundle-
    // adjustment drift means the first and last image edges don't quite
    // meet). The legacy backfill only inherited the wedge indices from a
    // neighbour and never touched the column's *pixel* content, so the
    // body of the empty column stayed black after the per-column ramps
    // (which only ever paint the top/bottom wedge rows). When a panorama
    // viewer wraps left ↔ right, that residual black middle band shows up
    // as a vertical black line right at the seam.
    //
    // We now (a) search for the nearest left and right non-empty neighbour
    // *cyclically* — the column just left of x=0 is x=W−1, the column just
    // right of x=W−1 is x=0 — so empty columns at the wrap boundary find
    // the closest real column on either side, and (b) copy the chosen
    // neighbour's pixels and mask into the empty column so every later
    // stage sees genuine content instead of a black band.
    bool any_empty = false;
    for (int x = 0; x < W; ++x) {
        if (top_join[x] < 0) { any_empty = true; break; }
    }
    if (any_empty) {
        std::vector<int> nearest_left(W, -1), nearest_right(W, -1);
        {
            int prev = -1;
            for (int i = 0; i < 2 * W; ++i) {
                const int xc = i % W;
                if (top_join[xc] >= 0) prev = xc;
                if (i >= W) nearest_left[xc] = prev;
            }
            int next = -1;
            for (int i = 2 * W - 1; i >= 0; --i) {
                const int xc = i % W;
                if (top_join[xc] >= 0) next = xc;
                if (i < W) nearest_right[xc] = next;
            }
        }

        // Make the local mask handle independent before mutating it so the
        // caller's buffer is never touched. (In the crop branch above the
        // slice already cloned the mask, so this is a no-op there.)
        if (mask.data == mask_in.data) mask = mask.clone();

        for (int x = 0; x < W; ++x) {
            if (top_join[x] >= 0) continue;

            const int lx = nearest_left[x];
            const int rx = nearest_right[x];

            int sx;
            if (lx < 0 && rx < 0) {
                // Defensive: every column is empty. Treat the column as
                // full canvas so the downstream code does something
                // deterministic and we don't run past the array edges.
                top_join[x] = 0;
                bot_join[x] = H - 1;
                continue;
            } else if (lx < 0) {
                sx = rx;
            } else if (rx < 0) {
                sx = lx;
            } else {
                const int dl = (x - lx + W) % W;
                const int dr = (rx - x + W) % W;
                sx = (dl <= dr) ? lx : rx;
            }

            // Inherit the donor's wedge depth so the fade ramp at the
            // filled column matches the donor's exactly, then copy the
            // donor's pixel + mask data so the body of the column shows
            // real content instead of black.
            top_join[x] = top_join[sx];
            bot_join[x] = bot_join[sx];
            pano.col(sx).copyTo(pano.col(x));
            mask.col(sx).copyTo(mask.col(x));
        }
    }

    // ── 2. Build wedge-free edge strips (S × W) for top and bottom ────────
    cv::Mat top_strip(S, W, CV_8UC3);
    cv::Mat bot_strip(S, W, CV_8UC3);
    for (int x = 0; x < W; ++x) {
        const int t = top_join[x];
        const int b = bot_join[x];
        const int span = std::max(1, b - t + 1);
        for (int i = 0; i < S; ++i) {
            const int sr_top = std::min(t + i, t + span - 1);
            const int sr_bot = std::max(b - i, t);
            top_strip.at<cv::Vec3b>(i, x) = pano.at<cv::Vec3b>(sr_top, x);
            bot_strip.at<cv::Vec3b>(i, x) = pano.at<cv::Vec3b>(sr_bot, x);
        }
    }

    // ── 3. Per-side edge_profile + flat colour ────────────────────────────
    auto computeProfile = [&](const cv::Mat& strip,
                              cv::Mat& edge_profile,
                              cv::Mat& flat_row) {
        cv::Mat mean_row_f;
        cv::reduce(strip, mean_row_f, 0, cv::REDUCE_AVG, CV_32FC3);
        cv::GaussianBlur(mean_row_f, edge_profile,
                         cv::Size(K, 1), 0, 0, cv::BORDER_REFLECT_101);
        const cv::Scalar mean_bgr = cv::mean(strip);
        flat_row = cv::Mat(1, W, CV_32FC3,
                           cv::Scalar(mean_bgr[0], mean_bgr[1], mean_bgr[2]));
    };
    cv::Mat top_edge, top_flat, bot_edge, bot_flat;
    computeProfile(top_strip, top_edge, top_flat);
    computeProfile(bot_strip, bot_edge, bot_flat);

    // ── 4. Allocate output float canvas and copy real content into it ─────
    const int OUT_H = H + pad_top + pad_bot;
    cv::Mat out_f(OUT_H, W, CV_32FC3, cv::Scalar(0, 0, 0));
    {
        cv::Mat pano_f;
        pano.convertTo(pano_f, CV_32FC3);
        pano_f.copyTo(out_f.rowRange(pad_top, pad_top + H));
    }

    // ── 5. Per-column ramps + pano-side feather ───────────────────────────
    for (int x = 0; x < W; ++x) {
        const int t = top_join[x];
        const int b = bot_join[x];
        const int D_top = pad_top + t;
        const int D_bot = pad_bot + (H - 1 - b);

        const cv::Vec3f ep_top = top_edge.at<cv::Vec3f>(0, x);
        const cv::Vec3f fl_top = top_flat.at<cv::Vec3f>(0, x);
        const cv::Vec3f ep_bot = bot_edge.at<cv::Vec3f>(0, x);
        const cv::Vec3f fl_bot = bot_flat.at<cv::Vec3f>(0, x);

        if (D_top > 0) {
            const int blur_rows = std::min(D_top, static_cast<int>(BR * D_top));
            const float fade_denom = static_cast<float>(std::max(D_top - blur_rows - 1, 1));
            for (int r = 0; r < D_top; ++r) {
                const int inner_r = D_top - 1 - r;
                float alpha;
                if (inner_r < blur_rows) {
                    alpha = 0.0f;
                } else {
                    float g = static_cast<float>(inner_r - blur_rows) / fade_denom;
                    if (g > 1.0f) g = 1.0f;
                    alpha = 1.0f - std::pow(1.0f - g, fp);
                }
                cv::Vec3f& dst = out_f.at<cv::Vec3f>(r, x);
                dst[0] = (1.0f - alpha) * ep_top[0] + alpha * fl_top[0];
                dst[1] = (1.0f - alpha) * ep_top[1] + alpha * fl_top[1];
                dst[2] = (1.0f - alpha) * ep_top[2] + alpha * fl_top[2];
            }
        }

        if (D_bot > 0) {
            const int blur_rows = std::min(D_bot, static_cast<int>(BR * D_bot));
            const float fade_denom = static_cast<float>(std::max(D_bot - blur_rows - 1, 1));
            const int row_start = pad_top + b + 1;
            for (int r = 0; r < D_bot; ++r) {
                float alpha;
                if (r < blur_rows) {
                    alpha = 0.0f;
                } else {
                    float g = static_cast<float>(r - blur_rows) / fade_denom;
                    if (g > 1.0f) g = 1.0f;
                    alpha = 1.0f - std::pow(1.0f - g, fp);
                }
                cv::Vec3f& dst = out_f.at<cv::Vec3f>(row_start + r, x);
                dst[0] = (1.0f - alpha) * ep_bot[0] + alpha * fl_bot[0];
                dst[1] = (1.0f - alpha) * ep_bot[1] + alpha * fl_bot[1];
                dst[2] = (1.0f - alpha) * ep_bot[2] + alpha * fl_bot[2];
            }
        }

        if (F > 0) {
            const int content_height = b - t + 1;
            const int actual_F = std::min(F, content_height / 2);
            if (actual_F > 0) {
                const float denom = static_cast<float>(std::max(actual_F - 1, 1));
                for (int r = 0; r < actual_F; ++r) {
                    const float tv = (actual_F == 1) ? 1.0f
                                    : static_cast<float>(r) / denom;
                    {
                        cv::Vec3f& dst = out_f.at<cv::Vec3f>(pad_top + t + r, x);
                        dst[0] = (1.0f - tv) * ep_top[0] + tv * dst[0];
                        dst[1] = (1.0f - tv) * ep_top[1] + tv * dst[1];
                        dst[2] = (1.0f - tv) * ep_top[2] + tv * dst[2];
                    }
                    {
                        cv::Vec3f& dst = out_f.at<cv::Vec3f>(pad_top + b - r, x);
                        dst[0] = (1.0f - tv) * ep_bot[0] + tv * dst[0];
                        dst[1] = (1.0f - tv) * ep_bot[1] + tv * dst[1];
                        dst[2] = (1.0f - tv) * ep_bot[2] + tv * dst[2];
                    }
                }
            }
        }
    }

    cv::Mat out_u8;
    out_f.convertTo(out_u8, CV_8UC3);

    std::cout << "Fill panorama (per-column): " << W << "x" << H
              << "  ->  " << out_u8.cols << "x" << out_u8.rows
              << "  (top_pad=" << pad_top << ", bot_pad=" << pad_bot
              << ", crop_top=" << crop_top << ", crop_bot=" << crop_bot
              << ", v_cam=" << std::fixed << std::setprecision(1)
              << vertical_camera_deg << "deg"
              << ", src=" << S << ", blur=" << K
              << ", blur_ratio=" << std::fixed << std::setprecision(2) << BR
              << ", fade=" << F
              << ", fade_power=" << std::fixed << std::setprecision(2) << FP
              << ")\n";

    pano = out_u8;
}

}  // namespace detail
}  // namespace camera360
