export 'package:camera_360/utilities/stitcher.dart'
    show StitchCropMode, StitchFeatureModel, Stitcher;

import 'package:camera_360/layouts/camera_selector.dart';
import 'package:camera_360/layouts/full_screen_camera_preview.dart';
import 'package:camera_360/layouts/helper_text.dart';
import 'package:camera_360/layouts/orientation_helpers.dart';
import 'package:camera_360/layouts/stitching_progress_overlay.dart';
import 'package:camera_360/utilities/onnx_feature_pipeline.dart';
import 'package:camera_360/utilities/stitcher.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart' show SchedulerBinding;
import 'package:flutter/services.dart' show rootBundle;
import 'package:camera/camera.dart';
import 'package:flutter_image_compress/flutter_image_compress.dart';
import 'package:gallery_saver_plus/gallery_saver.dart';
import 'package:dchs_motion_sensors/dchs_motion_sensors.dart';
import 'package:path_provider/path_provider.dart';
import 'dart:async';
import 'dart:math' show cos, sin, atan2, asin;
import 'dart:typed_data';
import 'package:vector_math/vector_math_64.dart' hide Colors;
import 'dart:io';
import 'package:wakelock_plus/wakelock_plus.dart';

/// Builds the start-capture button shown above the camera selector.
///
/// [onStart] begins panorama capture. Only invoked before capture has started;
/// the button is hidden once capture begins.
typedef CaptureStartButtonBuilder = Widget Function(
  VoidCallback onStart,
  bool enabled,
);

/// Builds the overlay shown while the panorama is being stitched.
///
/// [stage] is a human-readable English label that progresses through
/// "Preprocessing", "Matching features", "Bundle adjustment", "Warping",
/// "Blending", "Finalising", etc. [percent] is an overall 0..100 figure
/// across the entire stitch pipeline (matching + composition).
typedef StitchingProgressBuilder = Widget Function(
  BuildContext context,
  String stage,
  int percent,
);

class Camera360 extends StatefulWidget {
  /// Callback called when capture has ended and panorama is prepared
  final void Function(Map<String, dynamic>) onCaptureEnded;

  /// Callback called when camera has changed
  final void Function(int)? onCameraChanged;

  /// Callback called when progress has changed
  final void Function(int)? onProgressChanged;

  /// Optional builder for the overlay shown while the panorama is being
  /// stitched. When omitted, [StitchingProgressOverlay] is shown (stage
  /// label, progress bar, and percentage). When the stitcher hasn't
  /// started yet the overlay falls back to the plain [loadingText].
  final StitchingProgressBuilder? stitchingProgressBuilder;

  /// Number of photos captured in a full 360° rotation. Defaults to 16.
  final int? photoCount;

  /// Width (in pixels) each captured frame is resized to before stitching.
  /// Lower values are faster; defaults to 1000.
  final int? captureImageWidth;

  /// JPEG quality used when resizing captured frames (1–100). Defaults to 50.
  final int? captureImageQuality;

  /// Text shown in the loading overlay while the panorama is being stitched.
  final String? loadingText;

  /// Instruction text shown above the helper dot during capture.
  final String? helperText;

  /// Text shown when the device needs to be tilted left to level the horizon.
  final String? tiltLeftText;

  /// Text shown when the device needs to be tilted right to level the horizon.
  final String? tiltRightText;

  /// The vertical angle (0..180) the device should be held at during capture,
  /// where 90 = camera at the horizon, < 90 = camera tilted down, > 90 =
  /// camera tilted up. Defaults to 90 (camera aimed slightly downward), which
  /// also biases the post-capture fill so more padding goes on top to recover
  /// the missing sky. Pass 90 for the classic horizon-level capture mode.
  final double captureVerticalDeg;

  /// This popup is shown to help the user select a camera
  final bool cameraSelectorInfoPopUpShow;

  /// cameraSelector popup visibility
  final bool cameraSelectorShow;

  /// cameraSelector popup content [Widget]
  final Widget? cameraSelectorInfoPopUpContent;

  /// Camera not ready content [Widget]
  final Widget? cameraNotReadyContent;

  /// Builder for the start-capture button placed above the camera selector.
  /// When omitted, a default "Start capture" button is shown.
  final CaptureStartButtonBuilder? captureStartButtonBuilder;

  // ── Native stitcher options (see `Stitcher.stitch` / Stitcher.h) ───────────

  /// Feature pipeline: sift / orb / akaze / brisk (OpenCV's built-in
  /// detectors) or onnx (Dart-side SuperPoint + LightGlue, see
  /// [onnxModelAssetKey]).
  final StitchFeatureModel stitchModel;

  /// Flutter asset key for the SuperPoint + LightGlue ONNX pipeline model
  /// (e.g. `'models/superpoint_lightglue_pipeline.ort.onnx'`). Only
  /// consulted when [stitchModel] is `StitchFeatureModel.onnx`.
  ///
  /// The plugin copies the asset to a writable directory the first time it is
  /// needed, caching by file size. The model is not bundled with the plugin —
  /// declare it as an asset in your app's `pubspec.yaml`:
  ///
  /// ```yaml
  /// flutter:
  ///   assets:
  ///     - models/
  /// ```
  ///
  /// If the asset cannot be found, stitching throws a descriptive error.
  final String? onnxModelAssetKey;

  /// Match the closing pair (last ↔ first) for a full 360° loop.
  final bool stitchLoop360;

  /// Close the wrap seam (BA boost, yaw closure, ring canvas, period crop).
  /// Only applies when [stitchLoop360] is true. Defaults to enabled.
  final bool stitchLoopClosure;

  /// How the blended canvas is cropped before optional fill.
  final StitchCropMode stitchCrop;

  /// Synthesize homographies for failed adjacent pairs from a donor pair.
  final bool stitchFallback;

