import 'dart:ffi';
import 'dart:io';

/// Opens the camera_360 native library for Dart FFI.
///
/// On iOS the stitcher is vendored as an embedded dynamic framework. Its C
/// symbols are not visible through [DynamicLibrary.process] until the framework
/// is loaded explicitly.
DynamicLibrary openCamera360Library() {
  if (Platform.isAndroid) {
    return DynamicLibrary.open('libcamera_360.so');
  }
  if (Platform.isIOS) {
    return DynamicLibrary.open('camera_360.framework/camera_360');
  }
  return DynamicLibrary.process();
}
