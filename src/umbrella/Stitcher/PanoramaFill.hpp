// PanoramaFill — cylindrical → equirect remap + top/bottom padding fill for
// the panoramic stitching pipeline.
//
// Input is the cylindrical strip produced by `PanoramicStitcher` (a
// `CylindricalWarper` output, possibly post-cropped by Bbox/Inner). The
// strip's vertical pixel coordinate `v` (where `v = 0` is the world horizon
// after wave correction) maps to elevation `φ` by
//
//     v = f · tan(−φ)        ⇔     φ = −atan(v / f)
//
// where `f` is the cylindrical focal length (= `warped_image_scale` from
// the stitcher) in pixels, and the sign matches OpenCV's image-y-down
// convention (`v > 0` is below the horizon, `v < 0` is above).
//
// Output is an equirect canvas of size `W × round(W / target_ratio)` where
// the vertical pixel coordinate maps to elevation linearly:
//
//     R_canvas = H_target/2 − φ · pix_per_deg     (pix_per_deg = H_target/180)
//
// For each output row R we compute the target elevation, invert through
// the cylindrical mapping to find the fractional source pano row, and
// bilinear-interpolate vertically into the canvas. Output rows that fall
// outside the captured strip (above the top join or below the bottom join)
// are filled with the existing edge-profile + flat-colour ramp.
//
// Caller contract:
//   • `cyl_focal > 0` and `cyl_v_top_pano` correctly identify the strip's
//     cylindrical coordinate system → proper remap path runs. This requires
//     wave correction to have aligned the cylinder axis with world up
//     (the default in `PanoramicStitcher`).
//   • `cyl_focal <= 0` → legacy "paste 1:1, biased by `vertical_camera_deg`"
//     placement is used (only approximately correct near the horizon, and
//     only with `target_ratio = 2.0` + full 360° sweep).
//
// `vertical_camera_deg` is kept on the API for the legacy path and for
// downstream logging; it has no effect on the remap path (the strip's true
// horizon is recovered from `cyl_focal` + `cyl_v_top_pano`).
//
// `mask` (same size as `pano`, CV_8UC1) marks valid painted pixels. Black
// wedges along the top/bottom (crop=none or crop=bbox) are followed
// per-column so the fill ramp traces each column's actual content boundary.
//
// Visual ramp per side (top and bottom are independent):
//   inner `blur_ratio` fraction of the fill depth   → alpha = 0   (pure edge profile)
//   outer remainder                                  → alpha = 1 - (1 - g)^fade_power
//   row = (1 - alpha) * edge_profile + alpha * flat_colour
//
// The flat colour at the outer edge of each fill band is either derived from
// the mean of the sampled edge strip (`flat_use_edge_color = true`, the
// legacy default) or a caller-supplied BGR triple (`flat_use_edge_color =
// false`, useful for neutral gray padding before depth-map conversion).
//
// Pano-side feather (`fade_px` rows nearest the seam) blends real sampled
// content with `edge_profile` over the seam.

#pragma once

#include <opencv2/core.hpp>

namespace camera360 {
namespace detail {

// Remap a cylindrical `pano` onto an equirect canvas and pad the rows that
// fall outside the captured strip.
//
// When `cyl_focal > 0`, the strip's geometry is fully known and the
// remap is exact; `vertical_camera_deg` is then informational only.
// When `cyl_focal <= 0`, the legacy `vertical_camera_deg`-biased placement
// is used.
void fillTopBottomBlur(cv::Mat&        pano,
                       const cv::Mat&  mask,                 // same size as pano, CV_8UC1
                       double          target_ratio,         // target W/H (e.g. 2.0)
                       int             source_px,            // rows sampled for the edge profile
                       int             blur_kernel,          // horizontal Gaussian size (forced odd)
                       int             fade_px,              // pano-side feather rows
                       double          blur_ratio,           // pure-blur fraction of fill depth
                       double          fade_power,           // fade curve steepness (1 = linear)
                       double          vertical_camera_deg = 90.0,  // 0..180, 90 = horizon
                       double          cyl_focal           = 0.0,   // <= 0 = legacy placement
                       double          cyl_v_top_pano      = 0.0,   // cyl-y of pano row 0 (when cyl_focal>0)
                       bool            flat_use_edge_color = true,  // false → use flat_b/g/r
                       int             flat_b              = 128,   // OpenCV BGR when !flat_use_edge_color
                       int             flat_g              = 128,
                       int             flat_r              = 128);

}  // namespace detail
}  // namespace camera360
