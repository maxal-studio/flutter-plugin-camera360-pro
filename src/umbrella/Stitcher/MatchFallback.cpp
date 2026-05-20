#include "MatchFallback.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

#include "OpenCvMatcher.hpp"  // for camera360::detail::mirrorReversePair

namespace camera360 {

namespace {

// Project a regular `G x G` grid of points inset from the borders of image
// `a` through the donor homography (which is expressed in centered image
// coordinates). Output `pts_a` and `pts_b` are in original-image pixel
// coordinates (matching the convention used by `features[i].keypoints`).
void buildSyntheticGrid(const cv::Size& size_a,
                        const cv::Size& size_b,
                        const cv::Mat&  H_donor_centered,
                        int             G,
                        std::vector<cv::Point2f>& pts_a,
                        std::vector<cv::Point2f>& pts_b)
{
    const float w_a = static_cast<float>(size_a.width);
    const float h_a = static_cast<float>(size_a.height);
    const float w_b = static_cast<float>(size_b.width);
    const float h_b = static_cast<float>(size_b.height);

    const float inset_x = w_a * 0.10f;
    const float inset_y = h_a * 0.10f;
    const float span_x  = w_a - 2.f * inset_x;
    const float span_y  = h_a - 2.f * inset_y;

    pts_a.clear();
    pts_a.reserve(G * G);

    // Sample grid in image-a coords, then convert to centered coords for the
    // homography transform.
    std::vector<cv::Point2f> centered_a;
    centered_a.reserve(G * G);
    for (int j = 0; j < G; ++j) {
        for (int i = 0; i < G; ++i) {
            const float fx = (G == 1) ? 0.5f : (static_cast<float>(i) / (G - 1));
            const float fy = (G == 1) ? 0.5f : (static_cast<float>(j) / (G - 1));
            const float x  = inset_x + fx * span_x;
            const float y  = inset_y + fy * span_y;
            pts_a.emplace_back(x, y);
            centered_a.emplace_back(x - 0.5f * w_a, y - 0.5f * h_a);
        }
    }

    std::vector<cv::Point2f> centered_b;
    cv::perspectiveTransform(centered_a, centered_b, H_donor_centered);

    pts_b.clear();
    pts_b.reserve(centered_b.size());
    for (const auto& p : centered_b) {
        pts_b.emplace_back(p.x + 0.5f * w_b, p.y + 0.5f * h_b);
    }
}

}  // namespace

std::vector<std::pair<int, int>> fillFailedAdjacentPairs(
    std::vector<cv::detail::ImageFeatures>& features,
    std::vector<cv::detail::MatchesInfo>&   pairwise_matches,
    float                                   conf_thresh,
    const FallbackOptions&                  opt)
{
    std::vector<std::pair<int, int>> synthesized;

    const int N = static_cast<int>(features.size());
    if (N < 2) return synthesized;

    auto idx = [&](int i, int j) { return static_cast<size_t>(i) * N + j; };

    // Build the canonical list of adjacent pairs in capture order.
    const int n_pairs = opt.loop_360 ? N : (N - 1);
    std::vector<std::pair<int, int>> adj;
    adj.reserve(n_pairs);
    for (int p = 0; p < n_pairs; ++p) {
        adj.emplace_back(p, (p + 1) % N);
    }

    // ── Donor selection: highest-confidence adjacent pair above the floor ──
    int    donor_pair_idx = -1;
    double best_conf      = -1.0;
    for (int p = 0; p < n_pairs; ++p) {
        const auto& mi = pairwise_matches[idx(adj[p].first, adj[p].second)];
        if (mi.H.empty()) continue;
        if (mi.confidence < opt.min_donor_confidence) continue;
        if (mi.confidence > best_conf) {
            best_conf      = mi.confidence;
            donor_pair_idx = p;
        }
    }

    if (donor_pair_idx < 0) {
        if (opt.verbose) {
            std::cout << "[fallback] no donor adjacent pair with confidence > "
                      << opt.min_donor_confidence
                      << ", skipping fallback fill\n";
        }
        return synthesized;
    }

    const int donor_a = adj[donor_pair_idx].first;
    const int donor_b = adj[donor_pair_idx].second;
    cv::Mat   H_donor = pairwise_matches[idx(donor_a, donor_b)].H.clone();

    // ── Fill each failed adjacent pair using the donor homography ──────────
    const int G = std::max(2, opt.synth_grid_per_axis);
    int n_weak_replaced = 0;

    for (int p = 0; p < n_pairs; ++p) {
        const int a = adj[p].first;
        const int b = adj[p].second;
        auto& fwd = pairwise_matches[idx(a, b)];

        const bool no_homography = fwd.H.empty();
        const bool below_thresh  = !no_homography && fwd.confidence < conf_thresh;
        // Replace noisy real pairs (the confidence formula
        // num_inliers/(8 + 0.3*matches) can score very low-inlier SIFT/ORB
        // pairs above conf_thresh even when they are too noisy for BA).
        const bool too_few_inliers = !no_homography
                                  && opt.min_real_inliers > 0
                                  && fwd.num_inliers < opt.min_real_inliers;
        const bool failed = no_homography || below_thresh || too_few_inliers;
        if (!failed) continue;
        if (too_few_inliers && !below_thresh) ++n_weak_replaced;

        std::vector<cv::Point2f> pts_a, pts_b;
        buildSyntheticGrid(features[a].img_size,
                           features[b].img_size,
                           H_donor, G, pts_a, pts_b);

        const int  M       = static_cast<int>(pts_a.size());
        const int  ka_off  = static_cast<int>(features[a].keypoints.size());
        const int  kb_off  = static_cast<int>(features[b].keypoints.size());

        features[a].keypoints.reserve(ka_off + M);
        features[b].keypoints.reserve(kb_off + M);
        for (int i = 0; i < M; ++i) {
            features[a].keypoints.emplace_back(cv::KeyPoint(pts_a[i], 1.f));
            features[b].keypoints.emplace_back(cv::KeyPoint(pts_b[i], 1.f));
        }

        fwd.src_img_idx  = a;
        fwd.dst_img_idx  = b;
        fwd.H            = H_donor.clone();
        fwd.matches.clear();
        fwd.matches.reserve(M);
        for (int i = 0; i < M; ++i) {
            cv::DMatch d;
            d.queryIdx = ka_off + i;
            d.trainIdx = kb_off + i;
            d.distance = 0.f;
            fwd.matches.push_back(d);
        }
        fwd.inliers_mask.assign(M, 1);
        fwd.num_inliers = M;
        fwd.confidence  = opt.synth_confidence;

        // Mirror to the reverse-pair slot so HomographyBasedEstimator sees
        // a symmetric graph (queryIdx/trainIdx swapped, H = H.inv()).
        detail::mirrorReversePair(fwd, a, b, pairwise_matches[idx(b, a)]);

        synthesized.emplace_back(a, b);
    }

    if (opt.verbose) {
        std::cout << "[fallback] donor pair " << donor_a << "->" << donor_b
                  << " conf=" << best_conf
                  << ", synthesized " << synthesized.size() << " pair(s)";
        if (n_weak_replaced > 0) {
            std::cout << " (" << n_weak_replaced
                      << " noisy real pair(s) with <" << opt.min_real_inliers
                      << " inliers replaced)";
        }
        if (!synthesized.empty()) {
            std::cout << ":";
            for (const auto& s : synthesized) {
                std::cout << " (" << s.first << "," << s.second << ")";
            }
        }
        std::cout << "\n";
    }

    return synthesized;
}

}  // namespace camera360
