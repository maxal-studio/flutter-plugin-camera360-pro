import 'package:camera_360/camera_360.dart';
import 'package:panoramai/widgets/option_controls.dart';
import 'package:flutter/material.dart';

class CaptureOptions {
  const CaptureOptions({
    required this.nrPhotos,
    required this.verticalDeg,
    required this.debug,
    required this.saveCapturedImages,
    required this.debugInfoPosition,
  });

  final int nrPhotos;
  final double verticalDeg;
  final bool debug;
  final bool saveCapturedImages;
  final DebugInfoPosition debugInfoPosition;
}

class CaptureOptionsPanel extends StatefulWidget {
  const CaptureOptionsPanel({
    super.key,
    required this.nrPhotos,
    required this.verticalDeg,
    required this.debug,
    required this.saveCapturedImages,
    required this.debugInfoPosition,
    required this.onChanged,
  });

  final int nrPhotos;
  final double verticalDeg;
  final bool debug;
  final bool saveCapturedImages;
  final DebugInfoPosition debugInfoPosition;
  final ValueChanged<CaptureOptions> onChanged;

  @override
  State<CaptureOptionsPanel> createState() => _CaptureOptionsPanelState();
}

class _CaptureOptionsPanelState extends State<CaptureOptionsPanel> {
  late int _nrPhotos;
  late double _verticalDeg;
  late bool _debug;
  late bool _saveCapturedImages;
  late DebugInfoAlignment _debugAlignment;

  /// Slider increment for vertical camera angle (developer-tuned).
  static const double _verticalDegStep = 5;

  static const _debugAlignmentOptions = DebugInfoAlignment.values;

  @override
  void initState() {
    super.initState();
    _syncFromWidget();
  }

  @override
  void didUpdateWidget(covariant CaptureOptionsPanel oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.nrPhotos != widget.nrPhotos ||
        oldWidget.verticalDeg != widget.verticalDeg ||
        oldWidget.debug != widget.debug ||
        oldWidget.saveCapturedImages != widget.saveCapturedImages ||
        oldWidget.debugInfoPosition.alignment !=
            widget.debugInfoPosition.alignment) {
      _syncFromWidget();
    }
  }

  void _syncFromWidget() {
    _nrPhotos = widget.nrPhotos;
    _verticalDeg = widget.verticalDeg;
    _debug = widget.debug;
    _saveCapturedImages = widget.saveCapturedImages;
    _debugAlignment = widget.debugInfoPosition.alignment;
  }

  DebugInfoPosition _debugInfoPositionFromAlignment(DebugInfoAlignment value) {
    return switch (value) {
      DebugInfoAlignment.topLeft => DebugInfoPosition.topLeft,
      DebugInfoAlignment.topRight => DebugInfoPosition.topRight,
      DebugInfoAlignment.bottomLeft => DebugInfoPosition.bottomLeft,
      DebugInfoAlignment.bottomRight => DebugInfoPosition.bottomRight,
    };
  }

  String _debugAlignmentLabel(DebugInfoAlignment value) {
    return switch (value) {
      DebugInfoAlignment.topLeft => 'Top left',
      DebugInfoAlignment.topRight => 'Top right',
      DebugInfoAlignment.bottomLeft => 'Bottom left',
      DebugInfoAlignment.bottomRight => 'Bottom right',
    };
  }

  void _notify() {
    widget.onChanged(
      CaptureOptions(
        nrPhotos: _nrPhotos,
        verticalDeg: _verticalDeg,
        debug: _debug,
        saveCapturedImages: _saveCapturedImages,
        debugInfoPosition: _debugInfoPositionFromAlignment(_debugAlignment),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(8),
      child: Material(
        color: Colors.black.withValues(alpha: 0.85),
        borderRadius: BorderRadius.circular(12),
        child: ListView(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
          children: [
            const Text(
              'Capture options',
              style: TextStyle(
                color: Colors.white,
                fontSize: 16,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 4),
            Text(
              'Changing any value reinitializes the camera widget.',
              style: TextStyle(color: Colors.grey.shade400, fontSize: 12),
            ),
            const SizedBox(height: 12),
            optionIntSlider(
              label: 'Photos per panorama',
              value: _nrPhotos,
              min: 8,
              max: 32,
              onChanged: (v) {
                setState(() => _nrPhotos = v);
                _notify();
              },
            ),
            optionDegSlider(
              label: 'Vertical camera angle',
              value: _verticalDeg,
              min: 0,
              max: 180,
              step: _verticalDegStep,
              hint: '90° = horizon, < 90° = tilted down, > 90° = tilted up',
              onChanged: (v) {
                setState(() => _verticalDeg = v);
                _notify();
              },
            ),
            const SizedBox(height: 8),
            const Text(
              'Debug & storage',
              style: TextStyle(
                color: Colors.white,
                fontSize: 14,
                fontWeight: FontWeight.w600,
              ),
            ),
            optionSwitchTile(
              'Show orientation info',
              _debug,
              (v) {
                setState(() => _debug = v);
                _notify();
              },
              subtitle:
                  'Live horizontal, target, vertical, and tilt readouts during capture.',
            ),
            if (_debug)
              optionDropdown<DebugInfoAlignment>(
                label: 'Info position',
                value: _debugAlignment,
                items: _debugAlignmentOptions,
                labelBuilder: _debugAlignmentLabel,
                onChanged: (v) {
                  setState(() => _debugAlignment = v);
                  _notify();
                },
              ),
            optionSwitchTile(
              'Save captures to gallery',
              _saveCapturedImages,
              (v) {
                setState(() => _saveCapturedImages = v);
                _notify();
              },
              subtitle:
                  'Each frame is copied to the photo library; cache is still cleared after stitch.',
            ),
          ],
        ),
      ),
    );
  }
}
