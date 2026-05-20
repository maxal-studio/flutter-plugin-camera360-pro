#include "PanoramicStitcher.hpp"
#include "PanoramaCrop.hpp"
#include "PanoramaFill.hpp"

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

    // ── 3. Bundle adjustment (ray cost) ──────────────────────────────────────
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
    }
    stage.log("warp (seam scale)");

    // ── 6. Exposure compensation (block-based gain) at SEAM scale ───────────
    Ptr<ExposureCompensator> compensator =
        ExposureCompensator::createDefault(ExposureCompensator::GAIN_BLOCKS);
    compensator->feed(corners_seam, images_warped, masks_warped);
    stage.log("exposure feed");

    // ── 7. Seam finding at SEAM scale ───────────────────────────────────────
    Ptr<SeamFinder> seam_finder;
    if (opt_.seam_mode == StitcherOptions::Seam::Voronoi) {
        seam_finder = makePtr<VoronoiSeamFinder>();
    } else {
        seam_finder = makePtr<DpSeamFinder>(DpSeamFinder::COLOR);
    }
    seam_finder->find(images_warped_f, corners_seam, masks_warped);
    stage.log("seam find");

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
        // works unchanged. The `corner` argument is ignored by the compensator.
        compensator->apply(i, corners_full[i], img_warped, mask_warped);
        img_warped.convertTo(img_warped_s, CV_16S);
        auto c = Clock::now();
        ms_apply += std::chrono::duration_cast<std::chrono::milliseconds>(c - b).count();

        // Up-scale the seam mask from seam-resolution to compose-resolution.
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

    Mat result, result_mask;
    blender->blend(result, result_mask);
    stage.log("blender blend");

    // ── 8b. Wrap-seam gap repair ─────────────────────────────────────────────
    //
    // For a 360° loop panorama the cylindrical canvas wraps so that column 0
    // and column W-1 should be visually identical. Small bundle-adjustment
    // drift between the first and last image can leave a thin strip of
    // background (zero) columns at either or both horizontal edges of the
    // blended result. A 360° viewer renders this as a vertical black line at
    // the wrap seam.
    //
    // We repair it by scanning inwards from each edge for all-zero columns in
    // result_mask (blender output uses CV_16SC3 for result, CV_8U for
    // result_mask). For each such column we copy the "mirror" column from the
    // opposite edge so the seam is filled with real content. The search is
    // capped at a small fraction of the image width so we never accidentally
    // overwrite legitimate sky/ground content.
    if (opt_.loop_360) {
        const int RW = result_mask.cols;
        const int RH = result_mask.rows;
        const int max_gap = std::max(1, RW / 200);   // cap at ~0.5% of width

        // Collect empty columns from the left edge.
        std::vector<int> left_empty, right_empty;
        for (int x = 0; x < max_gap; ++x) {
            if (cv::countNonZero(result_mask.col(x)) == 0)
                left_empty.push_back(x);
            else break;   // stop at the first non-empty column
        }
        for (int x = RW - 1; x >= RW - max_gap; --x) {
            if (cv::countNonZero(result_mask.col(x)) == 0)
                right_empty.push_back(x);
            else break;
        }

        // Mirror each empty left column from the corresponding right column
        // and vice versa. Use the column at distance d+1 from the opposite
        // edge (the first filled column on that side) as the donor.
        for (int i = 0; i < static_cast<int>(left_empty.size()); ++i) {
            const int donor = RW - 1 - i;
            if (donor >= 0 && donor < RW &&
                cv::countNonZero(result_mask.col(donor)) > 0) {
                result.col(donor).copyTo(result.col(left_empty[i]));
                result_mask.col(donor).copyTo(result_mask.col(left_empty[i]));
            }
        }
        for (int i = 0; i < static_cast<int>(right_empty.size()); ++i) {
            const int donor = i;
            if (donor >= 0 && donor < RW &&
                cv::countNonZero(result_mask.col(donor)) > 0) {
                result.col(donor).copyTo(result.col(right_empty[i]));
                result_mask.col(donor).copyTo(result_mask.col(right_empty[i]));
            }
        }

        if (!left_empty.empty() || !right_empty.empty()) {
            std::cout << "Wrap-seam gap repair: filled "
                      << left_empty.size() << " left + "
                      << right_empty.size() << " right empty column(s)\n";
        }
    }

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

    if (opt_.fill_enabled) {
        detail::fillTopBottomBlur(pano, pano_mask,
                          opt_.fill_target_ratio,
                          opt_.fill_source_px, opt_.fill_blur_kernel,
                          opt_.fill_fade_px, opt_.fill_blur_ratio,
                          opt_.fill_fade_power,
                          opt_.fill_vertical_camera_deg);
        stage.log("fill");
    }

    if (opt_.verbose_timing) {
        auto total = std::chrono::duration_cast<std::chrono::milliseconds>(
                         Clock::now() - t_total_start).count();
        std::cout << "[stitch] " << std::left << std::setw(22) << "TOTAL stitch"
                  << ": " << total << " ms\n";
    }
    return true;
}

}  // namespace camera360
