// PanoramicStitcher — a custom stitcher modeled after OpenCV's
// `samples/cpp/stitching_detailed.cpp` pipeline, accepting externally
// supplied features and matches (from OpenCvMatcher or any other source).
//
// Pipeline (see `composePanorama`):
//   1. leaveBiggestComponent             — keep the connected subset
//   2. HomographyBasedEstimator          — initial cameras
//   3. BundleAdjusterRay                  — refine cameras
//   4. waveCorrect                        — straighten horizon
//   5. CylindricalWarper                  — warp images + masks
//   6. ExposureCompensator (GAIN_BLOCKS)  — equalise exposure
//   7. SeamFinder (DP / Voronoi)          — pick clean cuts
//   8. MultiBandBlender                   — feathered multi-band blend

#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/stitching/detail/matchers.hpp>

namespace camera360 {

struct StitcherOptions {
    double conf_thresh   = 0.4;   // leaveBiggestComponent / BA threshold
    int    blend_bands   = 5;     // MultiBandBlender pyramid levels
    float  blend_strength = 5.f;  // 0..100, controls blend_width
    enum class Seam { Voronoi, Dp };
    Seam   seam_mode = Seam::Dp;
    bool   wave_correct = true;

    // When true, the closing pair (last ↔ first image) was matched during
    // feature extraction. The stitcher uses this to know whether to repair
    // any thin strip of background-coloured columns left at the horizontal
    // wrap seam after blending — a post-blend step that eliminates the
    // vertical black line some 360° viewers render at the left/right boundary.
    bool loop_360 = true;

    // Resolution at which exposure compensation and seam finding run. The
    // expensive DP seam finder is roughly O(W*H) per overlap, so running it on
    // full-res cylindrical warps is dramatically slower than necessary.
    // Matches OpenCV's `Stitcher::PANORAMA` default (≈0.1 MP). Set to <= 0 to
    // disable down-sampling (run seams at the registration resolution).
    double seam_megapix = 0.1;

    // Print per-stage timings (component, BA, warp, exposure, seam, compose,
    // blend, crop) so we can see exactly where time is spent.
    bool   verbose_timing = true;

    // Auto-crop the panorama to remove black margins. Two policies are
    // supported: `Bbox` keeps every painted pixel (returns the tight
    // bounding box of the result mask, which can still leave thin
    // wedges of black at the top/bottom), while `Inner` returns the
    // largest top/bottom-aligned inner rectangle that contains *no*
    // black at all (preferred for 360° viewers like Pannellum).
    enum class Crop { None, Bbox, Inner };
    Crop crop_mode = Crop::Inner;

    // Post-crop vertical fill: pad the top and bottom of the panorama so the
    // final image reaches `fill_target_ratio` (W/H). Skipped when the
    // panorama already meets or exceeds the target ratio.
    //
    // Algorithm per side (top and bottom independent):
    //   1. Sample `fill_source_px` rows of the panorama edge.
    //   2. Vertical-mean them to a 1-row profile, then heavy horizontal
    //      Gaussian (`fill_blur_kernel` × 1) → smooth horizontal colour
    //      profile with no per-column detail. Raise `fill_blur_kernel` to
    //      make the blur look stronger (more uniform colour, more haze).
    //   3. Compose the fill band with a piecewise interpolation:
    //        - inner `fill_blur_ratio` fraction: pure edge profile (blur zone)
    //        - outer remainder: fades from the edge profile to the band's
    //          flat average colour at the outermost row.
    //      The fade alpha is shaped as 1 - (1 - g)^fill_fade_power, which is
    //      equivalent to stacking `fill_fade_power` independent flat-colour
    //      layers on top of the linear blend (using the "over" operator) and
    //      then collapsing them analytically into a single cv::addWeighted.
    //      Endpoints (alpha=0 at the join, alpha=1 at the outer edge) are
    //      untouched, so the join and the outer flat region match the simple
    //      linear case byte-for-byte; only the middle of the fade is pulled
    //      toward the flat colour more aggressively.
    //   4. Pano-side feather: overwrite the outermost `fill_fade_px` rows of
    //      the panorama with a per-row blend from edge_profile (at the join)
    //      to the original detail (`fill_fade_px` rows deep). Each pano row
    //      keeps its own real content for the blend, so raising fill_fade_px
    //      spreads the transition over more pano rows without stretching.
    bool   fill_enabled      = false;
    double fill_target_ratio = 2.0;   // 2:1 equirectangular
    int    fill_source_px    = 30;    // rows of real panorama sampled for the profile
    int    fill_blur_kernel  = 301;   // horizontal-only Gaussian kernel (forced odd).
                                      // Larger = stronger / hazier blur.
    int    fill_fade_px      = 20;    // rows of panorama overwritten with the
                                      // edge_profile → real_pano blend
    double fill_blur_ratio   = 0.1;   // fraction of the fill band that stays as
                                      // pure edge profile before fading to the
                                      // flat colour (clamped to [0, 1]).
    double fill_fade_power   = 2.0;   // number of stacked flat-colour layers
                                      // applied to the fade. 1.0 = pure linear
                                      // (the original behaviour); 2.0 = one
                                      // extra layer, pulls the middle of the
                                      // fade toward the flat colour; higher
                                      // values are even more aggressive.
                                      // Clamped to >= 1.0.

    // Phone vertical pose during capture, on the same 0..180 scale used by the
    // Flutter capture UI (90 = camera at the horizon, < 90 = camera tilted
    // down, > 90 = camera tilted up). Controls how the total fill budget is
    // split between top and bottom — see PanoramaFill.hpp. The 90° default
    // produces the symmetric (top = bottom) split the legacy code always used.
    double fill_vertical_camera_deg = 90.0;
};

class PanoramicStitcher {
public:
    explicit PanoramicStitcher(const StitcherOptions& opt = {});

    // Compose a panorama from full-resolution `images` + the per-image
    // `features` and N×N `pairwise_matches` produced by the OpenCV matcher.
    // On success, writes BGR `pano` (CV_8UC3) and returns true.
    bool stitch(const std::vector<cv::Mat>&                 images,
                std::vector<cv::detail::ImageFeatures>      features,
                std::vector<cv::detail::MatchesInfo>        pairwise_matches,
                cv::Mat&                                     pano);

private:
    StitcherOptions opt_;
};

}  // namespace camera360