  /// Pad top/bottom with blurred edge fill to [stitchFillRatio] aspect ratio.
  final bool stitchFill;

  /// Target width/height when [stitchFill] is true (e.g. 2.0 for 2:1).
  final double stitchFillRatio;

  /// Rows sampled from each panorama edge for the blur profile.
  final int stitchFillSource;

  /// Horizontal Gaussian kernel size for the edge profile (forced odd, ≥ 3).
  final int stitchFillBlur;

  /// Pano rows at the join blended with the edge profile.
  final int stitchFillFade;

  /// Fraction of the fill band that stays pure blur before fading to flat colour.
  final double stitchFillBlurRatio;

  /// Fade curve steepness toward flat colour (1.0 = linear).
  final double stitchFillFadePower;

  /// When true, each captured frame is copied to the device photo gallery
  /// before cache cleanup so frames survive [deleteCache]. Defaults to false.
  final bool debug;

  const Camera360({
    Key? key,
    required this.onCaptureEnded,
    this.onCameraChanged,
    this.onProgressChanged,
    this.stitchingProgressBuilder,
    this.photoCount,
    this.captureImageWidth,
    this.captureImageQuality,
    this.loadingText,
    this.helperText,
    this.tiltLeftText,
    this.tiltRightText,
    this.cameraSelectorShow = true,
    this.cameraSelectorInfoPopUpShow = true,
    this.cameraSelectorInfoPopUpContent,
    this.cameraNotReadyContent,
    this.captureStartButtonBuilder,
    this.captureVerticalDeg = 90,
    this.stitchModel = StitchFeatureModel.sift,
    this.onnxModelAssetKey,
    this.stitchLoop360 = true,
    this.stitchLoopClosure = true,
    this.stitchCrop = StitchCropMode.inner,
    this.stitchFallback = true,
    this.stitchFill = true,
    this.stitchFillRatio = 2.0,
    this.stitchFillSource = 30,
    this.stitchFillBlur = 401,
    this.stitchFillFade = 50,
    this.stitchFillBlurRatio = 0.01,
    this.stitchFillFadePower = 5.0,
    this.debug = false,
  }) : super(key: key);

  @override
  State<Camera360> createState() => _Camera360State();
}

// THE STATE IS BEING UPDATED EVERY SET SECONDS
// The state is updated by this function _setupSensors
class _Camera360State extends State<Camera360> with WidgetsBindingObserver {
  // A list with all available camera
  late List<CameraDescription> cameras;
  // Camera controller
  late CameraController controller;
  // _isReady is true when device is ready to take another image
  bool _isReady = false;
  double posX = 180, posY = 350;
  // _absoluteOrientation is a vector containing the yaw,pitch,roll of the device
  final Vector3 _absoluteOrientation = Vector3.zero();
  // StreamSubscription will subscribe to _absoluteOrientation changes
  final List<StreamSubscription<dynamic>> _streamSubscriptions =
      <StreamSubscription<dynamic>>[];

  // CONSTANTS
  // Centered dot
  static const double centeredDotRadius = 30;
  static const double centeredDotBorder = 2;

  // Helper dot
  late double deviceVerticalCorrectDeg;
  static const double helperDotRadius = 20;
  bool helperDotVerticalInPos = false;
  // Tolerances (half-windows) used when deciding whether the device is on
  // target for a photo. The "in position" check passes when the absolute
  // delta between target and current reading is within these bounds.
  static const double helperDotVerticalTolerance = 1;
  static const double helperDotHorizontalTolerance = 2;
  static const double helperDotRotationTolerance = 4;
  // Visual sensitivity of the helper dot: how many on-screen pixels the dot
  // moves per degree of rotation still needed.
  static const double helperDotPixelsPerDegH = 1;

  // System Variables (Some of these variables can be updated via the Constructor)
  late int capturedImageWidth;
  late int capturedImageQuality;
  late int nrPhotos;
  // Angular step between consecutive photos in a full 360° rotation.
  late double degToNextPosition;

  // VARIABLES
  List<XFile> capturedImages = [];
  List<XFile> capturedImagesForDeletion = [];
  int progressPercentage = 0;
  // True while the device is aligned with the helper dot horizontally /
  // vertically. Updated as a side-effect of the helper-dot position helpers.
  bool helperDotIsHorizontalInPos = false;
  bool deviceInCorrectPosition = false;

  // ── Heading bookkeeping ────────────────────────────────────────────────
  //
  // The horizontal helper-dot is driven entirely off two cumulative angles
  // that increase as the user rotates clockwise from capture start. Working
  // in unwrapped degrees (no 0/360 modulo, no per-photo "right range" split)
  // collapses the legacy heading code to a single subtraction and avoids the
  // discontinuities that used to make the dot jump by ~360° near the
  // wrap-around boundary.
  //
  // Both angles start at 0 on capture start.
  double _cumulativeHorizontalDeg = 0;
  // Last raw 0..360 compass heading observed; used to convert each new
  // reading into a signed shortest-path delta before accumulating.
  double? _previousRawHorizontalDeg;
  // Cumulative target where the next photo should be taken.
  double _targetCumulativeHorizontalDeg = 0;
  // While taking image is set to true
  bool takingPicture = false;
  // While waiting to take picture
  bool isWaitingToTakePhoto = false;
  // Time to wait before taking picture
  int timeToWaitBeforeTakingPicture = 500; // milliseconds
  // When stitching fails the user will need to take a another image
  // more to the left
  bool hasStitchingFailed = false;
  int selectedCameraKey = 0;
  bool _hasWideOrUltraWideCamera = false;
  String loadingText = "";
  String helperText = "";
  String helperTiltLeftText = "";
  String helperTiltRightText = "";

  int nrPhotosTaken = 0;
  late XFile finalStitchedImage;
  bool imageSaved = false;
  bool isPanoramaBeingStitched = false;

