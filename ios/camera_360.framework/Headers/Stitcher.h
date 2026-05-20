#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Compose a 360 panorama from `image_paths` using one of OpenCV's built-in
    // feature detectors (`model`), and write the result to `out`. Returns true
    // on success.
    //
    // Parameters mirror the CLI args of the custom_cpp reference runner
    // (custom_cpp/src/main.cpp:25-42) minus `dataset` and `infer_size`:
    //
    //   image_paths      "[a.jpg, b.jpg, c.jpg]" or comma/newline separated
    //                    list of full file paths. Order = display order.
    //   model            "sift" | "orb" | "akaze" | "brisk".
    //                    NULL or empty string defaults to "sift".
    //   out              Output panorama file path (.jpg / .png).
    //   loop_360         When true, also match the closing pair (last <-> first)
    //                    so a full 360 panorama can be composed.
    //   crop             "none" | "bbox" | "inner".
    //                    Controls how the painted area is cropped from the
    //                    blended canvas. Independent of `fill`.
    //   fallback         If true, synthesize failed adjacent pairs from the
    //                    highest-confidence donor pair before stitching.
    //   fill             If true, run the per-column blur->flat fill on the
    //                    post-crop panorama. The ramp's join row is the actual
    //                    content boundary in each column, so the fill works
    //                    even when `crop` leaves black wedges at the top or
    //                    bottom (e.g. crop="none" / "bbox").
    //   fill_ratio       Target W/H aspect when `fill` is true (e.g. 2.0).
    //   fill_source      Rows sampled from each edge as the blur source.
    //   fill_blur        Horizontal-only Gaussian kernel applied to the edge
    //                    profile (forced odd, >= 3).
    //   fill_fade        Number of panorama rows overwritten by the
    //                    edge_profile -> real_pano blend at the join.
    //   fill_blur_ratio  Fraction of the fill band that stays as pure blur
    //                    before fading to the flat colour (in [0, 1]).
    //   fill_fade_power  Stacked flat-colour layers in the fade (>= 1.0).
    //   fill_vertical_camera_deg
    //                    Phone vertical pose during capture, on the panorama
    //                    UI's 0..180 scale (90 = camera at the horizon,
    //                    < 90 = tilted down, > 90 = tilted up). Controls how
    //                    the total top/bottom fill budget is split: 90 gives
    //                    an even split (legacy behaviour), 75 puts more
    //                    padding on top to recover the sky a downward-tilted
    //                    capture is missing, and so on.
    bool stitch(const char *image_paths,
                const char *model,
                const char *out,
                bool        loop_360,
                const char *crop,
                bool        fallback,
                bool        fill,
                double      fill_ratio,
                int         fill_source,
                int         fill_blur,
                int         fill_fade,
                double      fill_blur_ratio,
                double      fill_fade_power,
                double      fill_vertical_camera_deg);

    // Same as `stitch`, but uses externally-computed keypoints + matches
    // instead of running an OpenCV detector. Used by the Dart-side SuperPoint
    // / LightGlue ONNX pipeline, which produces correspondences before the
    // panorama is composed.
    //
    // `features_blob` (little-endian, see ExternalFeatures.hpp for the layout)
    // carries per-image keypoint coordinates in the ORIGINAL image pixel
    // frame, plus per-pair `(queryIdx, trainIdx)` match index arrays. The
    // number of images encoded in the blob must match the number of paths in
    // `image_paths`.
    //
    // All other parameters behave identically to `stitch` (image paths,
    // output path, crop / fill / fallback / loop_360, etc.). No `model`
    // argument is needed because matching is already done.
    bool stitch_with_features(const char    *image_paths,
                              const uint8_t *features_blob,
                              size_t         features_blob_len,
                              const char    *out,
                              bool           loop_360,
                              const char    *crop,
                              bool           fallback,
                              bool           fill,
                              double         fill_ratio,
                              int            fill_source,
                              int            fill_blur,
                              int            fill_fade,
                              double         fill_blur_ratio,
                              double         fill_fade_power,
                              double         fill_vertical_camera_deg);

#ifdef __cplusplus
} // closing brace for extern "C"
#endif
