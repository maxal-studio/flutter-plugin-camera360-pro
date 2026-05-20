import 'package:camera_360/camera_360.dart';
import 'preview.dart';
import 'package:flutter/material.dart';
import 'package:image_picker/image_picker.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(
          title: const Text('360 Camera App'),
        ),
        body: const CameraPage(),
      ),
    );
  }
}

class CameraPage extends StatefulWidget {
  const CameraPage({super.key});

  @override
  State<CameraPage> createState() => _CameraPageState();
}

class _CameraPageState extends State<CameraPage> {
  int progressPercentage = 0;
  bool _showStitchMenu = false;
  bool _showCaptureMenu = false;

  int _captureNrPhotos = 8;
  double _captureVerticalDeg = 80;

  StitchFeatureModel _stitchModel = StitchFeatureModel.onnx;
  StitchCropMode _stitchCrop = StitchCropMode.none;
  bool _stitchLoop360 = true;
  bool _stitchFallback = true;
  bool _stitchFill = true;
  double _stitchFillRatio = 2.0;
  int _stitchFillSource = 30;
  int _stitchFillBlur = 401;
  int _stitchFillFade = 50;
  double _stitchFillBlurRatio = 0.01;
  double _stitchFillFadePower = 5.0;

  void _displayPanoramaMessage(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(message)),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [
        Camera360(
          key: ValueKey('capture-$_captureNrPhotos-$_captureVerticalDeg'),
          photoCount: _captureNrPhotos,
          captureVerticalDeg: _captureVerticalDeg,
          loadingText: 'Preparing panorama...',
          helperText: 'Point the camera at the dot',
          tiltLeftText: 'Tilt left',
          tiltRightText: 'Tilt right',
          cameraSelectorShow: true,
          cameraSelectorInfoPopUpShow: true,
          cameraSelectorInfoPopUpContent: const Column(
            children: [
              Padding(
                padding: EdgeInsets.only(bottom: 10),
                child: Text(
                  'Notice: This feature only works if your phone has a wide angle camera.',
                  textAlign: TextAlign.center,
                  style: TextStyle(color: Color(0xffDB4A3C)),
                ),
              ),
              Text(
                'Select the camera with the widest viewing angle below.',
                textAlign: TextAlign.center,
                style: TextStyle(color: Color(0xffEFEFEF)),
              ),
            ],
          ),
          stitchModel: _stitchModel,
          onnxModelAssetKey: 'models/superpoint_lightglue_pipeline.ort.onnx',
          stitchLoop360: _stitchLoop360,
          stitchCrop: _stitchCrop,
          stitchFallback: _stitchFallback,
          stitchFill: _stitchFill,
          stitchFillRatio: _stitchFillRatio,
          stitchFillSource: _stitchFillSource,
          stitchFillBlur: _stitchFillBlur,
          stitchFillFade: _stitchFillFade,
          stitchFillBlurRatio: _stitchFillBlurRatio,
          stitchFillFadePower: _stitchFillFadePower,
          onCaptureEnded: (data) {
            if (data['success'] == true) {
              final XFile panorama = data['panorama'];
              Navigator.of(context).push(
                MaterialPageRoute<void>(
                  builder: (context) => PanoramaPreviewScreen(
                    panoramaPath: panorama.path,
                  ),
                ),
              );
            } else {
              _displayPanoramaMessage('Panorama failed!');
            }
          },
          onCameraChanged: (cameraKey) {
            _displayPanoramaMessage('Camera changed $cameraKey');
          },
          onProgressChanged: (newProgressPercentage) {
            setState(() {
              progressPercentage = newProgressPercentage;
            });
          },
        ),
        SafeArea(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(8, 8, 8, 0),
                child: Row(
                  children: [
                    Expanded(
                      child: Text(
                        'Progress: $progressPercentage%',
                        style: const TextStyle(
                          color: Colors.white,
                          backgroundColor: Colors.black54,
                        ),
                      ),
                    ),
                    _optionsMenuButton(
                      label: 'Capture',
                      icon: Icons.camera_outlined,
                      isOpen: _showCaptureMenu,
                      onTap: () => setState(() {
                        _showCaptureMenu = !_showCaptureMenu;
                        if (_showCaptureMenu) _showStitchMenu = false;
                      }),
                    ),
                    const SizedBox(width: 6),
                    _optionsMenuButton(
                      label: 'Stitch',
                      icon: Icons.tune,
                      isOpen: _showStitchMenu,
                      onTap: () => setState(() {
                        _showStitchMenu = !_showStitchMenu;
                        if (_showStitchMenu) _showCaptureMenu = false;
                      }),
                    ),
                  ],
                ),
              ),
              if (_showCaptureMenu)
                Expanded(
                  child: _CaptureOptionsPanel(
                    nrPhotos: _captureNrPhotos,
                    verticalDeg: _captureVerticalDeg,
                    onChanged: (options) => setState(() {
                      _captureNrPhotos = options.nrPhotos;
                      _captureVerticalDeg = options.verticalDeg;
                    }),
                  ),
                ),
              if (_showStitchMenu)
                Expanded(
                  child: _StitchOptionsPanel(
                    model: _stitchModel,
                    crop: _stitchCrop,
                    loop360: _stitchLoop360,
                    fallback: _stitchFallback,
                    fill: _stitchFill,
                    fillRatio: _stitchFillRatio,
                    fillSource: _stitchFillSource,
                    fillBlur: _stitchFillBlur,
                    fillFade: _stitchFillFade,
                    fillBlurRatio: _stitchFillBlurRatio,
                    fillFadePower: _stitchFillFadePower,
                    onChanged: (options) => setState(() {
                      _stitchModel = options.model;
                      _stitchCrop = options.crop;
                      _stitchLoop360 = options.loop360;
                      _stitchFallback = options.fallback;
                      _stitchFill = options.fill;
                      _stitchFillRatio = options.fillRatio;
                      _stitchFillSource = options.fillSource;
                      _stitchFillBlur = options.fillBlur;
                      _stitchFillFade = options.fillFade;
                      _stitchFillBlurRatio = options.fillBlurRatio;
                      _stitchFillFadePower = options.fillFadePower;
                    }),
                  ),
                ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _optionsMenuButton({
    required String label,
    required IconData icon,
    required bool isOpen,
    required VoidCallback onTap,
  }) {
    return Material(
      color: Colors.black54,
      borderRadius: BorderRadius.circular(8),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(8),
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                isOpen ? Icons.close : icon,
                color: Colors.white,
                size: 20,
              ),
              const SizedBox(width: 6),
              Text(
                label,
                style: const TextStyle(
                  color: Colors.white,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _CaptureOptions {
  const _CaptureOptions({
    required this.nrPhotos,
    required this.verticalDeg,
  });

  final int nrPhotos;
  final double verticalDeg;
}

class _CaptureOptionsPanel extends StatefulWidget {
  const _CaptureOptionsPanel({
    required this.nrPhotos,
    required this.verticalDeg,
    required this.onChanged,
  });

  final int nrPhotos;
  final double verticalDeg;
  final ValueChanged<_CaptureOptions> onChanged;

  @override
  State<_CaptureOptionsPanel> createState() => _CaptureOptionsPanelState();
}

class _CaptureOptionsPanelState extends State<_CaptureOptionsPanel> {
  late int _nrPhotos;
  late double _verticalDeg;

  @override
  void initState() {
    super.initState();
    _syncFromWidget();
  }

  @override
  void didUpdateWidget(covariant _CaptureOptionsPanel oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.nrPhotos != widget.nrPhotos ||
        oldWidget.verticalDeg != widget.verticalDeg) {
      _syncFromWidget();
    }
  }

  void _syncFromWidget() {
    _nrPhotos = widget.nrPhotos;
    _verticalDeg = widget.verticalDeg;
  }

  void _notify() {
    widget.onChanged(
      _CaptureOptions(nrPhotos: _nrPhotos, verticalDeg: _verticalDeg),
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
              'Applied before capture starts; helper dots update live.',
              style: TextStyle(color: Colors.grey.shade400, fontSize: 12),
            ),
            const SizedBox(height: 12),
            _intSlider(
              label: 'Photos per panorama',
              value: _nrPhotos,
              min: 4,
              max: 32,
              onChanged: (v) {
                setState(() => _nrPhotos = v);
                _notify();
              },
            ),
            _sliderTile(
              label: 'Vertical camera angle',
              value: _verticalDeg,
              min: 0,
              max: 180,
              divisions: 180,
              display: '${_verticalDeg.round()}°',
              hint: '90° = horizon, < 90° = tilted down, > 90° = tilted up',
              onChanged: (v) {
                setState(() => _verticalDeg = v);
                _notify();
              },
            ),
          ],
        ),
      ),
    );
  }

  Widget _sliderTile({
    required String label,
    required double value,
    required double min,
    required double max,
    required int divisions,
    required String display,
    required ValueChanged<double> onChanged,
    String? hint,
  }) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(label, style: const TextStyle(color: Colors.white70)),
            Text(display, style: const TextStyle(color: Colors.white)),
          ],
        ),
        if (hint != null)
          Padding(
            padding: const EdgeInsets.only(bottom: 4),
            child: Text(
              hint,
              style: TextStyle(color: Colors.grey.shade500, fontSize: 11),
            ),
          ),
        Slider(
          value: value.clamp(min, max),
          min: min,
          max: max,
          divisions: divisions,
          onChanged: onChanged,
        ),
      ],
    );
  }

  Widget _intSlider({
    required String label,
    required int value,
    required int min,
    required int max,
    required ValueChanged<int> onChanged,
  }) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(label, style: const TextStyle(color: Colors.white70)),
            Text('$value', style: const TextStyle(color: Colors.white)),
          ],
        ),
        Slider(
          value: value.toDouble().clamp(min.toDouble(), max.toDouble()),
          min: min.toDouble(),
          max: max.toDouble(),
          divisions: max - min,
          onChanged: (v) => onChanged(v.round()),
        ),
      ],
    );
  }
}

