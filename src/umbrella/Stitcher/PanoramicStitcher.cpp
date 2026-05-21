#include "PanoramicStitcher.hpp"
#include "PanoramaCrop.hpp"
#include "PanoramaFill.hpp"
#include "ProgressTracker.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/stitching/detail/blenders.hpp>
#include <opencv2/stitching/detail/camera.hpp>
#include <opencv2/stitching/detail/exposure_compensate.hpp>
#include <opencv2/stitching/detail/motion_estimators.hpp>
#include <opencv2/stitching/detail/seam_finders.hpp>
#include <opencv2/stitching/detail/warpers.hpp>
#include <opencv2/stitching/warpers.hpp>

namespace camera360 {

namespace {

// Tiny RAII timer that prints "[stitch] <label>: <ms> ms" when verbose_timing
// is enabled. Held by reference so the cumulative log lines stay grouped.
struct StageTimer {
    using Clock = std::chrono::steady_clock;
    bool verbose;
    Clock::time_point t0;
    explicit StageTimer(bool v) : verbose(v), t0(Clock::now()) {}
    long long ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   Clock::now() - t0).count();
    }
    long long lap() {
        auto t = Clock::now();
        auto d = std::chrono::duration_cast<std::chrono::milliseconds>(t - t0).count();
        t0 = t;
        return d;
    }
    void log(const char* label) {
        if (!verbose) { lap(); return; }
        std::cout << "[stitch] " << std::left << std::setw(22) << label
                  << ": " << lap() << " ms\n";
    }
};

