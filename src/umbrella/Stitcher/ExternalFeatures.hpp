// ExternalFeatures — turn an externally-computed keypoint + match blob into
// `cv::detail::ImageFeatures` + `cv::detail::MatchesInfo` arrays that look
// identical (to `HomographyBasedEstimator` and `BundleAdjusterRay`) to the
// ones produced by `CvMatcher::compute()`.
//
// Used by the FFI entry point `stitch_with_features()` so the Dart-side
// SuperPoint / LightGlue ONNX pipeline can drive the same downstream
// stitching code path (fallback synthesis, bundle adjustment, blending) as
// the built-in OpenCV detectors.
//
// Blob layout (all integers little-endian):
//
//   header   : i32 magic = 0x4346504E ("CFPN"),
//              i32 version = 1,
//              i32 num_images N,
//              i32 num_pairs  K
//
//   img_tbl  : N × { i32 width, i32 height, i32 num_kpts, i32 kpts_byte_offset }
//   pair_tbl : K × { i32 src_idx, i32 dst_idx, i32 num_matches,
//                    i32 matches_byte_offset }
//
//   kpts     : per image (in `img_tbl` order):
//                float32 (x, y) × num_kpts
//              keypoint coordinates are in ORIGINAL image pixel space (i.e.
//              the same coordinate frame `cv::imread` returns).
//
//   matches  : per pair (in `pair_tbl` order):
//                int32 (queryIdx, trainIdx) × num_matches
//
// The per-image keypoints from each pair the image participates in are
// expected to be concatenated by the producer, with `queryIdx` / `trainIdx`
// rewritten by the corresponding offset. The C++ side does not de-duplicate
// — duplicate keypoints inside one image's array are harmless for RANSAC
// and bundle adjustment.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/stitching/detail/matchers.hpp>

namespace camera360 {

// Parse `feat_blob` and populate `features` (size N) + `pairwise_matches`
// (size N*N, row-major `i*N + j`). For each pair in the blob, computes the
// forward homography via RANSAC (using the same `detail::computeForwardHomography`
// helper `CvMatcher::compute()` uses) and mirrors the reverse pair.
//
// `images.size()` must equal the blob's N; per-image `width`/`height` in the
// blob must match the decoded `cv::Mat::size()`.
//
// Returns false (with a logged reason) if validation fails. Pairs whose RANSAC
// fails are left at zero-confidence — same behaviour as `CvMatcher::compute()`
// — so `MatchFallback` can fill them in.
bool buildFromExternalBlob(const uint8_t*                          feat_blob,
                           std::size_t                             feat_len,
                           const std::vector<cv::Mat>&             images,
                           std::vector<cv::detail::ImageFeatures>& features,
                           std::vector<cv::detail::MatchesInfo>&   pairwise_matches);

}  // namespace camera360
