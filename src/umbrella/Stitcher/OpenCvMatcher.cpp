#include "OpenCvMatcher.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace camera360 {

namespace {

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::milliseconds;

inline long long msNow(const Clock::time_point& t0) {
    return std::chrono::duration_cast<Ms>(Clock::now() - t0).count();
}

// ── Descriptor matcher factory ────────────────────────────────────────────────
// L2-based detectors: SIFT.
// Hamming-based (binary descriptor) detectors: ORB, AKAZE (default MLDB),
// BRISK.
cv::Ptr<cv::DescriptorMatcher> makeBfMatcher(const std::string& name) {
    if (name == "sift")
        return cv::BFMatcher::create(cv::NORM_L2, false);
    return cv::BFMatcher::create(cv::NORM_HAMMING, false);
}

cv::Ptr<cv::Feature2D> makeDetector(const std::string& name) {
    if (name == "sift")  return cv::SIFT::create();
    if (name == "orb")   return cv::ORB::create(5000);
    if (name == "akaze") return cv::AKAZE::create();
    if (name == "brisk") return cv::BRISK::create();
    throw std::runtime_error("Unknown OpenCV detector: '" + name +
                             "'.  Choose sift | orb | akaze | brisk");
}

}  // namespace

// ── CvMatcher ────────────────────────────────────────────────────────────────

CvMatcher::CvMatcher(const std::string& name)
    : name_(name),
      detector_(makeDetector(name)),
      bf_matcher_(makeBfMatcher(name))
{
    std::cout << "[" << name_ << "] opencv matcher\n";
}

void CvMatcher::compute(const std::vector<cv::Mat>&             images,
                        std::vector<cv::detail::ImageFeatures>& features,
                        std::vector<cv::detail::MatchesInfo>&   pairwise_matches,
                        bool                                    loop_360,
                        int                                     min_matches,
                        double                                  ransac_thresh)
{
    const int N = static_cast<int>(images.size());
    if (N < 2)
        throw std::runtime_error("CvMatcher::compute requires >= 2 images");
    const int n_pairs = loop_360 ? N : N - 1;

    // ── Stage 1: detect + describe every image ───────────────────────────
    features.assign(N, cv::detail::ImageFeatures());
    std::vector<cv::Mat> descriptors(N);

    const auto t_extract_start = Clock::now();
    for (int i = 0; i < N; ++i) {
        const auto ti = Clock::now();

        cv::Mat gray;
        if (images[i].channels() == 1) gray = images[i];
        else cv::cvtColor(images[i], gray, cv::COLOR_BGR2GRAY);

        std::vector<cv::KeyPoint> kpts;
        detector_->detectAndCompute(gray, cv::noArray(), kpts, descriptors[i]);

        features[i].img_idx  = i;
        features[i].img_size = images[i].size();
        features[i].keypoints = std::move(kpts);

        std::cout << "[" << name_ << "] image " << std::setw(2) << i
                  << ": " << std::setw(5) << features[i].keypoints.size()
                  << " kpts  " << msNow(ti) << " ms\n";
    }
    std::cout << "[" << name_ << "] extract total: "
              << msNow(t_extract_start) << " ms\n";

    // ── Stage 2: match adjacent pairs ────────────────────────────────────
    pairwise_matches.assign(static_cast<size_t>(N) * N,
                            cv::detail::MatchesInfo());
    auto idx = [&](int i, int j) { return static_cast<size_t>(i) * N + j; };

    const auto t_match_start = Clock::now();
    for (int p = 0; p < n_pairs; ++p) {
        const int a = p;
        const int b = (p + 1) % N;

        // Skip if either image has no descriptors (can happen with AKAZE
        // on nearly-textureless images).
        if (descriptors[a].empty() || descriptors[b].empty()) {
            std::cout << "Pair " << a << "->" << b
                      << ": empty descriptors, skipping\n";
            continue;
        }

        // kNN (k=2) + Lowe ratio test.
        std::vector<std::vector<cv::DMatch>> knn;
        bf_matcher_->knnMatch(descriptors[a], descriptors[b], knn, 2);

        cv::detail::MatchesInfo& fwd = pairwise_matches[idx(a, b)];
        fwd.src_img_idx = a;
        fwd.dst_img_idx = b;

        std::vector<cv::Point2f> Lpts, Rpts;
        for (const auto& pair : knn) {
            if (pair.size() < 2) continue;
            if (pair[0].distance < 0.75f * pair[1].distance) {
                fwd.matches.push_back(pair[0]);
                Lpts.push_back(features[a].keypoints[pair[0].queryIdx].pt);
                Rpts.push_back(features[b].keypoints[pair[0].trainIdx].pt);
            }
        }

        if (static_cast<int>(fwd.matches.size()) < min_matches) {
            std::cout << "Pair " << a << "->" << b
                      << ": only " << fwd.matches.size()
                      << " matches after ratio test, skipping\n";
            continue;
        }

        const bool ok = detail::computeForwardHomography(
            Lpts, Rpts,
            features[a].img_size, features[b].img_size,
            min_matches, ransac_thresh, fwd);
        if (!ok) {
            std::cout << "Pair " << a << "->" << b
                      << ": homography failed ("
                      << fwd.matches.size() << " matches)\n";
            continue;
        }

        std::cout << "Pair " << a << "->" << b
                  << ": " << fwd.matches.size() << " matches, "
                  << fwd.num_inliers << " inliers, confidence="
                  << fwd.confidence << "\n";

        detail::mirrorReversePair(fwd, a, b, pairwise_matches[idx(b, a)]);
    }
    std::cout << "[" << name_ << "] match total: "
              << msNow(t_match_start) << " ms\n";
}

