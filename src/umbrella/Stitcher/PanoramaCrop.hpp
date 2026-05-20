// PanoramaCrop — crop helpers for the panoramic stitching pipeline.
//
// Provides bboxRect / innerRect to compute axis-aligned ROIs from a blended
// panorama mask. Both functions operate in O(W*H) and are well-suited to the
// horizontal panoramas produced by CylindricalWarper.
//
//   bboxRect  — tight bounding box of every non-zero pixel (can still
//               leave thin black wedges at top/bottom).
//   innerRect — largest axis-aligned inner rectangle that contains *no*
//               black pixel at all; preferred for 360° viewers (Pannellum etc.)
//               because the output is strictly rectangular with no black fringe.

#pragma once

#include <opencv2/core.hpp>

namespace camera360 {
namespace detail {

// Tight bounding box of every non-zero pixel in `mask`. Returns an empty
// cv::Rect when the mask is fully black.
cv::Rect bboxRect(const cv::Mat& mask);

// Largest axis-aligned inner rectangle that contains no black pixel.
// Strategy: take the bounding box first, then per column shrink the
// top/bottom limits to max-of-tops / min-of-bottoms of valid runs.
// Fast (O(W*H)) and very effective for cylindrical panoramas that only
// have black wedges along the top/bottom edges.
cv::Rect innerRect(const cv::Mat& mask);

}  // namespace detail
}  // namespace camera360
