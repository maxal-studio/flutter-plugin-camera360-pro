import 'dart:async';
import 'dart:ffi';
import 'dart:io';

import 'package:camera/camera.dart';
import 'package:camera_360/camera_360_bindings_generated.dart';
import 'package:ffi/ffi.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

/// Signature for the stitching progress callback delivered by
/// [Stitcher.stitchImages].
///
/// `stage` is a human-readable English label (`"Detecting features"`,
/// `"Bundle adjustment"`, …). `percent` is an integer in 0..100 that
/// represents the overall progress across the entire stitch pipeline,
/// not just the current stage.
typedef StitchProgressCallback = void Function(String stage, int percent);

/// Numeric stage ids returned by the native `stitch_get_progress` FFI.
/// Must stay in sync with `progress::Stage` in
/// `src/umbrella/Stitcher/ProgressTracker.hpp`.
const int _stageIdle             = 0;
const int _stageDetectFeatures   = 1;
const int _stageMatchFeatures    = 2;
const int _stageEstimateCameras  = 3;
const int _stageBundleAdjust     = 4;
const int _stageWarp             = 5;
const int _stageExposureSeam     = 6;
const int _stageCompose          = 7;
const int _stageBlend            = 8;
const int _stageFinalize         = 9;

String _stageLabel(int id) {
  switch (id) {
    case _stageDetectFeatures:  return 'Detecting features';
    case _stageMatchFeatures:   return 'Matching features';
    case _stageEstimateCameras: return 'Estimating cameras';
    case _stageBundleAdjust:    return 'Bundle adjustment';
    case _stageWarp:            return 'Warping';
    case _stageExposureSeam:    return 'Exposure & seam';
    case _stageCompose:         return 'Composing';
    case _stageBlend:           return 'Blending';
    case _stageFinalize:        return 'Finalising';
    case _stageIdle:
    default:                    return 'Preparing';
  }
}

/// Main-isolate handle to the camera_360 dylib. Opened lazily the first
/// time progress is polled and cached after that so the
/// `Timer.periodic(50ms)` loop doesn't re-resolve symbols every tick.
Camera360Bindings? _mainIsoBindings;
Camera360Bindings _mainIsolateBindings() {
  if (_mainIsoBindings != null) return _mainIsoBindings!;
  final dylib = Platform.isAndroid
      ? DynamicLibrary.open('libcamera_360.so')
      : DynamicLibrary.process();
  return _mainIsoBindings = Camera360Bindings(dylib);
}

/// Feature pipeline passed to the native stitcher.
///
/// `sift` / `orb` / `akaze` / `brisk` map 1:1 to the C++ `model` argument and
/// drive the legacy `stitch()` FFI symbol. `onnx` selects the Dart-side
/// SuperPoint + LightGlue ONNX pipeline; matches are computed in Dart and
/// shipped over a separate FFI symbol (`stitch_with_features`).
enum StitchFeatureModel {
  sift,
  orb,
  akaze,
  brisk,
  onnx,
}

extension StitchFeatureModelX on StitchFeatureModel {
  /// Lowercase keyword expected by the native `stitch` function. Unused for
  /// the ONNX path (which calls a different FFI symbol).
  String get nativeName => name;
}

/// Panorama crop policy passed to the native stitcher.
///
/// Maps 1:1 to the C++ `crop` argument (`none` | `bbox` | `inner`).
/// Vertical fill is controlled separately via [Stitcher.stitchImages] `fill`.
enum StitchCropMode {
  /// Keep the full canvas (black borders intact).
  none,

  /// Crop to the bounding box of all painted pixels.
  bbox,

  /// Crop to the largest fully-painted inner rectangle (no black wedges).
  inner,
}

extension StitchCropModeX on StitchCropMode {
  /// Lowercase keyword expected by the native `stitch` function.
  String get nativeName => name;
}

/// Parameters forwarded into the stitching isolate.
///
/// All fields are primitive types so the object is safely sendable across
/// isolate boundaries.
class _StitchParams {
  const _StitchParams({
    required this.imagePaths,
    required this.model,
    required this.outPath,
    required this.loop360,
    required this.loopClosure,
    required this.cropMode,
    required this.fallback,
    required this.fill,
    required this.fillRatio,
    required this.fillSource,
    required this.fillBlur,
    required this.fillFade,
    required this.fillBlurRatio,
    required this.fillFadePower,
    required this.fillVerticalCameraDeg,
    this.featuresBlob,
  });

