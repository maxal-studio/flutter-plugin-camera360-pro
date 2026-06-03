#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Minimal no-op exports for the iOS Simulator slice. The real OpenCV-based
// implementation lives in the device-only framework slice.

bool stitch(const char *image_paths,
            const char *model,
            const char *out,
            bool loop_360,
            bool loop_closure,
            const char *crop,
            bool fallback,
            bool fill,
            double fill_ratio,
            int fill_source,
            int fill_blur,
            int fill_fade,
            double fill_blur_ratio,
            double fill_fade_power,
            double fill_vertical_camera_deg)
{
    (void)image_paths;
    (void)model;
    (void)out;
    (void)loop_360;
    (void)loop_closure;
    (void)crop;
    (void)fallback;
    (void)fill;
    (void)fill_ratio;
    (void)fill_source;
    (void)fill_blur;
    (void)fill_fade;
    (void)fill_blur_ratio;
    (void)fill_fade_power;
    (void)fill_vertical_camera_deg;
    return false;
}

bool stitch_with_features(const char *image_paths,
                          const uint8_t *features_blob,
                          size_t features_blob_len,
                          const char *out,
                          bool loop_360,
                          bool loop_closure,
                          const char *crop,
                          bool fallback,
                          bool fill,
                          double fill_ratio,
                          int fill_source,
                          int fill_blur,
                          int fill_fade,
                          double fill_blur_ratio,
                          double fill_fade_power,
                          double fill_vertical_camera_deg)
{
    (void)image_paths;
    (void)features_blob;
    (void)features_blob_len;
    (void)out;
    (void)loop_360;
    (void)loop_closure;
    (void)crop;
    (void)fallback;
    (void)fill;
    (void)fill_ratio;
    (void)fill_source;
    (void)fill_blur;
    (void)fill_fade;
    (void)fill_blur_ratio;
    (void)fill_fade_power;
    (void)fill_vertical_camera_deg;
    return false;
}

void stitch_get_progress(int *out_percent, int *out_stage)
{
    if (out_percent != NULL) {
        *out_percent = 0;
    }
    if (out_stage != NULL) {
        *out_stage = 0;
    }
}
