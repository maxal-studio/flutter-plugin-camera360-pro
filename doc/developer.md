# - Developer Documentation

# Debuging vs Release (Android)

On Android while debuging under `android/build.gardle` uncomment the lines after the comment `//For debuging` and comment out lines after the comment `//For release`

### Install cmake and ninja

```bash
 brew install cmake
 brew unlink cmake && brew link cmake
 brew install ninja
```

> For the Android `SDK` and `NDK`, define the following paths as environment variables. Typically, you add them to your shell config file, e.g., `~/.bashrc` or `~/.zshrc`. Make sure your NDK version is newer than `23.0.75`.
> Then run source to read the new comands into the shell `source ~/.zshrc` or `source ~/.bashrc`
>
> To install the SDK and NDK use Android Studio
> Open `Android Studio Preference` (or "File->Settings") > `Appearance & Behavior` >`System Settings` > `Android SDK`.
> On the next tab: `SDK Tools` , you can check if NDK is installed

> Update `26.0.10792818` with your NDK version

```bash
export ANDROID_HOME=/$HOME/Library/Android/sdk
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk
export ANDROID_NDK=$ANDROID_NDK_HOME
export NDK=$ANDROID_HOME/ndk/26.0.10792818
```

### First we need to download and prepare OpenCV for both platforms

```properties
cd /src
sh prebuild.sh
```

### Now we need to build the OpenCV Framework for iOS and Android

## iOS

```properties
cd /ios/
sh build.sh
```

### And the last step is to generate the Binding and install PODS

From the main dir of the plugin

```properties
sh build.sh
```

## macOS local stitcher test (`test_macos`)

Use the standalone `test_macos` harness to build and run the umbrella stitcher on your Mac without Flutter or a device. It compiles the same C++ sources as the plugin (`src/umbrella/Stitcher/`) into a command-line executable and calls the `stitch()` entry point from [Stitcher.h](../src/umbrella/Stitcher/Stitcher.h).

### Prerequisites

- **CMake** and a C++17 compiler (Xcode Command Line Tools on macOS).
- **OpenCV** with stitching support. On Apple Silicon, Homebrew is the usual install:

```bash
brew install cmake opencv
```

CMake looks for OpenCV at `/opt/homebrew/opt/opencv/lib/cmake/opencv4` first, then falls back to `find_package(OpenCV)`.

### Configure and build

From the **repository root**:

```bash
cmake -S test_macos -B test_macos/build -DCMAKE_BUILD_TYPE=Release
cmake --build test_macos/build -j$(sysctl -n hw.logicalcpu)
```

The binary is `test_macos/build/stitch_test`.

### Run (defaults)

Run from the repo root so the default input path resolves:

```bash
./test_macos/build/stitch_test
```

Defaults:

| Option       | Default                                                                                                |
| ------------ | ------------------------------------------------------------------------------------------------------ |
| Input folder | `./input/bathroom` (all `.jpg` / `.jpeg` / `.png`, sorted numerically by filename: `1.jpg` … `16.jpg`) |
| Output file  | `./output/panorama_out.jpg` (written to the current working directory)                                 |
| Model        | `sift` (`sift` \| `orb` \| `akaze` \| `brisk`)                                                         |
| Crop         | `inner` (`none` \| `bbox` \| `inner`)                                                                  |
| Fill         | enabled (per-column blur → flat top/bottom, works with any crop mode)                                  |
| Loop 360     | enabled                                                                                                |
| Fallback     | enabled                                                                                                |
| Fill params  | ratio `2.0`, source `30`, blur `401`, fade `50`, blur-ratio `0.01`, fade-power `5.0`                   |

The harness prints stage timings from the native stitcher (match, compose, crop, fill, etc.) and exits `0` on success, `2` on stitching failure.

### CLI overrides

```bash
./test_macos/build/stitch_test \
  --input test_macos/input/bathroom \
  --out   test_macos/output/bathroom.jpg \
  --model orb \
  --crop  none \
  --fill \
  --no-loop \
  --no-fallback \
  --fill-ratio 2.0 \
  --fill-source 30 \
  --fill-blur 401 \
  --fill-blur-ratio 0.01 \
  --fill-fade 50 \
  --fill-fade-power 5.0
```

| Flag                  | Description                                                                                |
| --------------------- | ------------------------------------------------------------------------------------------ |
| `--input DIR`         | Folder of images (not a `image_list.txt`; files are discovered and sorted by numeric stem) |
| `--out FILE`          | Output panorama path                                                                       |
| `--model NAME`        | OpenCV detector: `sift`, `orb`, `akaze`, `brisk`                                           |
| `--crop MODE`         | `none`, `bbox`, `inner` (was `fill` — that is now the separate `--fill` flag)              |
| `--no-loop`           | Do not match the closing pair (last ↔ first)                                               |
| `--no-loop-closure`   | Match the closing pair but skip wrap-seam closure (ring canvas, yaw closure, etc.)         |
| `--no-fallback`       | Disable homography fallback for failed adjacent pairs                                      |
| `--fill`              | Run the per-column blur → flat top/bottom fill (default). Works with every crop mode.      |
| `--no-fill`           | Skip the fill step entirely                                                                |
| `--fill-ratio R`      | Target W/H for the post-fill canvas                                                        |
| `--fill-source N`     | Rows sampled from each edge for the fill profile                                           |
| `--fill-blur K`       | Horizontal Gaussian kernel (odd, ≥ 3)                                                      |
| `--fill-blur-ratio R` | Blur fraction in `[0, 1]` before fade to flat colour                                       |
| `--fill-fade N`       | Pano rows blended at the join                                                              |
| `--fill-fade-power P` | Stacked flat-colour layers in the fade (≥ 1.0)                                             |
| `-h`, `--help`        | Show usage                                                                                 |

`--crop` and `--fill` are now independent. `--fill` works even with `--crop none` or `--crop bbox`: the fill ramp's "join" row is per-column and follows the actual content boundary, so black wedges along the top/bottom are smoothly replaced by the same blur → flat-colour profile.

### Examples with the bathroom dataset

Default behaviour (inner crop + fill, equivalent to the historical `--crop fill`):

```bash
./test_macos/build/stitch_test \
  --input test_macos/input/bathroom \
  --out   test_macos/output/bathroom.jpg
```

Keep the full uncropped canvas and let `--fill` paint over the irregular black wedges:

```bash
./test_macos/build/stitch_test \
  --input test_macos/input/bathroom \
  --out   test_macos/output/bathroom_none_fill.jpg \
  --crop  none \
  --fill
```

### Layout

```
test_macos/
  CMakeLists.txt    # Standalone CMake project (executable only)
  main.cpp          # CLI + image discovery → stitch()
  build/            # Local build output (gitignored if you add it)
```

Sources are not duplicated: `CMakeLists.txt` links `../src/umbrella/Stitcher/*.cpp` directly.

# Developer Reference links

1. [Scanbot.io](https://scanbot.io/blog/implementing-a-flutter-plugin-with-native-opencv-support-via-dartffi-part-1-2/)
2. [Configure CMAKE for OpenCV](https://docs.opencv.org/4.x/db/d05/tutorial_config_reference.html)
3. [Configure CMAKE for OpenCV - Opmtimized](https://github.com/opencv/opencv/wiki/Compact-build-advice)
