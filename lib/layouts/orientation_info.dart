import 'package:flutter/material.dart';

/// Screen corner / edge placement for the [OrientationInfo] debug overlay.
enum DebugInfoAlignment {
  topLeft,
  topRight,
  bottomLeft,
  bottomRight,
}

/// Configures where the orientation debug overlay is drawn during capture.
class DebugInfoPosition {
  /// Top-left corner with 8 px inset on all sides.
  static const topLeft = DebugInfoPosition();

  /// Top-right corner with 8 px inset on all sides.
  static const topRight = DebugInfoPosition(
    alignment: DebugInfoAlignment.topRight,
  );

  /// Bottom-left corner with 8 px inset on all sides.
  static const bottomLeft = DebugInfoPosition(
    alignment: DebugInfoAlignment.bottomLeft,
  );

  /// Bottom-right corner with 8 px inset on all sides.
  static const bottomRight = DebugInfoPosition(
    alignment: DebugInfoAlignment.bottomRight,
  );

  const DebugInfoPosition({
    this.alignment = DebugInfoAlignment.topLeft,
    this.padding = const EdgeInsets.all(8),
  });

  /// Which corner of the capture view hosts the overlay.
  final DebugInfoAlignment alignment;

  /// Inset from the chosen corner (e.g. `EdgeInsets.only(top: 16, left: 12)`).
  final EdgeInsets padding;

  Alignment get _flutterAlignment => switch (alignment) {
        DebugInfoAlignment.topLeft => Alignment.topLeft,
        DebugInfoAlignment.topRight => Alignment.topRight,
        DebugInfoAlignment.bottomLeft => Alignment.bottomLeft,
        DebugInfoAlignment.bottomRight => Alignment.bottomRight,
      };

  /// Wraps [child] inside a [Stack] using [alignment] and [padding].
  Widget position(Widget child) {
    return Align(
      alignment: _flutterAlignment,
      child: Padding(
        padding: padding,
        child: child,
      ),
    );
  }
}

/// Debug overlay showing live device orientation readings during capture.
class OrientationInfo extends StatelessWidget {
  final double horizontalDeg;
  final double targetHorizontalDeg;
  final double verticalDeg;
  final double tiltDeg;

  const OrientationInfo({
    super.key,
    required this.horizontalDeg,
    required this.targetHorizontalDeg,
    required this.verticalDeg,
    required this.tiltDeg,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      color: Colors.black.withValues(alpha: 0.65),
      padding: const EdgeInsets.all(8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            'Horizontal: ${horizontalDeg.toStringAsFixed(1)}°',
            style: const TextStyle(color: Colors.white, fontSize: 12),
          ),
          Text(
            'Target H: ${targetHorizontalDeg.toStringAsFixed(1)}°',
            style: const TextStyle(color: Colors.white, fontSize: 12),
          ),
          Text(
            'Vertical: ${verticalDeg.toStringAsFixed(1)}°',
            style: const TextStyle(color: Colors.white, fontSize: 12),
          ),
          Text(
            'Tilt: ${tiltDeg.toStringAsFixed(1)}°',
            style: const TextStyle(color: Colors.white, fontSize: 12),
          ),
        ],
      ),
    );
  }
}
