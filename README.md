# 360 Camera

This plugin allows users to take 360° panoramic images using the phone's camera. OpenCV stitches the frames together on-device using SIFT, ORB, AKAZE, or BRISK feature matching. Alternatively, a SuperPoint + LightGlue ONNX pipeline can be used for higher-quality feature matching.

A simple UI guides the user through capture by showing orientation dots on screen.

### Demo

Checkout [Tridiar - Virtual Tours](https://tridiar.com) on [iOS](https://apps.apple.com/us/app/tridiar/id1593706196) and [Android](https://play.google.com/store/apps/details?id=com.tridiar.agent)

![Demo](./doc/demo.gif)

## Installation

### iOS

ONNX Runtime requires minimum version `iOS 16` and static linkage.

In `ios/Podfile`, change the following lines:

```bash
platform :ios, '16.0'

# existing code ...

use_frameworks! :linkage => :static

# existing code ...
```

Add these lines to `Info.plist`:

```plist
<key>NSCameraUsageDescription</key>
<string>This application needs access to your Camera in order to capture 360 Images</string>
<key>NSMicrophoneUsageDescription</key>
<string>This application needs access to your Microphone in order to capture videos</string>
<key>NSPhotoLibraryUsageDescription</key>
<string>This application needs access to your Photo Library in order to save the captured images</string>
```

### Android

Set the minimum SDK to 21 (or higher) in `android/app/build.gradle`:

```properties
minSdkVersion 21
```

In `AndroidManifest.xml`:

```xml
<uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" />
```

Android build requires `proguard-rules.pro` inside your Android project at `android/app/` with the following content:

```
-keep class ai.onnxruntime.** { *; }
```

or running the below command from your terminal:

```bash
echo "-keep class ai.onnxruntime.** { *; }" > android/app/proguard-rules.pro
```

### Dart

```dart
import 'package:camera_360/camera_360.dart';

Camera360(
  // ── Capture settings ────────────────────────────────────────────────────────
  photoCount: 16,                  // frames per 360° rotation
  captureVerticalDeg: 90,          // 90 = horizon, < 90 = tilted down, > 90 = tilted up
  loadingText: 'Preparing panorama...',
  helperText: 'Point the camera at the dot',
  tiltLeftText: 'Tilt left',
  tiltRightText: 'Tilt right',

  // ── Camera selector UI ──────────────────────────────────────────────────────
  cameraSelectorShow: true,
  cameraSelectorInfoPopUpShow: true,

  // ── Stitching (native OpenCV pipeline) ─────────────────────────────────────
  stitchModel: StitchFeatureModel.sift,
  stitchLoop360: true,
  stitchCrop: StitchCropMode.inner,
  stitchFallback: true,
  stitchFill: true,
  stitchFillRatio: 2.0,
  stitchFillSource: 30,
  stitchFillBlur: 401,
  stitchFillFade: 50,
  stitchFillBlurRatio: 0.01,
  stitchFillFadePower: 5.0,

  onCaptureEnded: (data) {
    // { 'success': bool, 'panorama': XFile?, 'options': { ... } }
    if (data['success'] == true) {
      final XFile panorama = data['panorama'];
      print('Panorama saved: ${panorama.path}');
    }
  },
  onProgressChanged: (percent) => print('Progress: $percent%'),
),
```

> `onCaptureEnded` returns an `XFile` on success, or `null` if stitching failed.

## Capture options

| Parameter             | Type      | Default                         | Description                                                        |
| --------------------- | --------- | ------------------------------- | ------------------------------------------------------------------ |
| `photoCount`          | `int?`    | `16`                            | Number of frames captured in a full 360° rotation                  |
| `captureVerticalDeg`  | `double`  | `90`                            | Vertical angle the device should be held at (0..180; 90 = horizon) |
| `captureImageWidth`   | `int?`    | `1000`                          | Width each captured frame is resized to before stitching           |
| `captureImageQuality` | `int?`    | `50`                            | JPEG quality for frame resize (1–100)                              |
| `loadingText`         | `String?` | `'Preparing panorama...'`       | Overlay text shown while stitching                                 |
| `helperText`          | `String?` | `'Point the camera at the dot'` | Instruction text during capture                                    |
| `tiltLeftText`        | `String?` | `'Tilt left'`                   | Text shown when the device needs tilting left                      |
| `tiltRightText`       | `String?` | `'Tilt right'`                  | Text shown when the device needs tilting right                     |

## Stitching options

All options below are passed to the native `stitch()` function.

| Parameter             | Type                 | Default | Description                                                                                         |
| --------------------- | -------------------- | ------- | --------------------------------------------------------------------------------------------------- |
| `stitchModel`         | `StitchFeatureModel` | `sift`  | Feature detector: `sift`, `orb`, `akaze`, `brisk`, or `onnx`                                        |
| `onnxModelAssetKey`   | `String?`            | `null`  | Flutter asset key for the SuperPoint + LightGlue ONNX model (required when `stitchModel` is `onnx`) |
| `stitchLoop360`       | `bool`               | `true`  | Match last frame to first for a closed 360° loop                                                    |
| `stitchCrop`          | `StitchCropMode`     | `inner` | `none` — full canvas; `bbox` — tight bounding box; `inner` — largest rectangle with no black wedges |
| `stitchFallback`      | `bool`               | `true`  | Synthesize homographies for failed adjacent pairs                                                   |
| `stitchFill`          | `bool`               | `true`  | Pad top/bottom with blurred edge fill                                                               |
| `stitchFillRatio`     | `double`             | `2.0`   | Target width/height when fill is enabled (e.g. `2.0` = 2:1)                                         |
| `stitchFillSource`    | `int`                | `30`    | Rows sampled from each edge for the blur profile                                                    |
| `stitchFillBlur`      | `int`                | `401`   | Horizontal Gaussian kernel (odd, ≥ 3)                                                               |
| `stitchFillFade`      | `int`                | `50`    | Pano rows at the join blended with the edge profile                                                 |
| `stitchFillBlurRatio` | `double`             | `0.01`  | Fraction of fill band that stays pure blur                                                          |
| `stitchFillFadePower` | `double`             | `5.0`   | Fade steepness toward flat colour (`1.0` = linear)                                                  |

`stitchCrop` and `stitchFill` are independent: e.g. `stitchCrop: StitchCropMode.none` with `stitchFill: true` still fills black wedges using a per-column boundary.

## SuperPoint + LightGlue ONNX feature matching

For higher-quality panoramas (especially in low-texture scenes) you can use a SuperPoint + LightGlue ONNX pipeline instead of OpenCV's built-in detectors.

### Setup

The model is not bundled with the plugin — you need to include it in your app's assets. Add it to your `pubspec.yaml`:

You can download the modal from : https://github.com/fabio-sim/LightGlue-ONNX/releases/tag/v2.0 (superpoint_lightglue_pipeline.ort.onnx
)

