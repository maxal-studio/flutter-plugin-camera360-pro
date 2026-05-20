import 'package:camera/camera.dart';
import 'package:flutter/material.dart';

/// Scales [CameraPreview] to cover its parent (BoxFit.cover), regardless of
/// resolution preset or sensor aspect ratio.
class FullScreenCameraPreview extends StatelessWidget {
  final CameraController controller;

  const FullScreenCameraPreview({
    super.key,
    required this.controller,
  });

  @override
  Widget build(BuildContext context) {
    if (!controller.value.isInitialized) {
      return const ColoredBox(color: Colors.black);
    }

    return LayoutBuilder(
      builder: (BuildContext context, BoxConstraints constraints) {
        final Size size = constraints.biggest;
        if (size.isEmpty) {
          return CameraPreview(controller);
        }

        var scale = size.aspectRatio * controller.value.aspectRatio;
        if (scale < 1.0) {
          scale = 1.0 / scale;
        }

        return ClipRect(
          child: Center(
            child: Transform.scale(
              scale: scale,
              child: CameraPreview(controller),
            ),
          ),
        );
      },
    );
  }
}