  final List<String> imagePaths;
  final String model;
  final String outPath;
  final bool loop360;
  final bool loopClosure;
  final String cropMode;
  final bool fallback;
  final bool fill;
  final double fillRatio;
  final int fillSource;
  final int fillBlur;
  final int fillFade;
  final double fillBlurRatio;
  final double fillFadePower;
  // Phone vertical pose at capture time, on the 0..180 scale used by the
  // panorama UI (90 = camera at the horizon, < 90 = tilted down, > 90 =
  // tilted up). Forwarded into the native fill stage to bias the top/bottom
  // padding split toward whichever side is missing more world content.
  final double fillVerticalCameraDeg;

  /// Pre-computed keypoints + matches from the Dart-side ONNX feature
  /// pipeline. When non-null the worker isolate calls the
  /// `stitch_with_features` FFI symbol; otherwise the legacy `stitch`
  /// symbol runs OpenCV's built-in detector.
  final Uint8List? featuresBlob;
}

/// Top-level function required by [compute] — runs in a worker isolate.
///
/// Returns [_StitchParams.outPath] on success; throws on failure.
String _stitchSync(_StitchParams p) {
  // DynamicLibrary must be opened inside the isolate that will use it.
  final dylib = Platform.isAndroid
      ? DynamicLibrary.open("libcamera_360.so")
      : DynamicLibrary.process();

  final bindings = Camera360Bindings(dylib);

  final Pointer<Utf8> imagesPtr = p.imagePaths.toString().toNativeUtf8();
  final Pointer<Utf8> modelPtr = p.model.toNativeUtf8();
  final Pointer<Utf8> outPtr = p.outPath.toNativeUtf8();
  final Pointer<Utf8> cropPtr = p.cropMode.toNativeUtf8();

  // Pin the features blob into native memory for the lifetime of the FFI
  // call. malloc.allocate gives us a region the native side can read
  // directly without worrying about Dart GC moving things.
  Pointer<Uint8>? featuresPtr;
  final featuresLen = p.featuresBlob?.length ?? 0;
  if (p.featuresBlob != null && featuresLen > 0) {
    featuresPtr = malloc.allocate<Uint8>(featuresLen);
    featuresPtr.asTypedList(featuresLen).setAll(0, p.featuresBlob!);
  }

  bool success;
  try {
    if (featuresPtr != null) {
      success = bindings.stitch_with_features(
        imagesPtr.cast<Char>(),
        featuresPtr,
        featuresLen,
        outPtr.cast<Char>(),
        p.loop360,
        p.loopClosure,
        cropPtr.cast<Char>(),
        p.fallback,
        p.fill,
        p.fillRatio,
        p.fillSource,
        p.fillBlur,
        p.fillFade,
        p.fillBlurRatio,
        p.fillFadePower,
        p.fillVerticalCameraDeg,
      );
    } else {
      success = bindings.stitch(
        imagesPtr.cast<Char>(),
        modelPtr.cast<Char>(),
        outPtr.cast<Char>(),
        p.loop360,
        p.loopClosure,
        cropPtr.cast<Char>(),
        p.fallback,
        p.fill,
        p.fillRatio,
        p.fillSource,
        p.fillBlur,
        p.fillFade,
        p.fillBlurRatio,
        p.fillFadePower,
        p.fillVerticalCameraDeg,
      );
    }
  } finally {
    malloc.free(imagesPtr);
    malloc.free(modelPtr);
    malloc.free(outPtr);
    malloc.free(cropPtr);
    if (featuresPtr != null) malloc.free(featuresPtr);
  }

  if (!success) {
    final stage = p.featuresBlob != null
        ? 'stitch_with_features (native compose failed — check Xcode/Android '
            'logcat for ExternalFeatures / PanoramicStitcher messages)'
        : 'stitch (OpenCV matcher or compose failed)';
    throw Exception('Stitching failed: $stage');
  }
  return p.outPath;
}