class _StitchOptions {
  const _StitchOptions({
    required this.model,
    required this.crop,
    required this.loop360,
    required this.fallback,
    required this.fill,
    required this.fillRatio,
    required this.fillSource,
    required this.fillBlur,
    required this.fillFade,
    required this.fillBlurRatio,
    required this.fillFadePower,
  });

  final StitchFeatureModel model;
  final StitchCropMode crop;
  final bool loop360;
  final bool fallback;
  final bool fill;
  final double fillRatio;
  final int fillSource;
  final int fillBlur;
  final int fillFade;
  final double fillBlurRatio;
  final double fillFadePower;
}

class _StitchOptionsPanel extends StatefulWidget {
  const _StitchOptionsPanel({
    required this.model,
    required this.crop,
    required this.loop360,
    required this.fallback,
    required this.fill,
    required this.fillRatio,
    required this.fillSource,
    required this.fillBlur,
    required this.fillFade,
    required this.fillBlurRatio,
    required this.fillFadePower,
    required this.onChanged,
  });

  final StitchFeatureModel model;
  final StitchCropMode crop;
  final bool loop360;
  final bool fallback;
  final bool fill;
  final double fillRatio;
  final int fillSource;
  final int fillBlur;
  final int fillFade;
  final double fillBlurRatio;
  final double fillFadePower;
  final ValueChanged<_StitchOptions> onChanged;

