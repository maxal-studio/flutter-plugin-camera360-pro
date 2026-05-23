#include "PanoramaFill.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace camera360 {
namespace detail {

namespace {

// Compute the fractional source-pano-row that each output canvas row should
// sample from.
//
// `cyl_remap = true`:
//   Output row R corresponds to equirect elevation
//     φ(R) = (H_target/2 − 0.5 − R) · π / H_target           [radians]
//   In the cylindrical strip the same elevation lives at pixel
//     v(φ) = −f · tan(φ)
//   (OpenCV image-y-down convention: v > 0 is below the horizon, v < 0 above.)
//   The strip's pano row 0 is at cyl-y `cyl_v_top_pano`, so the source row is
//     src(R) = v(φ(R)) − cyl_v_top_pano
//
// `cyl_remap = false` (legacy fallback when the stitcher couldn't supply
// the cylindrical focal length):
//   Output row R is placed 1:1 from the strip, with the strip's geometric
//   centre parked on `cam_axis_row` derived from `vertical_camera_deg`. This
//   matches the pre-remap behaviour byte-for-byte at deg = 90 + horizon
//   captures, and is only approximately correct otherwise.
std::vector<double> buildSrcRowMap(int W, int H, int H_target,
                                   double vertical_camera_deg,
                                   double cyl_focal,
                                   double cyl_v_top_pano,
                                   bool   cyl_remap)
{
    std::vector<double> src(H_target);
    if (cyl_remap) {
        for (int R = 0; R < H_target; ++R) {
            const double phi = (H_target / 2.0 - 0.5 - R) * CV_PI
                             / static_cast<double>(H_target);
            const double v   = -cyl_focal * std::tan(phi);
            src[R] = v - cyl_v_top_pano;
        }
    } else {
        const double elev_cam = std::max(-90.0,
            std::min(90.0, vertical_camera_deg - 90.0));
        const double pix_per_deg = static_cast<double>(H_target) / 180.0;
        const double cam_axis_row = H_target / 2.0 - elev_cam * pix_per_deg;
        const double strip_top_f  = cam_axis_row - H / 2.0;
        for (int R = 0; R < H_target; ++R) {
            src[R] = R - strip_top_f;
        }
    }
    (void)W;
    return src;
}

// Bilinear in row: sample the row of `pano` at fractional position `r_f`,
// clamped to [r_lo, r_hi], for column x.
inline cv::Vec3f sampleRowBilinear(const cv::Mat& pano, int x,
                                   double r_f, int r_lo, int r_hi)
{
    if (r_f < r_lo) r_f = r_lo;
    if (r_f > r_hi) r_f = r_hi;
    const int r0 = std::min(static_cast<int>(std::floor(r_f)), r_hi);
    const int r1 = std::min(r0 + 1, r_hi);
    const float a = static_cast<float>(r_f - r0);
    const cv::Vec3b& p0 = pano.at<cv::Vec3b>(r0, x);
    const cv::Vec3b& p1 = pano.at<cv::Vec3b>(r1, x);
    return cv::Vec3f(
        (1.0f - a) * p0[0] + a * p1[0],
        (1.0f - a) * p0[1] + a * p1[1],
        (1.0f - a) * p0[2] + a * p1[2]);
}

}  // namespace

void fillTopBottomBlur(cv::Mat&        pano,
                       const cv::Mat&  mask_in,
                       double          target_ratio,
                       int             source_px,
                       int             blur_kernel,
                       int             fade_px,
                       double          blur_ratio,
                       double          fade_power,
                       double          vertical_camera_deg,
                       double          cyl_focal,
                       double          cyl_v_top_pano,
                       bool            flat_use_edge_color,
                       int             flat_b,
                       int             flat_g,
                       int             flat_r)
{
    if (pano.empty()) {
        std::cerr << "Fill panorama: empty panorama, skipping\n";
        return;
    }
    if (target_ratio <= 0.0) return;

    const int W = pano.cols;
    const int H = pano.rows;
    const int H_target = static_cast<int>(std::round(W / target_ratio));
    if (H_target <= 0) return;

    cv::Mat mask = mask_in;

    // ── Decide on remap path ──────────────────────────────────────────────
    //
    // Cylindrical → equirect remap when the stitcher supplied the focal
    // length and the v-coordinate of the pano's row 0. Without those two
    // numbers we fall back to the legacy "paste 1:1 with a `deg`-biased
    // offset" placement — which is only approximately correct near the
    // horizon and only for target_ratio = 2.0 + full 360° sweeps.
    const bool cyl_remap = (cyl_focal > 0.0);

    // ── Build the source-row mapping once (depends only on R) ─────────────
    const std::vector<double> src_row = buildSrcRowMap(
        W, H, H_target,
        vertical_camera_deg, cyl_focal, cyl_v_top_pano, cyl_remap);

    // src_row is monotonically increasing in R for cyl_remap (since tan is
    // monotone and the elevation decreases with R) and trivially so for the
    // legacy linear path. Both branches therefore share the same logic for
    // mapping a pano-row range [t, b] back to a canvas-row range
    // [R_first, R_last].
    auto canvasRangeFor = [&](double t, double b, int& R_first, int& R_last) {
        // Smallest R such that src_row[R] >= t.
        R_first = 0;
        while (R_first < H_target && src_row[R_first] < t) ++R_first;
        // Largest R such that src_row[R] <= b.
        R_last = H_target - 1;
        while (R_last >= 0 && src_row[R_last] > b) --R_last;
    };

    // ── Per-column top/bottom valid pano-row indices (mask wedges) ───────
    const bool has_usable_mask = !mask.empty() &&
                                 mask.cols == W && mask.rows == H &&
                                 mask.type() == CV_8UC1;
    std::vector<int> top_join(W, 0);
    std::vector<int> bot_join(W, H - 1);
    bool any_empty = false;

    if (has_usable_mask) {
        for (int x = 0; x < W; ++x) {
            int t = 0;
            while (t < H && mask.at<uchar>(t, x) == 0) ++t;
            if (t >= H) {
                top_join[x] = -1;
                bot_join[x] = -1;
                any_empty = true;
                continue;
            }
            int b = H - 1;
            while (b >= 0 && mask.at<uchar>(b, x) == 0) --b;
            top_join[x] = t;
            bot_join[x] = b;
        }

        // ── Cyclic backfill for fully-empty columns ──────────────────────
        //
        // The cylindrical warp + multi-band blend can leave a thin band of
        // completely-empty columns at the 360° wrap seam. Inherit pixels +
        // mask from the nearest non-empty neighbour (cyclically — empty
        // columns at the wrap boundary find the closest real column on
        // either side) so the remap below has real content to sample.
        if (any_empty) {
            std::vector<int> nearest_left(W, -1), nearest_right(W, -1);
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

            if (mask.data == mask_in.data) mask = mask.clone();

            for (int x = 0; x < W; ++x) {
                if (top_join[x] >= 0) continue;
                const int lx = nearest_left[x];
                const int rx = nearest_right[x];
                int sx;
                if (lx < 0 && rx < 0) {
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
                top_join[x] = top_join[sx];
                bot_join[x] = bot_join[sx];
                pano.col(sx).copyTo(pano.col(x));
                mask.col(sx).copyTo(mask.col(x));
            }
        }
    }

    // ── Parameter sanitisation ────────────────────────────────────────────
    const int    S  = std::max(1, std::min(source_px, H));
    int          K  = std::max(3, blur_kernel);
    if ((K & 1) == 0) ++K;
    const int    F  = std::max(0, fade_px);
    const double BR = std::min(1.0, std::max(0.0, blur_ratio));
    const double FP = std::max(1.0, fade_power);
    const float  fp = static_cast<float>(FP);

    // ── Build per-column edge profiles (S rows nearest each column's
    //     real-content boundary, mean → horizontally blurred → flat) ──
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

    cv::Mat top_mean_f, bot_mean_f, top_edge, bot_edge, top_flat, bot_flat;
    cv::reduce(top_strip, top_mean_f, 0, cv::REDUCE_AVG, CV_32FC3);
    cv::reduce(bot_strip, bot_mean_f, 0, cv::REDUCE_AVG, CV_32FC3);
    cv::GaussianBlur(top_mean_f, top_edge, cv::Size(K, 1), 0, 0,
                     cv::BORDER_REFLECT_101);
    cv::GaussianBlur(bot_mean_f, bot_edge, cv::Size(K, 1), 0, 0,
                     cv::BORDER_REFLECT_101);
    {
        const auto clamp_u8 = [](int v) {
            return static_cast<double>(std::max(0, std::min(255, v)));
        };
        if (flat_use_edge_color) {
            const cv::Scalar top_avg = cv::mean(top_strip);
            const cv::Scalar bot_avg = cv::mean(bot_strip);
            top_flat = cv::Mat(1, W, CV_32FC3,
                               cv::Scalar(top_avg[0], top_avg[1], top_avg[2]));
            bot_flat = cv::Mat(1, W, CV_32FC3,
                               cv::Scalar(bot_avg[0], bot_avg[1], bot_avg[2]));
        } else {
            const cv::Scalar flat(clamp_u8(flat_b),
                                  clamp_u8(flat_g),
                                  clamp_u8(flat_r));
            top_flat = cv::Mat(1, W, CV_32FC3, flat);
            bot_flat = cv::Mat(1, W, CV_32FC3, flat);
        }
    }

    // ── Allocate output canvas (float-precision; collapsed to u8 below) ──
    cv::Mat out_f(H_target, W, CV_32FC3, cv::Scalar(0, 0, 0));

    // Per-column accounting so the verbose log can summarise the work done.
    int summary_R_first = H_target;
    int summary_R_last  = -1;

    // ── Per-column: bilinear-sample the strip into the canvas, then fill ──
    for (int x = 0; x < W; ++x) {
        const int t = top_join[x];
        const int b = bot_join[x];
        if (t < 0 || b < 0) continue;       // should not happen after backfill

        int R_first, R_last;
        canvasRangeFor(static_cast<double>(t), static_cast<double>(b),
                       R_first, R_last);

        const cv::Vec3f ep_top = top_edge.at<cv::Vec3f>(0, x);
        const cv::Vec3f fl_top = top_flat.at<cv::Vec3f>(0, x);
        const cv::Vec3f ep_bot = bot_edge.at<cv::Vec3f>(0, x);
        const cv::Vec3f fl_bot = bot_flat.at<cv::Vec3f>(0, x);

        // 1. Sampled content rows.
        if (R_first <= R_last) {
            for (int R = R_first; R <= R_last; ++R) {
                const cv::Vec3f v = sampleRowBilinear(pano, x, src_row[R], t, b);
                out_f.at<cv::Vec3f>(R, x) = v;
            }
            if (R_first < summary_R_first) summary_R_first = R_first;
            if (R_last  > summary_R_last)  summary_R_last  = R_last;
        }

        // 2. Top fill: canvas rows [0, R_first).
        const int D_top = std::max(0, R_first);
        if (D_top > 0) {
            const int blur_rows = std::min(D_top, static_cast<int>(BR * D_top));
            const float fade_denom = static_cast<float>(
                std::max(D_top - blur_rows - 1, 1));
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

        // 3. Bottom fill: canvas rows (R_last, H_target).
        const int D_bot = std::max(0, H_target - 1 - R_last);
        if (D_bot > 0) {
            const int blur_rows = std::min(D_bot, static_cast<int>(BR * D_bot));
            const float fade_denom = static_cast<float>(
                std::max(D_bot - blur_rows - 1, 1));
            const int row_start = R_last + 1;
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

        // 4. Pano-side feather: blend sampled rows nearest the joins with
        //    the edge profile so the fill→strip transition is smooth.
        if (F > 0 && R_last > R_first) {
            const int content_height = R_last - R_first + 1;
            const int actual_F = std::min(F, content_height / 2);
            if (actual_F > 0) {
                const float denom = static_cast<float>(std::max(actual_F - 1, 1));
                for (int r = 0; r < actual_F; ++r) {
                    const float tv = (actual_F == 1) ? 1.0f
                                  : static_cast<float>(r) / denom;
                    {
                        cv::Vec3f& dst = out_f.at<cv::Vec3f>(R_first + r, x);
                        dst[0] = (1.0f - tv) * ep_top[0] + tv * dst[0];
                        dst[1] = (1.0f - tv) * ep_top[1] + tv * dst[1];
                        dst[2] = (1.0f - tv) * ep_top[2] + tv * dst[2];
                    }
                    {
                        cv::Vec3f& dst = out_f.at<cv::Vec3f>(R_last - r, x);
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

    std::cout << "Fill panorama: " << W << "x" << H
              << "  ->  " << out_u8.cols << "x" << out_u8.rows
              << "  (mode=" << (cyl_remap ? "cyl-remap" : "legacy");
    if (cyl_remap) {
        // Strip's actual angular extent (taken at the column-wise widest
        // valid range, summarised across the canvas).
        const int R0 = std::max(0, std::min(H_target - 1, summary_R_first));
        const int R1 = std::max(0, std::min(H_target - 1, summary_R_last));
        const double phi_top_deg =
            (H_target / 2.0 - 0.5 - R0) * 180.0 / H_target;
        const double phi_bot_deg =
            (H_target / 2.0 - 0.5 - R1) * 180.0 / H_target;
        std::cout << ", cyl_f=" << std::fixed << std::setprecision(1) << cyl_focal
                  << ", cyl_v_top=" << cyl_v_top_pano
                  << ", strip_phi=[" << std::fixed << std::setprecision(1)
                  << phi_bot_deg << ".." << phi_top_deg << "]deg";
    } else {
        std::cout << ", v_cam=" << std::fixed << std::setprecision(1)
                  << vertical_camera_deg << "deg";
    }
    std::cout << ", src=" << S << ", blur=" << K
              << ", blur_ratio=" << std::fixed << std::setprecision(2) << BR
              << ", fade=" << F
              << ", fade_power=" << std::fixed << std::setprecision(2) << FP
              << ", flat=" << (flat_use_edge_color
                  ? "edge"
                  : ("bgr(" + std::to_string(flat_b) + "," +
                               std::to_string(flat_g) + "," +
                               std::to_string(flat_r) + ")"))
              << ")\n";

    pano = out_u8;
}

}  // namespace detail
}  // namespace camera360
