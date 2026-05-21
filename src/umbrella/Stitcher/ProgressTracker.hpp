// ProgressTracker — process-wide live progress reporter for the umbrella
// stitcher. The Flutter side polls the two atomic counters through the
// `stitch_get_progress` FFI on a Timer while the worker isolate executes
// the synchronous `stitch` / `stitch_with_features` FFI in another thread.
//
// One stitch at a time. The atomics live in the dylib's process-wide data
// segment, so every isolate that opens the library sees the same values.
//
// Usage:
//   1. Entry point (Stitcher.cpp) calls reset(Mode) at start and
//      finalize() at end (success OR failure) so the poller stops at 100 %.
//   2. Pipeline stages call set(stage, percent_within_stage). The tracker
//      maps that to an overall 0..100 % via the active weight table.

#pragma once

#include <atomic>
#include <cstdint>

namespace camera360 {
namespace progress {

// Numeric stage IDs are part of the FFI ABI — the Dart side maps these
// to human-readable labels. Keep the order/values stable; append new
// stages at the end.
enum class Stage : int {
    Idle             = 0,
    DetectFeatures   = 1,
    MatchFeatures    = 2,
    EstimateCameras  = 3,
    BundleAdjust     = 4,
    Warp             = 5,
    ExposureSeam     = 6,
    Compose          = 7,
    Blend            = 8,
    Finalize         = 9,
};

// Which weight table to use. Picks which slice of the 0..100 range each
// stage occupies.
//   WithMatching      — full pipeline, used by `stitch()` (OpenCV path).
//                       Feature detection + matching consume 0..40 %.
//   WithoutMatching   — compose only, used by `stitch_with_features()`.
//                       Matching already happened in Dart (ONNX path);
//                       native stages span 0..100 % of compose work.
enum class Mode : int {
    WithMatching    = 0,
    WithoutMatching = 1,
};

// Reset the tracker for a new stitch. Picks the weight table for the
// caller and zeroes both atomics.
void reset(Mode mode);

// Set the active stage and percent-within-stage (0..100). The tracker
// maps that to an overall percent using the active weight table.
// Monotonic: never reports a smaller overall percent than the previous
// call (so the UI bar can only grow).
void set(Stage stage, int percent_within_stage);

// Force the tracker to (Finalize, 100 %). Call once at the end of every
// stitch entry point, on both the success and failure paths, so the
// Dart-side poller sees a terminal value.
void finalize();

// Snapshot the two atomics. Safe to call from any thread.
void snapshot(int* out_overall_percent, int* out_stage_id);

}  // namespace progress
}  // namespace camera360