// ── Wrap-seam tonal alignment + feather ──────────────────────────────────────
//
// After the multi-band blender produces a full panorama, the left and right
// edges still belong to different source images (capture #0 and capture
// #N-1). For a sweep that already closes geometrically, the remaining wrap
// mismatch is dominated by photometric drift (different exposure / white
// balance per frame), with a thin pixel-level offset on top.
//
// `wrapSeamBlend` fixes both in a single post-blend pass:
//   1. Sample a strip of `sample_px` columns at each edge, using only rows
//      where both edges are mask-valid (so cropped wedges / black caps do
//      not poison the statistics).
//   2. Compute the per-channel mean colour difference  delta = mean(right)
//      - mean(left)  and split it in half: shift the left edge by +delta/2
//      tapering linearly to 0 over `correct_px` columns, and the right
//      edge by -delta/2 tapering from 0. The interior of the panorama is
//      untouched.
//   3. Optionally alpha-blend the outermost `feather_px` columns between
//      the two edges so any remaining pixel-level discontinuity fades out.
//
// Operates in place on a CV_16SC3 blender output (the same buffer that
// would otherwise be convertTo'd to CV_8U).
void wrapSeamBlend(cv::Mat& result, const cv::Mat& mask, bool verbose) {
    using cv::Vec3d;
    const int W = result.cols;
    const int H = result.rows;
    if (W < 32 || H < 4) return;
    if (result.type() != CV_16SC3 || mask.type() != CV_8UC1) return;

    // Strip widths. correct_px is intentionally large so the tonal taper
    // is gentle (delta/2 over ~12 % of the canvas → invisible per-column
    // shift even for a 20-unit BGR delta). feather_px stays tiny — it
    // only needs to hide single-pixel mismatch at the very edges.
    const int sample_px  = std::clamp(W / 60,  6, 32);
    const int correct_px = std::clamp(W / 8,  32, 256);
    const int feather_px = std::clamp(W / 200, 2, 12);

    Vec3d sumL(0, 0, 0), sumR(0, 0, 0);
    long countL = 0, countR = 0;
    for (int y = 0; y < H; ++y) {
        const uchar* m   = mask.ptr<uchar>(y);
        const short* row = result.ptr<short>(y);
        bool left_ok = false, right_ok = false;
        for (int x = 0; x < sample_px; ++x)
            if (m[x])           { left_ok = true; break; }
        for (int x = W - sample_px; x < W; ++x)
            if (m[x])           { right_ok = true; break; }
        if (!left_ok || !right_ok) continue;

        for (int x = 0; x < sample_px; ++x) {
            if (m[x]) {
                sumL[0] += row[3 * x + 0];
                sumL[1] += row[3 * x + 1];
                sumL[2] += row[3 * x + 2];
                ++countL;
            }
        }
        const int xR0 = W - sample_px;
        for (int x = 0; x < sample_px; ++x) {
            if (m[xR0 + x]) {
                sumR[0] += row[3 * (xR0 + x) + 0];
                sumR[1] += row[3 * (xR0 + x) + 1];
                sumR[2] += row[3 * (xR0 + x) + 2];
                ++countR;
            }
        }
    }
    if (countL < 50 || countR < 50) {
        if (verbose) {
            std::cout << "[stitch] wrap seam blend  : skipped (L=" << countL
                      << " R=" << countR << " mask-valid pix)\n";
        }
        return;
    }

    const Vec3d meanL = sumL * (1.0 / countL);
    const Vec3d meanR = sumR * (1.0 / countR);
    const Vec3d delta = meanR - meanL;
    const Vec3d half  = delta * 0.5;

    if (verbose) {
        std::cout << "[stitch] wrap seam delta  : BGR=("
                  << std::fixed << std::setprecision(2)
                  << delta[0] << ", " << delta[1] << ", " << delta[2] << ")"
                  << "  taper=" << correct_px << "px  feather=" << feather_px
                  << "px  L=" << countL << " R=" << countR << " pix\n";
    }

    auto sat16 = [](double v) -> short {
        if (v < -32768.0) return -32768;
        if (v >  32767.0) return  32767;
        return static_cast<short>(std::lround(v));
    };

    // Tapered tonal correction. Left edge gets +half, right edge gets
    // -half, both fading to 0 by `correct_px` columns in from the edge.
    const int rstart = std::max(0, W - correct_px);
    for (int y = 0; y < H; ++y) {
        short* row = result.ptr<short>(y);
        for (int x = 0; x < correct_px && x < W; ++x) {
            const double t = 1.0 - static_cast<double>(x) / correct_px;
            row[3 * x + 0] = sat16(row[3 * x + 0] + t * half[0]);
            row[3 * x + 1] = sat16(row[3 * x + 1] + t * half[1]);
            row[3 * x + 2] = sat16(row[3 * x + 2] + t * half[2]);
        }
        for (int x = rstart; x < W; ++x) {
            const double t = static_cast<double>(x - rstart) / correct_px;
            row[3 * x + 0] = sat16(row[3 * x + 0] - t * half[0]);
            row[3 * x + 1] = sat16(row[3 * x + 1] - t * half[1]);
            row[3 * x + 2] = sat16(row[3 * x + 2] - t * half[2]);
        }
    }

    // Feather: mirror-pair the outermost feather_px columns and alpha-blend
    // them together so the very seam pixels average out. Blend weight
    // starts at 0.5 / 0.5 at the edge (k = 0) and tapers back to no-op as
    // k approaches feather_px.
    if (feather_px > 0) {
        for (int y = 0; y < H; ++y) {
            const uchar* m   = mask.ptr<uchar>(y);
            short*       row = result.ptr<short>(y);
            for (int k = 0; k < feather_px; ++k) {
                const int xl = k;
                const int xr = W - 1 - k;
                if (!m[xl] || !m[xr]) continue;
                const double a = 0.5 +
                    0.5 * (static_cast<double>(k) / feather_px);
                for (int c = 0; c < 3; ++c) {
                    const double L = row[3 * xl + c];
                    const double R = row[3 * xr + c];
                    row[3 * xl + c] = sat16(a * L + (1.0 - a) * R);
                    row[3 * xr + c] = sat16(a * R + (1.0 - a) * L);
                }
            }
        }
    }
}

}  // anonymous namespace

PanoramicStitcher::PanoramicStitcher(const StitcherOptions& opt) : opt_(opt) {}

