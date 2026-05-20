// OpenCvMatcher — wraps OpenCV's built-in feature detectors (SIFT, ORB, AKAZE,
// BRISK) behind a small `compute(...)` interface that produces
// `cv::detail::ImageFeatures` + an N*N `cv::detail::MatchesInfo` array ready
// for `PanoramicStitcher`.
//
// Usage (string name selects the detector):
//   sift    SIFT  + BFMatcher L2 norm    + Lowe ratio 0.75
//   orb     ORB   + BFMatcher Hamming    + Lowe ratio 0.75
//   akaze   AKAZE + BFMatcher Hamming    + Lowe ratio 0.75
//   brisk   BRISK + BFMatcher Hamming    + Lowe ratio 0.75

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/stitching/detail/matchers.hpp>

namespace camera360 {

class CvMatcher {
public:
    explicit CvMatcher(const std::string& name);

    // Compute features and pairwise matches for `images` (in display order).
    // When `loop_360` is true the closing pair (last <-> first) is also
    // matched, so a 360 panorama can be composed downstream.
    void compute(const std::vector<cv::Mat>&             images,
                 std::vector<cv::detail::ImageFeatures>& features,
                 std::vector<cv::detail::MatchesInfo>&   pairwise_matches,
                 bool                                    loop_360      = true,
                 int                                     min_matches   = 8,
                 double                                  ransac_thresh = 3.0);

private:
    std::string                     name_;
    cv::Ptr<cv::Feature2D>          detector_;
    cv::Ptr<cv::DescriptorMatcher>  bf_matcher_;
};

// ── Shared homography helpers (used by both the matcher and the fallback) ──

namespace detail {

// Centered coordinates: origin moved to the image center. This matches what
// `cv::detail::BestOf2NearestMatcher::match()` does internally before calling
// `findHomography`, and `HomographyBasedEstimator` expects H in this frame.
void centeredPoints(const std::vector<cv::Point2f>& pts, int w, int h,
                    std::vector<cv::Point2f>& out);

// Run RANSAC homography on centered correspondences and populate `fwd` with
// `H`, `inliers_mask`, `num_inliers`, `confidence`. Returns false (and leaves
// `fwd.confidence == 0`) when there are too few matches or homography fails.
//
// `min_matches` and `ransac_thresh` mirror BestOf2NearestMatcher defaults.
bool computeForwardHomography(const std::vector<cv::Point2f>&  left_pts,
                              const std::vector<cv::Point2f>&  right_pts,
                              cv::Size                         left_size,
                              cv::Size                         right_size,
                              int                              min_matches,
                              double                           ransac_thresh,
                              cv::detail::MatchesInfo&         fwd);

// Build the reverse `MatchesInfo(b -> a)` from the forward `MatchesInfo(a -> b)`.
// Mirrors the fields the OpenCV BestOf2NearestMatcher would have produced for
// the symmetric pair (queryIdx/trainIdx swapped, H = forward.H.inv()).
void mirrorReversePair(const cv::detail::MatchesInfo& fwd,
                       int                            a,
                       int                            b,
                       cv::detail::MatchesInfo&       rev);

}  // namespace detail

}  // namespace camera360