class Stitcher {
  /// Stitch [images] into a 360 panorama using one of OpenCV's built-in
  /// feature detectors, or — when [featuresBlob] is supplied — feed
  /// pre-computed keypoints + matches directly to the native composer
  /// (typically from the Dart-side SuperPoint + LightGlue ONNX pipeline,
  /// see `OnnxFeaturePipeline`).
  ///
  /// The heavy FFI work runs in a worker isolate via [compute] so it never
  /// blocks the main thread.
  static Future<XFile> stitchImages(
    List<XFile> images, {
    StitchFeatureModel model = StitchFeatureModel.sift,
    bool loop360 = true,
    bool loopClosure = true,
    StitchCropMode crop = StitchCropMode.inner,
    bool fallback = true,
    bool fill = true,
    double fillRatio = 2.0,
    int fillSource = 30,
    int fillBlur = 401,
    int fillFade = 50,
    double fillBlurRatio = 0.01,
    double fillFadePower = 5.0,
    // Phone vertical pose at capture time on the 0..180 scale used by the
    // capture UI. 90 = camera at horizon → symmetric top/bottom fill (the
    // legacy default); deviations bias the fill toward whichever side is
    // missing more world content.
    double fillVerticalCameraDeg = 90.0,
    // Optional pre-computed keypoints + matches blob (see
    // ExternalFeatures.hpp for the byte layout). When non-null, routes the
    // FFI call through `stitch_with_features` and the `model` argument is
    // ignored by the native side.
    Uint8List? featuresBlob,
    // Optional progress callback fired ~20 Hz while the worker isolate is
    // stitching. When [featuresBlob] is non-null (ONNX path) the native
    // pipeline only covers compose work, so the callback's percent is
    // rescaled into 65..100 to leave room for the Dart-side feature
    // matching (0..65) emitted by [OnnxFeaturePipeline].
    StitchProgressCallback? onStitchProgress,
  }) async {
    // getApplicationDocumentsDirectory uses a platform channel — must be
    // called on the main isolate before spawning the worker.
    final dir = await getApplicationDocumentsDirectory();
    final outPath =
        "${dir.path}/stitched-panorama-${DateTime.now().millisecondsSinceEpoch}.jpg";

    final params = _StitchParams(
      imagePaths: images.map((f) => f.path).toList(),
      model: model.nativeName,
      outPath: outPath,
      loop360: loop360,
      loopClosure: loopClosure,
      cropMode: crop.nativeName,
      fallback: fallback,
      fill: fill,
      fillRatio: fillRatio,
      fillSource: fillSource,
      fillBlur: fillBlur,
      fillFade: fillFade,
      fillBlurRatio: fillBlurRatio,
      fillFadePower: fillFadePower,
      fillVerticalCameraDeg: fillVerticalCameraDeg,
      featuresBlob: featuresBlob,
    );

    debugPrint(params.imagePaths.toString());

    Timer? poller;
    Pointer<Int>? percentPtr;
    Pointer<Int>? stagePtr;
    if (onStitchProgress != null) {
      final bindings = _mainIsolateBindings();
      percentPtr = malloc.allocate<Int>(sizeOf<Int>());
      stagePtr = malloc.allocate<Int>(sizeOf<Int>());
      percentPtr.value = 0;
      stagePtr.value = _stageIdle;

      // ONNX matching runs in Dart (0..65 %); native compose fills 65..100.
      final bool rescale = featuresBlob != null;
      const int onnxComposeStart = 65;
      const int onnxComposeSpan = 100 - onnxComposeStart;

      // ONNX matching already reported 0..65 %; don't let native reset()
      // at stitch_with_features start look like a drop back to 0 %.
      int lastPercent = rescale ? onnxComposeStart : -1;
      int lastStage = -1;
      poller = Timer.periodic(const Duration(milliseconds: 50), (_) {
        bindings.stitch_get_progress(percentPtr!, stagePtr!);
        final int nativePercent = percentPtr.value;
        final int stageId = stagePtr.value;
        int overall = rescale
            ? (onnxComposeStart +
                    (nativePercent * onnxComposeSpan) ~/ 100)
                .clamp(0, 100)
            : nativePercent.clamp(0, 100);
        if (overall < lastPercent) overall = lastPercent;
        if (overall == lastPercent && stageId == lastStage) return;
        lastPercent = overall;
        lastStage = stageId;
        onStitchProgress(_stageLabel(stageId), overall);
      });
    }

    try {
      final resultPath = await compute(_stitchSync, params);
      // Make sure the UI lands on 100 % even if the timer didn't fire
      // between the final atomic update and the FFI return.
      if (onStitchProgress != null) {
        onStitchProgress(_stageLabel(_stageFinalize), 100);
      }
      return XFile(resultPath);
    } finally {
      poller?.cancel();
      if (percentPtr != null) malloc.free(percentPtr);
      if (stagePtr != null) malloc.free(stagePtr);
    }
  }
}