```yaml
flutter:
  assets:
    - models/
```

Place the model file (e.g. `superpoint_lightglue_pipeline.ort.onnx`) in `<your_app>/models/`.

### Usage

```dart
Camera360(
  stitchModel: StitchFeatureModel.onnx,
  onnxModelAssetKey: 'models/superpoint_lightglue_pipeline.ort.onnx',
  // ...other options
),
```

The plugin automatically copies the bundled asset to a writable directory on first use (skipping the copy on subsequent runs when the file is unchanged). If the asset key cannot be found, a descriptive error is thrown.

## Direct stitching API

You can also stitch a list of images outside the capture UI:

```dart
import 'package:camera_360/camera_360.dart';

final panorama = await Stitcher.stitchImages(
  imageFiles,
  model: StitchFeatureModel.sift,
  crop: StitchCropMode.inner,
  fill: true,
  fillRatio: 2.0,
);
```

Stitching runs in a background isolate so the UI stays responsive.

## Example app

The `example/` app includes a **Stitch** button that opens a panel to change all stitching options before capture.

```bash
cd example && flutter run
```

## Developer documentation

Native C++ development (macOS test harness, CMake, CLI flags): [doc/developer.md](doc/developer.md)

Regenerate FFI bindings after changing `src/umbrella/Stitcher/Stitcher.h`:

```bash
flutter pub run ffigen --config ffigen.yaml
```