  // Stitch-phase progress mirrored from the native ProgressTracker and the
  // Dart-side ONNX pipeline. `_stitchStage` is empty until the first
  // callback fires; the loading overlay falls back to plain `loadingText`
  // while that's the case.
  String _stitchStage = '';
  int _stitchPercent = 0;

  /// Bumped at each stitch so [StitchingProgressOverlay] resets its bar once.
  int _stitchRunId = 0;

  // Add this field at the top of the class with other variables
  Timer? _waitingTimer;
  // Panorama capture runs only after the user presses the start button
  bool _captureEnabled = false;
  // True once we have seen a real sample on the absolute-orientation stream.
  // Used to gate the cumulative-heading accumulation so it does not anchor on
  // the stale/zero `_absoluteOrientation` value present before the first
  // sensor event.
  bool _sensorHasFirstReading = false;

  // Resolved writable path for the ONNX model. Set during initState when
  // onnxModelAssetKey is provided; null until the copy completes (or when
  // the OpenCV path is used).
  String? _resolvedOnnxModelPath;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);

    // Initialise capture settings from widget params.
    deviceVerticalCorrectDeg = widget.captureVerticalDeg;
    capturedImageWidth = widget.captureImageWidth ?? 1000;
    capturedImageQuality = widget.captureImageQuality ?? 50;
    nrPhotos = widget.photoCount ?? 16;
    degToNextPosition = 360 / nrPhotos;
    selectedCameraKey = 0;
    loadingText = widget.loadingText ?? 'Preparing panorama...';
    helperText = widget.helperText ?? 'Point the camera at the dot';
    helperTiltLeftText = widget.tiltLeftText ?? 'Tilt left';
    helperTiltRightText = widget.tiltRightText ?? 'Tilt right';

    // Pre-warm the ONNX model path so it is ready before the first stitch.
    if (widget.stitchModel == StitchFeatureModel.onnx &&
        widget.onnxModelAssetKey != null &&
        widget.onnxModelAssetKey!.isNotEmpty) {
      _ensureOnnxModelOnDisk(widget.onnxModelAssetKey!).then((path) {
        if (!mounted) return;
        setState(() => _resolvedOnnxModelPath = path);
      }).catchError((Object e) {
        debugPrint('Camera360: failed to extract ONNX model: $e');
      });
    }

    _setupCameras();
  }

  // Delete files and cache before restarting app
  Future<void> deleteCache() async {
    // Delete all images
    await deletePanoramaImages();
  }

  void _startCapture() {
    if (_captureEnabled) {
      return;
    }
    _sensorHasFirstReading = false;
    _resetHeadingTracking();
    _setupSensors();
    setState(() {
      _captureEnabled = true;
    });
  }

  // Resets the unwrapped-heading bookkeeping. Must be called whenever the
  // user's "start" reference point is about to change (capture start,
  // panorama restart, etc.) so the cumulative deltas don't carry over from
  // a previous run.
  void _resetHeadingTracking() {
    _cumulativeHorizontalDeg = 0;
    _previousRawHorizontalDeg = null;
    _targetCumulativeHorizontalDeg = 0;
  }

  // Folds the latest raw 0..360 compass heading into the cumulative
  // (unwrapped) heading by taking the shortest-path signed delta. The first
  // reading after a reset just anchors the previous value and returns 0,
  // giving every capture a "starts at 0°" reference frame.
  double _updateCumulativeHorizontalDeg(double rawHeadingDeg) {
    if (_previousRawHorizontalDeg == null) {
      _previousRawHorizontalDeg = rawHeadingDeg;
      return _cumulativeHorizontalDeg;
    }
    double delta = rawHeadingDeg - _previousRawHorizontalDeg!;
    if (delta > 180) {
      delta -= 360;
    } else if (delta < -180) {
      delta += 360;
    }
    _cumulativeHorizontalDeg += delta;
    _previousRawHorizontalDeg = rawHeadingDeg;
    return _cumulativeHorizontalDeg;
  }

  void _disableCapture() {
    _waitingTimer?.cancel();
    _captureEnabled = false;
    takingPicture = false;
    isWaitingToTakePhoto = false;
    _disableSensors();
  }

  Widget _buildCaptureStartButton() {
    if (_captureEnabled) {
      return const SizedBox.shrink();
    }

    if (widget.captureStartButtonBuilder != null) {
      return widget.captureStartButtonBuilder!(_startCapture, true);
    }

    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: ElevatedButton(
        onPressed: _startCapture,
        child: const Text('Start capture'),
      ),
    );
  }

  // Reset Main
  void restartApp({String? reason, bool clearCache = true}) {
    debugPrint("'Panorama360': Restarting app reason: $reason");
    deleteCache();

    _disableCapture();
    if (!mounted) return;
    setState(() {
      _captureEnabled = false;
      _sensorHasFirstReading = false;
      _resetHeadingTracking();
      capturedImages = [];
      helperDotIsHorizontalInPos = false;
      deviceInCorrectPosition = false;
      takingPicture = false;
      hasStitchingFailed = false;
      nrPhotosTaken = 0;
      imageSaved = false;
      isPanoramaBeingStitched = false;
      progressPercentage = 0;
    });
    widget.onProgressChanged?.call(0);
  }

  // Delete all panorama images
  Future<void> deletePanoramaImages() async {
    capturedImagesForDeletion = capturedImages;
    for (var capturedImage in capturedImagesForDeletion) {
      try {
        if (await File(capturedImage.path).exists()) {
          await File(capturedImage.path).delete();
          debugPrint("'Panorama360': Deleted image: ${capturedImage.path}");
        }
      } catch (e) {
        // Error in getting access to the file.
        debugPrint(
            "'Panorama360': Failed Deleting panorama image: ${capturedImage.path}");
      }
    }
  }

  // Enable Device Motion Sensors
  void _setupSensors() {
    // Update interval
    int interval = Duration.microsecondsPerSecond ~/ 30;
    motionSensors.absoluteOrientationUpdateInterval = interval;
    motionSensors.orientationUpdateInterval = interval;

    // Stream
    _streamSubscriptions.add(motionSensors.absoluteOrientation
        .listen((AbsoluteOrientationEvent event) {
      setState(() {
        _absoluteOrientation.setValues(event.yaw, event.pitch, event.roll);
        _sensorHasFirstReading = true;
      });
    }));
  }

  // Disable Device Motion Sensors
  void _disableSensors() {
    for (StreamSubscription<dynamic> subscription in _streamSubscriptions) {
      subscription.cancel();
    }
    _streamSubscriptions.clear();
  }

  // Setup Cameras
  Future<void> _setupCameras() async {
    cameras = await availableCameras();
    final List<CameraDescription> backOrExternal =
        cameras.where((CameraDescription description) {
      return description.lensDirection == CameraLensDirection.back ||
          description.lensDirection == CameraLensDirection.external;
    }).toList();
    debugPrint(
        "'Panorama360': All back or external cameras: ${backOrExternal.toString()}");
    final List<CameraDescription> ultraWide = backOrExternal
        .where((CameraDescription d) => d.lensType == CameraLensType.ultraWide)
        .toList();
    debugPrint(
        "'Panorama360': All ultra wide cameras: ${ultraWide.toString()}");
    final List<CameraDescription> wideOnly = backOrExternal
        .where((CameraDescription d) => d.lensType == CameraLensType.wide)
        .toList();
    debugPrint("'Panorama360': All wide only cameras: ${wideOnly.toString()}");
    if (ultraWide.isNotEmpty) {
      cameras = ultraWide;
      _hasWideOrUltraWideCamera = true;
    } else if (wideOnly.isNotEmpty) {
      cameras = wideOnly;
      _hasWideOrUltraWideCamera = true;
    } else {
      cameras = backOrExternal;
      _hasWideOrUltraWideCamera = false;
    }
    // Open selected camera by user or first one
    _initCamera(selectedCameraKey);
  }

  // Initialize camera
  Future<void> _initCamera(int cameraKey) async {
    // Check if selected camera exists
    if (cameras.asMap().containsKey(cameraKey) == false) {
      // Update selectedCameraKey
      selectedCameraKey = 0;
      cameraKey = 0;
    }
    // Change the camera
    try {
      CameraDescription description = cameras[cameraKey];
      // Update selectedCameraKey
      selectedCameraKey = cameraKey;
      // initialize camera controllers.
      controller = CameraController(description, ResolutionPreset.max,
          enableAudio: false, imageFormatGroup: ImageFormatGroup.jpeg);
      await controller.initialize();
      setState(() {
        _isReady = true;
      });
    } catch (_) {
      setState(() {
        _isReady = false;
      });
    }
  }

  // Advance the helper dot and allow the next photo to be taken.
  void prepareForNextImageCapture() {
    _moveHelperDotToNextPosition();
    takingPicture = false;
  }

  /// Copy the asset at [assetKey] to a writable directory and return its
  /// on-disk path. Skips the write when the existing file's size already
  /// matches the asset (idempotent across hot restarts and app updates).
  ///
  /// Throws a descriptive [StateError] if the asset cannot be found, so the
  /// caller does not silently fall back to the OpenCV path.
  Future<String> _ensureOnnxModelOnDisk(String assetKey) async {
    final ByteData assetData;
    try {
      assetData = await rootBundle.load(assetKey);
    } catch (e) {
      throw StateError(
        'Camera360: ONNX model asset "$assetKey" could not be loaded. '
        'Make sure it is declared under "flutter: assets:" in your '
        'pubspec.yaml.\nOriginal error: $e',
      );
    }

    final assetBytes = assetData.buffer.asUint8List(
      assetData.offsetInBytes,
      assetData.lengthInBytes,
    );

    final dir = await getApplicationSupportDirectory();
    final fileName = assetKey.split('/').last;
    final outFile = File('${dir.path}/$fileName');

    if (await outFile.exists() && await outFile.length() == assetBytes.length) {
      return outFile.path;
    }

    await outFile.writeAsBytes(assetBytes, flush: true);
    debugPrint('Camera360: ONNX model written to ${outFile.path}');
    return outFile.path;
  }

  /// Runs the native stitcher with [widget]'s stitch options.
  ///
  /// Forwards the current [deviceVerticalCorrectDeg] (the pose the user was
  /// asked to hold during capture, 0..180 with 90 = horizon) so the native
  /// fill stage can bias the top/bottom padding split for tilted captures
  /// instead of always padding symmetrically.
  ///
  /// When [Camera360.stitchModel] is `StitchFeatureModel.onnx`, runs the
  /// SuperPoint + LightGlue ONNX pipeline on the main isolate before
  /// dispatching the FFI call (platform-channel plugins can't run in
  /// background isolates without extra plumbing). The native stitcher then
  /// composes the panorama from the pre-computed matches.
  Future<XFile> _stitchImages(List<XFile> images) async {
    _stitchRunId++;
    _stitchStage = 'Preparing';
    _stitchPercent = 0;
    if (mounted) setState(() {});
    Uint8List? featuresBlob;
    if (widget.stitchModel == StitchFeatureModel.onnx) {
      final assetKey = widget.onnxModelAssetKey;
      if (assetKey == null || assetKey.isEmpty) {
        throw StateError(
          'Camera360: stitchModel is StitchFeatureModel.onnx but '
          'onnxModelAssetKey was not provided.',
        );
      }
      final modelPath =
          _resolvedOnnxModelPath ?? await _ensureOnnxModelOnDisk(assetKey);
      _resolvedOnnxModelPath ??= modelPath;

      debugPrint(
        'Camera360: ONNX stitch — ${images.length} images, model=$modelPath',
      );
      final pipeline = OnnxFeaturePipeline();
      try {
        await pipeline.load(modelPath);
        featuresBlob = await pipeline.compute(
          images,
          loop360: widget.stitchLoop360,
          onProgress: _emitStitchProgress,
        );
        debugPrint(
          'Camera360: ONNX features ready (${featuresBlob.length} bytes), '
          'calling native stitch_with_features',
        );
      } catch (e, st) {
        debugPrint('Camera360: ONNX feature pipeline failed: $e');
        debugPrint('$st');
        rethrow;
      } finally {
        await pipeline.dispose();
      }
    }

    return Stitcher.stitchImages(
      images,
      model: widget.stitchModel,
      loop360: widget.stitchLoop360,
      loopClosure: widget.stitchLoopClosure,
      crop: widget.stitchCrop,
      fallback: widget.stitchFallback,
      fill: widget.stitchFill,
      fillRatio: widget.stitchFillRatio,
      fillSource: widget.stitchFillSource,
      fillBlur: widget.stitchFillBlur,
      fillFade: widget.stitchFillFade,
      fillBlurRatio: widget.stitchFillBlurRatio,
      fillFadePower: widget.stitchFillFadePower,
      fillVerticalCameraDeg: deviceVerticalCorrectDeg,
      featuresBlob: featuresBlob,
      onStitchProgress: _emitStitchProgress,
    );
  }

  Widget _buildStitchOverlay(BuildContext context) {
    if (!isPanoramaBeingStitched) {
      return Center(
        child: Text(
          loadingText,
          style: const TextStyle(color: Colors.white),
        ),
      );
    }

    final stage = _stitchStage.isEmpty ? loadingText : _stitchStage;
    if (widget.stitchingProgressBuilder != null) {
      return widget.stitchingProgressBuilder!(
        context,
        stage,
        _stitchPercent,
      );
    }

    return StitchingProgressOverlay(
      key: ValueKey(_stitchRunId),
      stage: stage,
      percent: _stitchPercent,
      fallbackLabel: loadingText,
    );
  }

  /// Updates stitch overlay state from the ONNX pipeline or native poller.
  void _emitStitchProgress(String stage, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    // Never move the displayed bar backward between stages.
    if (percent < _stitchPercent) percent = _stitchPercent;
    if (stage == _stitchStage && percent == _stitchPercent) return;
    if (mounted) {
      setState(() {
        _stitchStage = stage;
        _stitchPercent = percent;
      });
    } else {
      _stitchStage = stage;
      _stitchPercent = percent;
    }
  }

  // Resize captured image for faster stitching
  Future<XFile> resizeImage(File img) async {
    final filePath = img.absolute.path;
    final lastIndex = filePath.lastIndexOf(RegExp(r'.png|.jp'));
    final splitted = filePath.substring(0, (lastIndex));
    final outPath = "${splitted}_compressed${filePath.substring(lastIndex)}";

    XFile? compressedImage;
    if (lastIndex == filePath.lastIndexOf(RegExp(r'.png'))) {
      compressedImage = await FlutterImageCompress.compressAndGetFile(
        filePath,
        outPath,
        format: CompressFormat.png,
        quality: capturedImageQuality,
        minWidth: capturedImageWidth,
        minHeight: 1,
      );
    } else {
      compressedImage = await FlutterImageCompress.compressAndGetFile(
        filePath,
        outPath,
        quality: capturedImageQuality,
        minWidth: capturedImageWidth,
        minHeight: 1,
      );
    }

    // delete original file
    try {
      if (await File(img.path).exists()) {
        await File(img.path).delete();
      }
    } catch (e) {
      // Error in getting access to the file.
      debugPrint("'Panorama360': Failed Deleting panorama image");
    }

    debugPrint("'Panorama360': Image taken: ${compressedImage!.path}");

    return XFile(compressedImage.path);
  }

  /// Copies [imagePath] to the photo gallery when [Camera360.debug] is enabled.
  Future<void> _saveCapturedImageToGalleryIfDebug(String imagePath) async {
    if (!widget.debug) {
      return;
    }
    try {
      final saved = await GallerySaver.saveImage(imagePath);
      debugPrint(
          "'Panorama360': Debug gallery save ${saved == true ? 'ok' : 'failed'}: $imagePath");
    } catch (e) {
      debugPrint("'Panorama360': Debug gallery save error: $e");
    }
  }

  // Take picture
  Future<XFile?> _takePicture() async {
    try {
      takingPicture = true;
      XFile image = await controller.takePicture().then((XFile? file) {
        return resizeImage(File(file!.path));
      });
      await _saveCapturedImageToGalleryIfDebug(image.path);
      capturedImages.add(image);
      nrPhotosTaken++;
      prepareForNextImageCapture();
      return image;
    } catch (e) {
      return null;
    }
  }

  // Advance the cumulative target by one photo step. The target is stored
  // unwrapped (no 0/360 modulo), so this is just an addition and the helper
  // dot can never jump by ~360°.
  void _moveHelperDotToNextPosition() {
    _targetCumulativeHorizontalDeg += degToNextPosition;
    if (morePhotosNeeded()) {
      updateProgress();
    }
  }

  // Position the helper dot relative to the current vertical reading.
  // Symmetric mapping: at the target the dot sits at screen centre, and the
  // dot moves ~`(containerHeight / 180)` pixels for each degree away from
  // target so the full screen height covers ±90° of pitch. This works for
  // any [deviceVerticalCorrectDeg], not just 90°.
  double updateHelperDotVerticalPosition(
      double deviceVerticalDeg, double containerHeight) {
    final double remainingDeg = deviceVerticalCorrectDeg - deviceVerticalDeg;
    helperDotVerticalInPos = remainingDeg.abs() <= helperDotVerticalTolerance;

    final double centerY = containerHeight / 2;
    final double usableHeight = containerHeight - 2 * helperDotRadius;
    final double pixelsPerDeg = usableHeight / 180;

    // Positive remaining = camera is below target → the user must tilt up,
    // so the dot is drawn above centre to lead them there.
    double helperDotPosY =
        centerY - helperDotRadius - remainingDeg * pixelsPerDeg;
    if (helperDotPosY < 0) helperDotPosY = 0;
    final double maxY = containerHeight - 2 * helperDotRadius;
    if (helperDotPosY > maxY) helperDotPosY = maxY;
    return helperDotPosY;
  }

  // Position the helper dot horizontally based on how much rotation is still
  // needed. Single formula, no wrap, no "moveRight" branch:
  //   posX = screen-centre + (target − current) * pixels-per-degree
  //
  // Both `target` and `current` are unwrapped (cumulative) degrees, so a
  // small sensor jitter only produces a small dot movement — the legacy
  // ~360° jumps near the wrap boundary cannot happen here.
  double updateHelperDotHorizontalPosition(
      double cumulativeHorizontalDeg, double containerWidth) {
    final double remainingDeg =
        _targetCumulativeHorizontalDeg - cumulativeHorizontalDeg;
    helperDotIsHorizontalInPos =
        remainingDeg.abs() <= helperDotHorizontalTolerance;

    final double centerX = containerWidth / 2;
    double helperDotPosX =
        centerX - helperDotRadius + remainingDeg * helperDotPixelsPerDegH;
    const double minX = helperDotRadius;
    final double maxX = containerWidth - 2 * helperDotRadius;
    if (helperDotPosX < minX) helperDotPosX = minX;
    if (helperDotPosX > maxX) helperDotPosX = maxX;
    return helperDotPosX;
  }

  // Check if Device Rotation is correct
  bool checkDeviceRotation(deviceRotationDeg) {
    if (checkRightDeviceRotation(deviceRotationDeg) &&
        checkLeftDeviceRotation(deviceRotationDeg)) {
      return true;
    }
    return false;
  }

  // Check if Device is rotated more to the left
  bool checkLeftDeviceRotation(deviceRotationDeg) {
    if (deviceRotationDeg < (helperDotRotationTolerance * -1)) {
      return false;
    }

    return true;
  }

  // Check if Device is rotated more to the right
  bool checkRightDeviceRotation(deviceRotationDeg) {
    if (deviceRotationDeg > helperDotRotationTolerance) {
      return false;
    }

    return true;
  }

  // Prepare data for the callback function after image stitched
  void prepareOnCaptureEnded(finalStitchedImage) {
    // Check if resolution is greater then 2:1

    Map<String, dynamic> returnedData = {
      'success': finalStitchedImage != null ? true : false,
      'panorama': finalStitchedImage,
      'options': {
        'selected_camera': selectedCameraKey,
        'vertical_camera_angle': deviceVerticalCorrectDeg,
      },
    };

    widget.onCaptureEnded(returnedData);

    // After panorama stitching has failed or succeeded then restart the app
    restartApp(reason: "Capture ended");
  }

  // Stitch all captured imaged into the Final Panorama image
  Future<void> prepareFinalPanorama() async {
    // Download image
    if (imageSaved == false) {
      imageSaved = true;
      if (isPanoramaBeingStitched == false) {
        isPanoramaBeingStitched = true;

        // All pictures have been captured — report 100% before stitching starts.
        if (progressPercentage != 100) {
          progressPercentage = 100;
          widget.onProgressChanged?.call(progressPercentage);
        }

        try {
          finalStitchedImage = await _stitchImages(capturedImages);
          isPanoramaBeingStitched = false;

          // Callback function
          prepareOnCaptureEnded(finalStitchedImage);
        } catch (e, st) {
          stitchingFailed();

          // Callback function
          prepareOnCaptureEnded(null);
          debugPrint("'Panorama360': Stitching failed: $e");
          debugPrint('$st');
        }
      }
    }
  }

  // Check if more photos are needed to complete a 360 deg panorama
  bool morePhotosNeeded() {
    return nrPhotosTaken < nrPhotos;
  }

  // Check if mobile is in position and app is ready to take photo
  bool readyToTakePhoto() {
    return _captureEnabled &&
        morePhotosNeeded() &&
        deviceInCorrectPosition &&
        takingPicture == false &&
        hasStitchingFailed == false;
  }

  // Stitching failed
  void stitchingFailed() {
    hasStitchingFailed = true;
    restartApp(reason: "Stitching failed");
  }

  // Select camera lens
  void selectCamera(int cameraKey) {
    // Inform that camera has changed
    widget.onCameraChanged?.call(cameraKey);
    // Initialize new camera
    _initCamera(cameraKey).then((value) {
      // Restart app
      restartApp(reason: "Camera selected");
    });
  }

  // Translate the cumulative target into a 0..100 progress percentage. The
  // target is unwrapped, so the formula is a straight ratio without the
  // legacy "if we just wrapped past 360, force 100%" special case.
  void updateProgress() {
    int newProgressPercentage =
        (_targetCumulativeHorizontalDeg * 100 / 360).round();
    if (newProgressPercentage > 100) newProgressPercentage = 100;
    if (newProgressPercentage < 0) newProgressPercentage = 0;
    if (newProgressPercentage != progressPercentage) {
      progressPercentage = newProgressPercentage;
      widget.onProgressChanged?.call(progressPercentage);
    }
  }

  // When app is not active disable the sensor readings
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    super.didChangeAppLifecycleState(state);

    switch (state) {
      case AppLifecycleState.resumed:
        // Reenable sensors when app state is resumed during active capture
        if (_captureEnabled) {
          _setupSensors();
        }
        break;
      case AppLifecycleState.inactive:
      case AppLifecycleState.paused:
      case AppLifecycleState.detached:
      case AppLifecycleState.hidden:
        // Disable sensors when app is not active
        _disableSensors();
        break;
    }
  }

  @override
  void dispose() {
    _disableCapture();
    // Disable screen always on
    WakelockPlus.disable();
    controller.dispose();
    // Sensors are disabled using didChangeAppLifecycleState but we need
    // to disable them again when disposed in case user navigates into another
    // view
    _disableSensors();

    // Restart app
    restartApp(reason: "App disposed");
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!_isReady) {
      return widget.cameraNotReadyContent ??
          const Center(
            child: Padding(
              padding: EdgeInsets.symmetric(horizontal: 10, vertical: 20),
              child: Text(
                "Camera loading...",
                style: TextStyle(fontWeight: FontWeight.bold),
              ),
            ),
          );
    } else {
      // Keep the screen on
      WakelockPlus.enable();

      return LayoutBuilder(
        builder: (BuildContext context, BoxConstraints constraints) {
          if (morePhotosNeeded() == false) {
            // Schedule outside the build phase so that any setState / callback
            // calls inside prepareFinalPanorama don't trigger
            // "setState called during build".
            SchedulerBinding.instance.addPostFrameCallback((_) {
              if (mounted) prepareFinalPanorama();
            });
          }

          // Reconstruct the device-to-world rotation matrix from the
          // reported (yaw, pitch, roll) Euler triple and read heading,
          // elevation and camera-axis tilt off that matrix. Working through
          // the matrix is gimbal-lock-free, so the UI behaves at any
          // `deviceVerticalCorrectDeg`, including 90° (phone vertical,
          // camera on the horizon), where Euler-based heading otherwise
          // becomes singular and pitch is clamped at ±90°.
          //
          // The heading is then folded into `_cumulativeHorizontalDeg`
          // (further below), which is what the helper-dot positioner
          // actually consumes. A waiting UI is rendered until the first
          // real sensor sample arrives so we never anchor on the stale
          // (0, 0, 0) default.
          final double euA = _absoluteOrientation.x;
          final double euB = _absoluteOrientation.y;
          final double euG = _absoluteOrientation.z;
          final double cosA = cos(euA), sinA = sin(euA);
          final double cosB = cos(euB), sinB = sin(euB);
          final double cosG = cos(euG), sinG = sin(euG);

          // Back-camera axis (device −Z) expressed in world ENU coordinates.
          final double camEast = -(cosA * sinG + sinA * sinB * cosG);
          final double camNorth = -(sinA * sinG - cosA * sinB * cosG);
          final double camUp = -(cosB * cosG);

          // Continuous compass heading, 0..360, increasing as the user
          // rotates clockwise (matches the old yaw+roll convention).
          double headingDeg = degrees(atan2(camEast, camNorth));
          if (headingDeg < 0) headingDeg += 360;
          double deviceHorizontalDeg =
              double.parse(headingDeg.toStringAsFixed(1));

          // Elevation of the camera axis above the horizon, remapped to the
          // legacy "90 = horizon" scale so deviceVerticalCorrectDeg keeps the
          // same meaning. Range is [0, 180]: 0 = camera straight down,
          // 90 = horizon, 180 = camera straight up.
          final double elevationDeg = degrees(asin(camUp.clamp(-1.0, 1.0)));
          double deviceVerticalDeg =
              double.parse((90 + elevationDeg).toStringAsFixed(1));

          // Camera-axis tilt, read off the rotation-matrix row that produces
          // pitch (R[2,0] / R[2,1]) instead of going through Euler roll.
          //
          // At the panorama pose (phone vertical, camera at horizon) the
          // sensor's Euler decomposition is gimbal-locked: a tiny physical
          // tilt around the camera axis splits into a ~90° jump in `azimuth`
          // and a compensating ~90° jump in `roll`. The two cancel for the
          // combined heading (which is why the unwrapped cumulative heading
          // is fine) but they do *not* cancel for `roll` alone — so
          // `degrees(roll)` would report ~90° of tilt for ~0° of physical
          // tilt and block photo capture indefinitely.
          //
          // R[2,0] and R[2,1] are continuous through that singularity and
          // depend only on pitch + roll (not azimuth), so this reading is
          // both gimbal-lock-safe and heading-invariant.
          //   R[2,0] = -sin(roll)·cos(pitch) → -sinG · cosB
          //   R[2,1] = -sin(pitch)            →  sinB
          //   tilt   = atan2(-R[2,0], R[2,1]) = atan2(sinG · cosB, sinB)
          final double tiltRad = atan2(sinG * cosB, sinB);
          double deviceRotationDeg =
              double.parse(degrees(tiltRad).toStringAsFixed(1));

          // Accumulate the raw heading into the unwrapped cumulative angle
          // used by the dot helpers. Only counts after a real sensor reading
          // — the (0,0,0) default would otherwise anchor at a bogus 0°.
          double cumulativeHorizontalDeg = _cumulativeHorizontalDeg;
          if (_sensorHasFirstReading) {
            cumulativeHorizontalDeg =
                _updateCumulativeHorizontalDeg(deviceHorizontalDeg);
          }

          if (_captureEnabled && !_sensorHasFirstReading) {
            return Stack(
              children: [
                Positioned.fill(
                  child: FullScreenCameraPreview(controller: controller),
                ),
              ],
            );
          }

          if (!_captureEnabled) {
            return Container(
              color: Colors.black,
              child: morePhotosNeeded()
                  ? Stack(
                      children: [
                        Positioned.fill(
                          child:
                              FullScreenCameraPreview(controller: controller),
                        ),
                        Column(
                          mainAxisAlignment: MainAxisAlignment.end,
                          children: [
                            _buildCaptureStartButton(),
                            widget.cameraSelectorShow == true &&
                                    !_hasWideOrUltraWideCamera
                                ? CameraSelector(
                                    cameras: cameras,
                                    selectedCameraKey: selectedCameraKey,
                                    infoPopUpContent:
                                        widget.cameraSelectorInfoPopUpContent,
                                    infoPopUpShow:
                                        widget.cameraSelectorInfoPopUpShow,
                                    onCameraChanged: (cameraKey) {
                                      selectCamera(cameraKey);
                                    })
                                : Container(),
                          ],
                        ),
                      ],
                    )
                  : _buildStitchOverlay(context),
            );
          }

          double containerWidth = constraints.maxWidth;
          double containerHeight = constraints.maxHeight;
          bool isDeviceRotationCorrect = checkDeviceRotation(deviceRotationDeg);

          // Centered dot
          double centeredDotPosX =
              (containerWidth / 2) - centeredDotRadius - centeredDotBorder;
          double centeredDotPosY =
              (containerHeight / 2) - centeredDotRadius - centeredDotBorder;

          // Update helper dot positions from the unwrapped cumulative
          // heading and the symmetric vertical reading.
          double helperDotPosX = updateHelperDotHorizontalPosition(
              cumulativeHorizontalDeg, containerWidth);
          double helperDotPosY = updateHelperDotVerticalPosition(
              deviceVerticalDeg, containerHeight);
          deviceInCorrectPosition = helperDotVerticalInPos &&
              helperDotIsHorizontalInPos &&
              isDeviceRotationCorrect;

          // Take picture
          if (readyToTakePhoto()) {
            if (!takingPicture && !isWaitingToTakePhoto) {
              takingPicture = true;
              WidgetsBinding.instance.addPostFrameCallback((_) {
                if (mounted) {
                  setState(() {
                    isWaitingToTakePhoto = true;
                  });

                  _waitingTimer?.cancel();
                  _waitingTimer = Timer(
                    Duration(milliseconds: timeToWaitBeforeTakingPicture),
                    () {
                      if (mounted) {
                        // Only proceed if device is still in correct position after the full duration
                        if (deviceInCorrectPosition &&
                            takingPicture &&
                            isWaitingToTakePhoto) {
                          setState(() {
                            isWaitingToTakePhoto = false;
                          });
                          _takePicture();
                        } else {
                          setState(() {
                            isWaitingToTakePhoto = false;
                            takingPicture = false;
                          });
                        }
                      }
                    },
                  );
                }
              });
            }
          } else if ((takingPicture || isWaitingToTakePhoto) &&
              !deviceInCorrectPosition) {
            // Cancel timer and reset states if device moves out of position
            _waitingTimer?.cancel();
            WidgetsBinding.instance.addPostFrameCallback((_) {
              if (mounted) {
                setState(() {
                  takingPicture = false;
                  isWaitingToTakePhoto = false;
                });
              }
            });
          }

          // Centered dot color
          var centeredDotColor = deviceInCorrectPosition == true
              ? Colors.white.withValues(alpha: 0.7)
              : Colors.transparent;

          //  Helper dot color depending on device rotation
          var helperDotColor =
              deviceInCorrectPosition == true ? Colors.white : Colors.red;

          return Container(
            color: Colors.black,
            child: morePhotosNeeded()
                ? Stack(
                    children: [
                      Positioned.fill(
                        child: FullScreenCameraPreview(controller: controller),
                      ),
                      Column(
                        mainAxisAlignment: MainAxisAlignment.end,
                        children: [
                          widget.cameraSelectorShow == true &&
                                  !_hasWideOrUltraWideCamera
                              ? CameraSelector(
                                  cameras: cameras,
                                  selectedCameraKey: selectedCameraKey,
                                  infoPopUpContent:
                                      widget.cameraSelectorInfoPopUpContent,
                                  infoPopUpShow:
                                      widget.cameraSelectorInfoPopUpShow,
                                  onCameraChanged: (cameraKey) {
                                    selectCamera(cameraKey);
                                  })
                              : Container(),
                          // Reset
                          ElevatedButton(
                              onPressed: () =>
                                  restartApp(reason: "Reset button hit"),
                              child: const Text("reset")),
                        ],
                      ),
                      // Helper Text for the first image
                      HelperText(
                        shown: capturedImages.isEmpty &&
                            (helperDotVerticalInPos == false ||
                                helperDotIsHorizontalInPos == false),
                        helperText: helperText,
                      ),
                      // Display tilt helper text
                      HelperText(
                        shown: helperDotVerticalInPos == true &&
                            helperDotIsHorizontalInPos == true &&
                            isDeviceRotationCorrect == false,
                        helperText: checkLeftDeviceRotation(deviceRotationDeg)
                            ? helperTiltLeftText
                            : helperTiltRightText,
                      ),
                      // Displays dots to help the user orientate
                      OrientationHelpers(
                        helperDotPosX: helperDotPosX,
                        helperDotPosY: helperDotPosY,
                        helperDotRadius: helperDotRadius,
                        helperDotColor: helperDotColor,
                        centeredDotPosX: centeredDotPosX,
                        centeredDotRadius: centeredDotRadius,
                        centeredDotPosY: centeredDotPosY,
                        centeredDotBorder: centeredDotBorder,
                        centeredDotColor: centeredDotColor,
                        deviceInCorrectPosition: deviceInCorrectPosition,
                        isDeviceRotationCorrect: isDeviceRotationCorrect,
                        deviceRotationDeg: deviceRotationDeg,
                        isWaitingToTakePhoto: isWaitingToTakePhoto,
                        timeToWaitBeforeTakingPicture:
                            timeToWaitBeforeTakingPicture,
                      ),
                    ],
                  )
                : _buildStitchOverlay(context),
          );
        },
      );
    }
  }
}
