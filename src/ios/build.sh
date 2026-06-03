#!/bin/bash

set -euo pipefail

WD="$(cd "$(dirname "$0")" && pwd)"
IOS_DIR="$WD/../../ios"
INSTALLDIR="$WD/build"
STUB_BUILD="$WD/stub-build"
CONFIG=Release
export IPHONEOS_DEPLOYMENT_TARGET=12.0
OPENCV_TOOLCHAIN="$WD/opencv-build/opencv-4.12.0/platforms/ios/cmake/Toolchains/Toolchain-iPhoneOS_Xcode.cmake"
STUB_SOURCE="$IOS_DIR/stub/stitcher_stub.c"
STUB_INFO_PLIST="$IOS_DIR/stub/Info.plist"
PUBLIC_HEADER="$WD/../umbrella/Stitcher/Stitcher.h"

require_file() {
  if [ ! -f "$1" ]; then
    echo "ERROR: required file not found: $1" >&2
    exit 1
  fi
}

build_device_framework() {
  echo "*********** Building device framework ($CONFIG)"
  rm -rf "$INSTALLDIR"
  mkdir "$INSTALLDIR"

  pushd "$INSTALLDIR" >/dev/null
  cmake ../ \
    -DCMAKE_TOOLCHAIN_FILE="$OPENCV_TOOLCHAIN" \
    -DIOS_ARCH=arm64 \
    -DIPHONEOS_DEPLOYMENT_TARGET="$IPHONEOS_DEPLOYMENT_TARGET" \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_CONFIGURATION_TYPES="$CONFIG" \
    -GXcode

  xcodebuild \
    -project camera_360.xcodeproj \
    -target camera_360 \
    -configuration "$CONFIG" \
    -sdk iphoneos \
    ARCHS=arm64 \
    DEBUG_INFORMATION_FORMAT=dwarf-with-dsym \
    BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
    ONLY_ACTIVE_ARCH=NO \
    COPY_PHASE_STRIP=NO \
    STRIP_INSTALLED_PRODUCT=NO

  DEVICE_FW="$INSTALLDIR/Release-iphoneos/camera_360.framework"
  require_file "$DEVICE_FW/camera_360"

  local DEVICE_DSYM_BUNDLE="$INSTALLDIR/Release-iphoneos/camera_360.framework.dSYM"
  rm -rf "$DEVICE_DSYM_BUNDLE"
  dsymutil "$DEVICE_FW/camera_360" -o "$DEVICE_DSYM_BUNDLE"

  DEVICE_DSYM="$DEVICE_DSYM_BUNDLE/Contents/Resources/DWARF/camera_360"
  popd >/dev/null
}

build_simulator_stub_framework() {
  echo "*********** Building simulator stub framework"
  require_file "$STUB_SOURCE"
  require_file "$STUB_INFO_PLIST"
  require_file "$PUBLIC_HEADER"

  rm -rf "$STUB_BUILD"
  local OUT_FW="$STUB_BUILD/camera_360.framework"
  mkdir -p "$OUT_FW/Headers"

  cp "$PUBLIC_HEADER" "$OUT_FW/Headers/Stitcher.h"
  cp "$STUB_INFO_PLIST" "$OUT_FW/Info.plist"

  local SDK
  SDK="$(xcrun --sdk iphonesimulator --show-sdk-path)"
  local MIN_VERSION="$IPHONEOS_DEPLOYMENT_TARGET"

  clang -dynamiclib \
    -arch arm64 -arch x86_64 \
    -isysroot "$SDK" \
    -mios-simulator-version-min="$MIN_VERSION" \
    -install_name "@rpath/camera_360.framework/camera_360" \
    "$STUB_SOURCE" \
    -o "$OUT_FW/camera_360"

  SIM_FW="$OUT_FW"
  require_file "$SIM_FW/camera_360"
}

create_xcframework() {
  echo "*********** Creating xcframework"
  rm -rf "$IOS_DIR/camera_360.xcframework"
  rm -rf "$IOS_DIR/camera_360.framework"

  local DEVICE_DSYM_BUNDLE="$INSTALLDIR/Release-iphoneos/camera_360.framework.dSYM"
  require_file "$DEVICE_DSYM_BUNDLE/Contents/Resources/DWARF/camera_360"
  DEVICE_DSYM_BUNDLE="$(cd "$(dirname "$DEVICE_DSYM_BUNDLE")" && pwd)/$(basename "$DEVICE_DSYM_BUNDLE")"

  xcodebuild -create-xcframework \
    -framework "$DEVICE_FW" \
    -debug-symbols "$DEVICE_DSYM_BUNDLE" \
    -framework "$SIM_FW" \
    -output "$IOS_DIR/camera_360.xcframework"

  echo "*********** Verifying xcframework"
  dwarfdump --uuid "$IOS_DIR/camera_360.xcframework/ios-arm64/camera_360.framework/camera_360"
  dwarfdump --uuid "$IOS_DIR/camera_360.xcframework/ios-arm64/dSYMs/camera_360.framework.dSYM"
  lipo -info "$IOS_DIR/camera_360.xcframework/ios-arm64_x86_64-simulator/camera_360.framework/camera_360"
  plutil -extract AvailableLibraries json -o - "$IOS_DIR/camera_360.xcframework/Info.plist" | grep -i DebugSymbolsPath || {
    echo "ERROR: xcframework is missing DebugSymbolsPath for the device slice" >&2
    exit 1
  }
  echo "*********** COMPLETED -> $IOS_DIR/camera_360.xcframework"
}

require_file "$OPENCV_TOOLCHAIN"
build_device_framework
build_simulator_stub_framework
create_xcframework