bool PanoramicStitcher::stitch(const std::vector<cv::Mat>&                 images_in,
                               std::vector<cv::detail::ImageFeatures>      features,
                               std::vector<cv::detail::MatchesInfo>        pairwise_matches,
                               cv::Mat&                                     pano)
{
    using namespace cv;
    using namespace cv::detail;
    using Clock = std::chrono::steady_clock;

    if (images_in.size() < 2) {
        std::cerr << "Need at least 2 images\n";
        return false;
    }

    const auto t_total_start = Clock::now();
    StageTimer stage(opt_.verbose_timing);

    // ── 1. Keep biggest connected component (drops images we couldn't link) ──
    std::vector<int> indices = leaveBiggestComponent(features, pairwise_matches,
                                                     static_cast<float>(opt_.conf_thresh));
    if (indices.size() < 2) {
        std::cerr << "leaveBiggestComponent: only " << indices.size() << " image(s) connected\n";
        return false;
    }

    std::vector<Mat> images;
    images.reserve(indices.size());
    for (int i : indices) images.push_back(images_in[i]);

    const int N = static_cast<int>(images.size());
    std::cout << "After biggest component: " << N << " images\n";
    stage.log("connected component");
    progress::set(progress::Stage::EstimateCameras, 0);

    // ── 2. Initial cameras from homography graph ─────────────────────────────
    std::vector<CameraParams> cameras;
    {
        Ptr<Estimator> est = makePtr<HomographyBasedEstimator>();
        if (!(*est)(features, pairwise_matches, cameras)) {
            std::cerr << "Homography estimator failed\n";
            return false;
        }
    }
    for (auto& c : cameras) {
        Mat R; c.R.convertTo(R, CV_32F); c.R = R;
    }
    stage.log("homography estimator");
    progress::set(progress::Stage::EstimateCameras, 100);

    // ── 3. Bundle adjustment (ray cost) ──────────────────────────────────────
    progress::set(progress::Stage::BundleAdjust, 0);
    {
        Ptr<BundleAdjusterBase> ba = makePtr<BundleAdjusterRay>();
        ba->setConfThresh(opt_.conf_thresh);
        Mat_<uchar> refine_mask = Mat::zeros(3, 3, CV_8U);
        refine_mask(0,0) = 1; refine_mask(0,1) = 1; refine_mask(0,2) = 1;
        refine_mask(1,1) = 1; refine_mask(1,2) = 1;
        ba->setRefinementMask(refine_mask);
        if (!(*ba)(features, pairwise_matches, cameras)) {
            std::cerr << "BundleAdjusterRay failed\n";
            return false;
        }
    }
    stage.log("bundle adjustment");
    progress::set(progress::Stage::BundleAdjust, 100);

    // ── 4. Median focal → warp scale; optional wave correction ──────────────
    std::vector<double> focals;
    for (const auto& c : cameras) focals.push_back(c.focal);
    std::sort(focals.begin(), focals.end());
    const double warped_image_scale = focals.size() % 2
        ? focals[focals.size() / 2]
        : 0.5 * (focals[focals.size() / 2 - 1] + focals[focals.size() / 2]);
    std::cout << "Median focal (warp scale): " << warped_image_scale << "\n";

    if (opt_.wave_correct) {
        std::vector<Mat> rmats;
        for (auto& c : cameras) rmats.push_back(c.R.clone());
        waveCorrect(rmats, WAVE_CORRECT_HORIZ);
        for (size_t i = 0; i < cameras.size(); ++i) cameras[i].R = rmats[i];
    }
    stage.log("wave correct");

    // Compute the seam-finding scale. OpenCV's reference pipeline runs
    // exposure-compensation + seam-finding at ~0.1 MP, then warps again at
    // full resolution only for the final blend. Doing seams at full-res is
    // ~10× more expensive (DpSeamFinder is roughly O(W*H) per overlap).
    const double full_area  = static_cast<double>(images[0].cols) * images[0].rows;
    const double seam_scale = (opt_.seam_megapix > 0.0)
        ? std::min(1.0, std::sqrt(opt_.seam_megapix * 1e6 / full_area))
        : 1.0;
    if (opt_.verbose_timing) {
        std::cout << "[stitch] seam_scale       : " << seam_scale
                  << "  (" << static_cast<int>(images[0].cols * seam_scale) << "x"
                  << static_cast<int>(images[0].rows * seam_scale) << " per image)\n";
    }

    // Cameras adapted to the seam scale (focal/principal point scale linearly).
    std::vector<CameraParams> cameras_seam = cameras;
    for (auto& c : cameras_seam) {
        c.focal *= seam_scale;
        c.ppx   *= seam_scale;
        c.ppy   *= seam_scale;
    }

    Ptr<WarperCreator> warper_creator = makePtr<cv::CylindricalWarper>();
    Ptr<RotationWarper> warper_seam = warper_creator->create(
        static_cast<float>(warped_image_scale * seam_scale));
    Ptr<RotationWarper> warper_full = warper_creator->create(
        static_cast<float>(warped_image_scale));

    // ── 5. Warp images + masks at SEAM scale ────────────────────────────────
    std::vector<Point> corners_seam(N);
    std::vector<Size>  sizes_seam(N);
    std::vector<UMat>  masks(N), masks_warped(N), images_warped(N), images_warped_f(N);

    progress::set(progress::Stage::Warp, 0);
    for (int i = 0; i < N; ++i) {
        Mat img_seam;
        if (seam_scale < 1.0) {
            resize(images[i], img_seam, Size(), seam_scale, seam_scale, INTER_LINEAR_EXACT);
        } else {
            img_seam = images[i];
        }

        masks[i].create(img_seam.size(), CV_8U);
        masks[i].setTo(Scalar::all(255));

        Mat_<float> K;
        cameras_seam[i].K().convertTo(K, CV_32F);

        corners_seam[i] = warper_seam->warp(img_seam, K, cameras_seam[i].R,
                                            INTER_LINEAR, BORDER_REFLECT, images_warped[i]);
        sizes_seam[i]   = images_warped[i].size();
        warper_seam->warp(masks[i], K, cameras_seam[i].R,
                          INTER_NEAREST, BORDER_CONSTANT, masks_warped[i]);
        images_warped[i].convertTo(images_warped_f[i], CV_32F);
        progress::set(progress::Stage::Warp, ((i + 1) * 100) / N);
    }
    stage.log("warp (seam scale)");

    // ── 6. Exposure compensation (block-based gain) at SEAM scale ───────────
    progress::set(progress::Stage::ExposureSeam, 0);
    Ptr<ExposureCompensator> compensator =
        ExposureCompensator::createDefault(ExposureCompensator::GAIN_BLOCKS);
    compensator->feed(corners_seam, images_warped, masks_warped);
    stage.log("exposure feed");
    progress::set(progress::Stage::ExposureSeam, 50);

    // ── 7. Seam finding at SEAM scale ───────────────────────────────────────
    Ptr<SeamFinder> seam_finder;
    if (opt_.seam_mode == StitcherOptions::Seam::Voronoi) {
        seam_finder = makePtr<VoronoiSeamFinder>();
    } else {
        seam_finder = makePtr<DpSeamFinder>(DpSeamFinder::COLOR);
    }
    seam_finder->find(images_warped_f, corners_seam, masks_warped);
    stage.log("seam find");
    progress::set(progress::Stage::ExposureSeam, 100);

    // masks_warped is the only thing we still need below (the seam cuts are
    // baked into it). Release everything else now.
    images_warped.clear();
    images_warped_f.clear();
    masks.clear();

    // ── 8. Composition: warp ONCE at full res, exposure-correct, blend ──────
    // Full-res corners/sizes via warpRoi (no need to actually warp the image).
    std::vector<Point> corners_full(N);
    std::vector<Size>  sizes_full(N);
    for (int i = 0; i < N; ++i) {
        Mat_<float> K;
        cameras[i].K().convertTo(K, CV_32F);
        Rect roi = warper_full->warpRoi(images[i].size(), K, cameras[i].R);
        corners_full[i] = roi.tl();
        sizes_full[i]   = roi.size();
    }

    Ptr<Blender> blender;
    Mat img_warped, img_warped_s;
    Mat dilated_mask, seam_mask, mask, mask_warped;

    long long ms_warp = 0, ms_apply = 0, ms_feed = 0;

    progress::set(progress::Stage::Compose, 0);
    for (int i = 0; i < N; ++i) {
        Mat_<float> K;
        cameras[i].K().convertTo(K, CV_32F);

        auto a = Clock::now();
        warper_full->warp(images[i], K, cameras[i].R,
                          INTER_LINEAR, BORDER_REFLECT, img_warped);
        mask.create(images[i].size(), CV_8U);
        mask.setTo(Scalar::all(255));
        warper_full->warp(mask, K, cameras[i].R,
                          INTER_NEAREST, BORDER_CONSTANT, mask_warped);
        auto b = Clock::now();
        ms_warp += std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();

        // BlocksCompensator::apply auto-resizes its stored gain map to the
        // input image, so feeding at seam scale + applying at compose scale
        // works unchanged. The `corner` argument is ignored by the
        // compensator.
        compensator->apply(i, corners_full[i], img_warped, mask_warped);
        img_warped.convertTo(img_warped_s, CV_16S);
        auto c = Clock::now();
        ms_apply += std::chrono::duration_cast<std::chrono::milliseconds>(c - b).count();

        // Up-scale this image's seam mask from seam-resolution to compose-
        // resolution.
        dilate(masks_warped[i], dilated_mask, Mat());
        resize(dilated_mask, seam_mask, mask_warped.size(), 0, 0, INTER_LINEAR_EXACT);
        mask_warped = seam_mask & mask_warped;

        if (!blender) {
            blender = makePtr<MultiBandBlender>(false);
            Size canvas_size = resultRoi(corners_full, sizes_full).size();
            float blend_width = std::sqrt(static_cast<float>(canvas_size.area()))
                              * opt_.blend_strength / 100.f;
            int n_bands = static_cast<int>(std::ceil(std::log(blend_width)
                                                     / std::log(2.f)) - 1.f);
            n_bands = std::max(1, std::min(n_bands, opt_.blend_bands));
            static_cast<MultiBandBlender*>(blender.get())->setNumBands(n_bands);
            blender->prepare(corners_full, sizes_full);
        }

        blender->feed(img_warped_s, mask_warped, corners_full[i]);
        auto d = Clock::now();
        ms_feed += std::chrono::duration_cast<std::chrono::milliseconds>(d - c).count();
        progress::set(progress::Stage::Compose, ((i + 1) * 100) / N);
    }

    if (opt_.verbose_timing) {
        std::cout << "[stitch] " << std::left << std::setw(22) << "compose warp (full)"
                  << ": " << ms_warp  << " ms\n";
        std::cout << "[stitch] " << std::left << std::setw(22) << "compose apply gain"
                  << ": " << ms_apply << " ms\n";
        std::cout << "[stitch] " << std::left << std::setw(22) << "blender feed"
                  << ": " << ms_feed  << " ms\n";
    }
    stage.lap();  // reset timer baseline so the next log measures only blend

    progress::set(progress::Stage::Blend, 0);
    Mat result, result_mask;
    blender->blend(result, result_mask);
    stage.log("blender blend");
    progress::set(progress::Stage::Blend, 100);

    // ── 8a. Wrap-seam tonal alignment + feather ─────────────────────────────
    //
    // Multi-band blending is acyclic: the leftmost and rightmost columns
    // came from different source frames, so a stitched 360° sweep usually
    // has a thin chroma jump at the wrap seam (sometimes a sub-pixel
    // geometric jump too). For a sweep that already closes geometrically
    // we only need a photometric fix + tiny feather. See wrapSeamBlend in
    // the anonymous namespace above for the algorithm.
    progress::set(progress::Stage::Finalize, 0);
    if (opt_.loop_closure && opt_.loop_360) {
        wrapSeamBlend(result, result_mask, opt_.verbose_timing);
        stage.log("wrap seam blend");
    }
    progress::set(progress::Stage::Finalize, 25);

    // ── 9. Optional black-margin crop ───────────────────────────────────────
    Mat result_8u;
    result.convertTo(result_8u, CV_8U);

    Mat mask_8u;
    if (result_mask.depth() != CV_8U) result_mask.convertTo(mask_8u, CV_8U);
    else                              mask_8u = result_mask;

    cv::Rect roi;
    switch (opt_.crop_mode) {
        case StitcherOptions::Crop::None:  roi = cv::Rect();                      break;
        case StitcherOptions::Crop::Bbox:  roi = detail::bboxRect(mask_8u);       break;
        case StitcherOptions::Crop::Inner: roi = detail::innerRect(mask_8u);      break;
    }

    cv::Mat pano_mask;
    if (roi.width > 0 && roi.height > 0 &&
        roi.size() != result_8u.size()) {
        std::cout << "Cropping panorama: "
                  << result_8u.cols << "x" << result_8u.rows
                  << "  →  " << roi.width << "x" << roi.height
                  << "  (offset " << roi.x << "," << roi.y << ")\n";
        pano      = result_8u(roi).clone();
        pano_mask = mask_8u(roi).clone();
    } else {
        pano      = result_8u;
        pano_mask = mask_8u;
    }
    stage.log("crop");
    progress::set(progress::Stage::Finalize, 60);

    if (opt_.fill_enabled) {
        detail::fillTopBottomBlur(pano, pano_mask,
                          opt_.fill_target_ratio,
                          opt_.fill_source_px, opt_.fill_blur_kernel,
                          opt_.fill_fade_px, opt_.fill_blur_ratio,
                          opt_.fill_fade_power,
                          opt_.fill_vertical_camera_deg);
        stage.log("fill");
    }
    progress::set(progress::Stage::Finalize, 100);

    if (opt_.verbose_timing) {
        auto total = std::chrono::duration_cast<std::chrono::milliseconds>(
                         Clock::now() - t_total_start).count();
        std::cout << "[stitch] " << std::left << std::setw(22) << "TOTAL stitch"
                  << ": " << total << " ms\n";
    }
    return true;
}

}  // namespace camera360