  @override
  State<_StitchOptionsPanel> createState() => _StitchOptionsPanelState();
}

class _StitchOptionsPanelState extends State<_StitchOptionsPanel> {
  late StitchFeatureModel _model;
  late StitchCropMode _crop;
  late bool _loop360;
  late bool _fallback;
  late bool _fill;
  late double _fillRatio;
  late int _fillSource;
  late int _fillBlur;
  late int _fillFade;
  late double _fillBlurRatio;
  late double _fillFadePower;
  bool _showAdvanced = false;

  @override
  void initState() {
    super.initState();
    _syncFromWidget();
  }

  @override
  void didUpdateWidget(covariant _StitchOptionsPanel oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.model != widget.model ||
        oldWidget.crop != widget.crop ||
        oldWidget.fill != widget.fill) {
      _syncFromWidget();
    }
  }

  void _syncFromWidget() {
    _model = widget.model;
    _crop = widget.crop;
    _loop360 = widget.loop360;
    _fallback = widget.fallback;
    _fill = widget.fill;
    _fillRatio = widget.fillRatio;
    _fillSource = widget.fillSource;
    _fillBlur = widget.fillBlur;
    _fillFade = widget.fillFade;
    _fillBlurRatio = widget.fillBlurRatio;
    _fillFadePower = widget.fillFadePower;
  }

  void _notify() {
    widget.onChanged(
      _StitchOptions(
        model: _model,
        crop: _crop,
        loop360: _loop360,
        fallback: _fallback,
        fill: _fill,
        fillRatio: _fillRatio,
        fillSource: _fillSource,
        fillBlur: _fillBlur,
        fillFade: _fillFade,
        fillBlurRatio: _fillBlurRatio,
        fillFadePower: _fillFadePower,
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
              'Stitching options',
              style: TextStyle(
                color: Colors.white,
                fontSize: 16,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 4),
            Text(
              'Applied on the next stitch (during or after capture).',
              style: TextStyle(color: Colors.grey.shade400, fontSize: 12),
            ),
            const SizedBox(height: 12),
            _dropdown<StitchFeatureModel>(
              label: 'Feature model',
              value: _model,
              items: StitchFeatureModel.values,
              labelBuilder: (v) => v.name,
              onChanged: (v) {
                setState(() => _model = v);
                _notify();
              },
            ),
            _dropdown<StitchCropMode>(
              label: 'Crop',
              value: _crop,
              items: StitchCropMode.values,
              labelBuilder: (v) => v.name,
              onChanged: (v) {
                setState(() => _crop = v);
                _notify();
              },
            ),
            _switchTile('Loop 360°', _loop360, (v) {
              setState(() => _loop360 = v);
              _notify();
            }),
            _switchTile('Match fallback', _fallback, (v) {
              setState(() => _fallback = v);
              _notify();
            }),
            _switchTile('Top/bottom fill', _fill, (v) {
              setState(() => _fill = v);
              _notify();
            }),
            if (_fill) ...[
              _sliderTile(
                label: 'Fill ratio (W/H)',
                value: _fillRatio,
                min: 1.0,
                max: 3.0,
                divisions: 20,
                display: _fillRatio.toStringAsFixed(1),
                onChanged: (v) {
                  setState(() => _fillRatio = v);
                  _notify();
                },
              ),
              InkWell(
                onTap: () => setState(() => _showAdvanced = !_showAdvanced),
                child: Padding(
                  padding: const EdgeInsets.symmetric(vertical: 8),
                  child: Row(
                    children: [
                      Icon(
                        _showAdvanced ? Icons.expand_less : Icons.expand_more,
                        color: Colors.white70,
                        size: 20,
                      ),
                      const SizedBox(width: 4),
                      const Text(
                        'Advanced fill',
                        style: TextStyle(color: Colors.white70),
                      ),
                    ],
                  ),
                ),
              ),
              if (_showAdvanced) ...[
                _intSlider(
                  label: 'Fill source (rows)',
                  value: _fillSource,
                  min: 5,
                  max: 80,
                  onChanged: (v) {
                    setState(() => _fillSource = v);
                    _notify();
                  },
                ),
                _intSlider(
                  label: 'Fill blur (kernel)',
                  value: _fillBlur,
                  min: 3,
                  max: 601,
                  step: 2,
                  onChanged: (v) {
                    setState(() => _fillBlur = v);
                    _notify();
                  },
                ),
                _intSlider(
                  label: 'Fill fade (rows)',
                  value: _fillFade,
                  min: 0,
                  max: 100,
                  onChanged: (v) {
                    setState(() => _fillFade = v);
                    _notify();
                  },
                ),
                _sliderTile(
                  label: 'Blur ratio',
                  value: _fillBlurRatio,
                  min: 0.0,
                  max: 1.0,
                  divisions: 20,
                  display: _fillBlurRatio.toStringAsFixed(2),
                  onChanged: (v) {
                    setState(() => _fillBlurRatio = v);
                    _notify();
                  },
                ),
                _sliderTile(
                  label: 'Fade power',
                  value: _fillFadePowerClamped(),
                  min: 1.0,
                  max: 10.0,
                  divisions: 18,
                  display: _fillFadePower.toStringAsFixed(1),
                  onChanged: (v) {
                    setState(() => _fillFadePower = v);
                    _notify();
                  },
                ),
              ],
            ],
          ],
        ),
      ),
    );
  }

  double _fillFadePowerClamped() => _fillFadePower.clamp(1.0, 10.0);

  Widget _dropdown<T>({
    required String label,
    required T value,
    required List<T> items,
    required String Function(T) labelBuilder,
    required ValueChanged<T> onChanged,
  }) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Row(
        children: [
          SizedBox(
            width: 110,
            child: Text(label, style: const TextStyle(color: Colors.white70)),
          ),
          Expanded(
            child: DropdownButton<T>(
              value: value,
              isExpanded: true,
              dropdownColor: Colors.grey.shade900,
              style: const TextStyle(color: Colors.white),
              items: items
                  .map(
                    (e) => DropdownMenuItem(
                      value: e,
                      child: Text(labelBuilder(e)),
                    ),
                  )
                  .toList(),
              onChanged: (v) {
                if (v != null) onChanged(v);
              },
            ),
          ),
        ],
      ),
    );
  }

  Widget _switchTile(
    String label,
    bool value,
    ValueChanged<bool> onChanged,
  ) {
    return SwitchListTile(
      contentPadding: EdgeInsets.zero,
      title: Text(label, style: const TextStyle(color: Colors.white)),
      value: value,
      onChanged: onChanged,
    );
  }

  Widget _sliderTile({
    required String label,
    required double value,
    required double min,
    required double max,
    required int divisions,
    required String display,
    required ValueChanged<double> onChanged,
  }) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(label, style: const TextStyle(color: Colors.white70)),
            Text(display, style: const TextStyle(color: Colors.white)),
          ],
        ),
        Slider(
          value: value.clamp(min, max),
          min: min,
          max: max,
          divisions: divisions,
          onChanged: onChanged,
        ),
      ],
    );
  }

  Widget _intSlider({
    required String label,
    required int value,
    required int min,
    required int max,
    int step = 1,
    required ValueChanged<int> onChanged,
  }) {
    final divisions = ((max - min) / step).round();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(label, style: const TextStyle(color: Colors.white70)),
            Text('$value', style: const TextStyle(color: Colors.white)),
          ],
        ),
        Slider(
          value: value.toDouble().clamp(min.toDouble(), max.toDouble()),
          min: min.toDouble(),
          max: max.toDouble(),
          divisions: divisions > 0 ? divisions : null,
          onChanged: (v) => onChanged((v / step).round() * step),
        ),
      ],
    );
  }
}