// ── Shared homography helpers ────────────────────────────────────────────────

namespace detail {

void centeredPoints(const std::vector<cv::Point2f>& pts, int w, int h,
                    std::vector<cv::Point2f>& out)
{
    out.resize(pts.size());
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    for (size_t i = 0; i < pts.size(); ++i)
        out[i] = cv::Point2f(pts[i].x - cx, pts[i].y - cy);
}

bool computeForwardHomography(const std::vector<cv::Point2f>&  left_pts,
                              const std::vector<cv::Point2f>&  right_pts,
                              cv::Size                         left_size,
                              cv::Size                         right_size,
                              int                              min_matches,
                              double                           ransac_thresh,
                              cv::detail::MatchesInfo&         fwd)
{
    if (static_cast<int>(left_pts.size()) < min_matches) return false;

    std::vector<cv::Point2f> src, dst;
    centeredPoints(left_pts,  left_size.width,  left_size.height,  src);
    centeredPoints(right_pts, right_size.width, right_size.height, dst);

    cv::Mat inlier_mask;
    cv::Mat H = cv::findHomography(src, dst, cv::RANSAC, ransac_thresh, inlier_mask);
    if (H.empty()) return false;

    fwd.H            = H;
    fwd.inliers_mask = std::vector<uchar>(inlier_mask.begin<uchar>(),
                                          inlier_mask.end<uchar>());
    fwd.num_inliers  = static_cast<int>(std::count(fwd.inliers_mask.begin(),
                                                   fwd.inliers_mask.end(), uchar(1)));
    fwd.confidence   = fwd.num_inliers / (8.0 + 0.3 * fwd.matches.size());
    // NOTE: OpenCV's BestOf2NearestMatcher zeroes confidence when it exceeds
    // 3.0 to filter "duplicate view" pairs whose raw 2NN matches saturate the
    // inlier count. We deliberately skip that guard: LightGlue already
    // pre-filters its matches, so near-perfect inlier rates are normal and
    // tripping the guard would zero legitimate adjacent pairs (especially with
    // strong descriptors like ALIKED, where every pair lands at ~3.0–3.2).
    return true;
}

void mirrorReversePair(const cv::detail::MatchesInfo& fwd,
                       int                            a,
                       int                            b,
                       cv::detail::MatchesInfo&       rev)
{
    rev.src_img_idx = b;
    rev.dst_img_idx = a;
    rev.matches.clear();
    rev.matches.reserve(fwd.matches.size());
    for (const auto& d : fwd.matches) {
        cv::DMatch r = d;
        std::swap(r.queryIdx, r.trainIdx);
        rev.matches.push_back(r);
    }
    rev.inliers_mask = fwd.inliers_mask;
    rev.num_inliers  = fwd.num_inliers;
    rev.confidence   = fwd.confidence;
    rev.H            = fwd.H.empty() ? cv::Mat() : fwd.H.inv();
    (void)a; // Silence unused-arg warning in case of future logging.
}

}  // namespace detail
}  // namespace camera360
