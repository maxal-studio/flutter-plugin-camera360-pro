// MatchFallback — post-processing pass that fills in adjacent pairs whose
// real feature match failed by reusing the homography of the highest-
// confidence adjacent pair as a "donor".
//
// Use case: a controller captures images every X degrees, so any confident
// adjacent pair's homography is a valid geometric stand-in for any failed
// adjacent pair (same intrinsics, same image size, same centered coordinate
// frame). This keeps blank/untextured frames in the panorama instead of
// being dropped by `leaveBiggestComponent`.
//
// The fill is matcher-agnostic and runs between `CvMatcher::compute()`
// and `PanoramicStitcher::stitch()`.

#pragma once

#include <utility>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/stitching/detail/matchers.hpp>

namespace camera360 {

struct FallbackOptions {
    // Donor pair must have at least this confidence.
    float min_donor_confidence = 0.8f;

    // Confidence written on synthesized pairs. Must exceed
    // `PanoramicStitcher`'s `conf_thresh` so that the synthesized pair is
    // both kept by `leaveBiggestComponent` and used by the bundle adjuster.
    float synth_confidence = 1.0f;

    // Number of synthetic correspondences per axis (total = grid * grid).
    // 5x5 = 25 points is enough to give the bundle adjuster a stable signal
    // without bloating the keypoint arrays.
    int synth_grid_per_axis = 5;

    // Replace any real pair whose RANSAC inlier count is below this with the
    // donor homography. The confidence formula `inliers/(8 + 0.3*matches)`
    // can score noisy 6–13-inlier pairs well above `conf_thresh` (especially
    // for sparse detectors like SIFT/ORB), and chaining several such pairs
    // gives `BundleAdjusterRay` an unrecoverable initial rotation. The
    // donor pair represents the controller's true angular step and is a
    // safer substitute. Set to 0 to disable.
    int min_real_inliers = 15;

    // Match the closing pair (N-1, 0) too. Must match the value passed to
    // `CvMatcher::compute(loop_360=…)`.
    bool loop_360 = true;

    bool verbose = true;
};

// Inspect `pairwise_matches` for adjacent pairs (i, (i+1) % N) whose
// confidence is below `conf_thresh` or whose homography is empty, and fill
// them in using the donor's homography. Synthetic keypoints are appended to
// `features[a].keypoints` and `features[b].keypoints` and matched 1:1.
//
// Returns the list of (a, b) pairs that were synthesized (for logging).
std::vector<std::pair<int, int>> fillFailedAdjacentPairs(
    std::vector<cv::detail::ImageFeatures>& features,
    std::vector<cv::detail::MatchesInfo>&   pairwise_matches,
    float                                   conf_thresh,
    const FallbackOptions&                  opt = {});

}  // namespace camera360
