// PanoramaFill — top/bottom padding fill for the panoramic stitching pipeline.
//
// `fillTopBottomBlur` places the captured panorama strip on a fixed-size
// equirect-style canvas (`W × round(W / target_ratio)`) at the row where
// the camera was actually pointing, and pads the side(s) that need more
// world content with a smoothly blurred edge profile that fades to a flat
// colour at the outermost rows.
//
// `mask` (same size as `pano`, CV_8UC1) marks valid painted pixels.
// When the panorama has black wedges (crop=none or crop=bbox) the per-column
// path traces each column's own real-content boundary as the fill seam, so
// the ramp follows the wedge outline.  With a fully-painted mask (or no mask)
// the legacy single-band fast path runs instead.
//
// Visual ramp per side (top and bottom are independent):
//   inner `blur_ratio` fraction of the fill depth   → alpha = 0   (pure edge profile)
//   outer remainder                                  → alpha = 1 - (1 - g)^fade_power
//   row = (1 - alpha) * edge_profile + alpha * flat_colour
//
// Pano-side feather (`fade_px` rows nearest the seam) blends real content
// with `edge_profile` over the seam, so raising fade_px never stretches a
// single source row.
//
// `vertical_camera_deg` describes how the phone was held during capture, on a
// 0..180 scale where 90 = camera at the horizon, < 90 = camera tilted down,
// > 90 = camera tilted up. Conceptually it shifts the captured strip up or
// down inside the equirect canvas so the world horizon lands at the canvas's
// vertical centre:
//
//   elev_cam     = clamp(vertical_camera_deg − 90, −90, +90)   // degrees
//   pix_per_deg  = H_target / 180                              // H_target = round(W / target_ratio)
//   cam_axis_row = H_target/2 − elev_cam · pix_per_deg          // row in the canvas
//
// The strip's vertical centre is positioned at `cam_axis_row`. Any rows
// that overflow the canvas (camera looked far enough off-horizon to capture
// below-nadir / above-zenith content) are cropped off the over-extended
// side; the kept rows are then padded on the opposite side to fill out to
// `H_target`.
//
// At 90° (`elev_cam = 0`) this collapses exactly to the legacy symmetric
// `pad_top = pad_bot = (H_target − H) / 2` split with no cropping, so
// horizon-level captures are byte-for-byte identical to the pre-tilt
// behaviour.

#pragma once

#include <opencv2/core.hpp>

namespace camera360 {
namespace detail {

// Pad `pano` top and/or bottom so `W / H >= target_ratio`.
// Pass an empty `mask` to use the legacy (single horizontal band) fast path.
void fillTopBottomBlur(cv::Mat&        pano,
                       const cv::Mat&  mask,                 // same size as pano, CV_8UC1
                       double          target_ratio,         // target W/H (e.g. 2.0)
                       int             source_px,            // rows sampled for the edge profile
                       int             blur_kernel,          // horizontal Gaussian size (forced odd)
                       int             fade_px,              // pano-side feather rows
                       double          blur_ratio,           // pure-blur fraction of fill depth
                       double          fade_power,           // fade curve steepness (1 = linear)
                       double          vertical_camera_deg = 90.0);  // 0..180, 90 = horizon

}  // namespace detail
}  // namespace camera360
