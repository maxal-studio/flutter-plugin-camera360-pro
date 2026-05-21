import 'package:flutter/material.dart';

/// Built-in overlay shown while a panorama is being stitched.
///
/// Used by [Camera360] when no custom [StitchingProgressBuilder] is
/// supplied. Shows the current [stage] label, a linear progress bar,
/// and overall [percent] (0..100).
class StitchingProgressOverlay extends StatefulWidget {
  const StitchingProgressOverlay({
    super.key,
    required this.stage,
    required this.percent,
    this.fallbackLabel = 'Preparing panorama…',
  });

  final String stage;
  final int percent;
  final String fallbackLabel;

  @override
  State<StitchingProgressOverlay> createState() =>
      _StitchingProgressOverlayState();
}

class _StitchingProgressOverlayState extends State<StitchingProgressOverlay> {
  /// Overall progress 0..1; only increases so the bar never resets between
  /// pipeline stages.
  double _barProgress = 0;

  @override
  void initState() {
    super.initState();
    _applyProgress(widget.percent);
  }

  @override
  void didUpdateWidget(StitchingProgressOverlay oldWidget) {
    super.didUpdateWidget(oldWidget);
    _applyProgress(widget.percent);
  }

  void _applyProgress(int percent) {
    final next = (percent.clamp(0, 100)) / 100.0;
    if (next > _barProgress) {
      setState(() => _barProgress = next);
    }
  }

  @override
  Widget build(BuildContext context) {
    final label =
        widget.stage.isNotEmpty ? widget.stage : widget.fallbackLabel;
    final percentDisplay = (_barProgress * 100).round();

    return Center(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 48),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              label,
              style: const TextStyle(
                color: Colors.white,
                fontSize: 16,
                fontWeight: FontWeight.w500,
              ),
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 16),
            ClipRRect(
              borderRadius: BorderRadius.circular(4),
              child: LinearProgressIndicator(
                value: _barProgress,
                minHeight: 4,
                backgroundColor: Colors.white24,
                valueColor: const AlwaysStoppedAnimation<Color>(Colors.white),
              ),
            ),
            const SizedBox(height: 8),
            Text(
              '$percentDisplay%',
              style: const TextStyle(
                color: Colors.white70,
                fontSize: 14,
                fontFeatures: [FontFeature.tabularFigures()],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
