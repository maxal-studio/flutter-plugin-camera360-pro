import 'package:camera_360/camera_360.dart';
import 'package:panoramai/widgets/option_controls.dart';
import 'package:flutter/material.dart';

class StitchOptions {
  const StitchOptions({
    required this.model,
    required this.crop,
    required this.loop360,
    required this.loopClosure,
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
  final bool loopClosure;
  final bool fallback;
  final bool fill;
  final double fillRatio;
  final int fillSource;
  final int fillBlur;
  final int fillFade;
  final double fillBlurRatio;
  final double fillFadePower;
}

class StitchOptionsPanel extends StatefulWidget {
  const StitchOptionsPanel({
    super.key,
    required this.model,
    required this.crop,
    required this.loop360,
    required this.loopClosure,
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
  final bool loopClosure;
  final bool fallback;
  final bool fill;
  final double fillRatio;
  final int fillSource;
  final int fillBlur;
  final int fillFade;
  final double fillBlurRatio;
  final double fillFadePower;
  final ValueChanged<StitchOptions> onChanged;

  @override
  State<StitchOptionsPanel> createState() => _StitchOptionsPanelState();
}

class _StitchOptionsPanelState extends State<StitchOptionsPanel> {
  late StitchFeatureModel _model;
  late StitchCropMode _crop;
  late bool _loop360;
  late bool _loopClosure;
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
  void didUpdateWidget(covariant StitchOptionsPanel oldWidget) {
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
    _loopClosure = widget.loopClosure;
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
      StitchOptions(
        model: _model,
        crop: _crop,
        loop360: _loop360,
        loopClosure: _loopClosure,
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

  double _fillFadePowerClamped() => _fillFadePower.clamp(1.0, 10.0);

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
              'Changing any value reinitializes the camera widget.',
              style: TextStyle(color: Colors.grey.shade400, fontSize: 12),
            ),
            const SizedBox(height: 12),
            optionDropdown<StitchFeatureModel>(
              label: 'Feature model',
              value: _model,
              items: StitchFeatureModel.values,
              labelBuilder: (v) => v.name,
              onChanged: (v) {
                setState(() => _model = v);
                _notify();
              },
            ),
            optionDropdown<StitchCropMode>(
              label: 'Crop',
              value: _crop,
              items: StitchCropMode.values,
              labelBuilder: (v) => v.name,
              onChanged: (v) {
                setState(() => _crop = v);
                _notify();
              },
            ),
            optionSwitchTile('Loop 360°', _loop360, (v) {
              setState(() => _loop360 = v);
              _notify();
            }),
            optionSwitchTile('Loop closure', _loopClosure, (v) {
              setState(() => _loopClosure = v);
              _notify();
            }),
            optionSwitchTile('Match fallback', _fallback, (v) {
              setState(() => _fallback = v);
              _notify();
            }),
            optionSwitchTile('Top/bottom fill', _fill, (v) {
              setState(() => _fill = v);
              _notify();
            }),
            if (_fill) ...[
              optionSliderTile(
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
                optionIntSlider(
                  label: 'Fill source (rows)',
                  value: _fillSource,
                  min: 5,
                  max: 80,
                  onChanged: (v) {
                    setState(() => _fillSource = v);
                    _notify();
                  },
                ),
                optionIntSlider(
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
                optionIntSlider(
                  label: 'Fill fade (rows)',
                  value: _fillFade,
                  min: 0,
                  max: 100,
                  onChanged: (v) {
                    setState(() => _fillFade = v);
                    _notify();
                  },
                ),
                optionSliderTile(
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
                optionSliderTile(
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
}
