#include "ProgressTracker.hpp"

#include <algorithm>
#include <array>

namespace camera360 {
namespace progress {

namespace {

// One weight row per stage: { lo, hi } overall-percent bounds. A stage's
// percent-within-stage scales linearly between those bounds, e.g.
//   MatchFeatures with percent=50 → overall 33 (=(8 + 58) / 2).
struct Range { int lo; int hi; };

// Number of entries must match the Stage enum cardinality so we can
// index by static_cast<int>(stage).
constexpr int kStageCount = 10;

// Feature detection + matching dominate wall-clock time on typical
// phone captures, so they claim ~58 % of the bar. Compose stages share
// the remainder.
constexpr std::array<Range, kStageCount> kWeightsWithMatching = {{
    {  0,   0},   // Idle
    {  0,   8},   // DetectFeatures
    {  8,  58},   // MatchFeatures
    { 58,  62},   // EstimateCameras
    { 62,  72},   // BundleAdjust
    { 72,  80},   // Warp
    { 80,  88},   // ExposureSeam
    { 88,  94},   // Compose
    { 94,  98},   // Blend
    { 98, 100},   // Finalize
}};

// stitch_with_features() skips DetectFeatures + MatchFeatures entirely;
// compose work fills the whole 0..100 range. The two skipped stages map
// to (0, 0) so a stray call before the table is rewritten is a no-op.
constexpr std::array<Range, kStageCount> kWeightsWithoutMatching = {{
    {  0,   0},   // Idle
    {  0,   0},   // DetectFeatures (unused)
    {  0,   0},   // MatchFeatures  (unused)
    {  0,  15},   // EstimateCameras
    { 15,  45},   // BundleAdjust
    { 45,  60},   // Warp
    { 60,  75},   // ExposureSeam
    { 75,  90},   // Compose
    { 90,  95},   // Blend
    { 95, 100},   // Finalize
}};

std::atomic<int>  g_overall_percent{0};
std::atomic<int>  g_stage_id{static_cast<int>(Stage::Idle)};
std::atomic<int>  g_mode{static_cast<int>(Mode::WithMatching)};

const std::array<Range, kStageCount>& activeTable() {
    return (g_mode.load(std::memory_order_relaxed) ==
            static_cast<int>(Mode::WithoutMatching))
        ? kWeightsWithoutMatching
        : kWeightsWithMatching;
}

}  // namespace

void reset(Mode mode) {
    g_mode.store(static_cast<int>(mode), std::memory_order_relaxed);
    g_overall_percent.store(0, std::memory_order_relaxed);
    g_stage_id.store(static_cast<int>(Stage::Idle), std::memory_order_relaxed);
}

void set(Stage stage, int percent_within_stage) {
    const int idx = static_cast<int>(stage);
    if (idx < 0 || idx >= kStageCount) return;

    const int clamped = std::clamp(percent_within_stage, 0, 100);
    const Range r = activeTable()[idx];
    const int span = r.hi - r.lo;
    int overall = (span > 0)
        ? r.lo + (clamped * span + 50) / 100
        : r.lo;
    overall = std::clamp(overall, 0, 100);

    // Monotonic: never let the overall percent regress when a new stage
    // starts at 0 % within-stage (its range lo can be below the previous
    // stage's hi). The bar only moves forward across the whole pipeline.
    const int prev = g_overall_percent.load(std::memory_order_relaxed);
    if (overall > prev) {
        g_overall_percent.store(overall, std::memory_order_relaxed);
    }
    g_stage_id.store(idx, std::memory_order_relaxed);
}

void finalize() {
    g_overall_percent.store(100, std::memory_order_relaxed);
    g_stage_id.store(static_cast<int>(Stage::Finalize),
                     std::memory_order_relaxed);
}

void snapshot(int* out_overall_percent, int* out_stage_id) {
    if (out_overall_percent) {
        *out_overall_percent =
            g_overall_percent.load(std::memory_order_relaxed);
    }
    if (out_stage_id) {
        *out_stage_id = g_stage_id.load(std::memory_order_relaxed);
    }
}

}  // namespace progress
}  // namespace camera360
