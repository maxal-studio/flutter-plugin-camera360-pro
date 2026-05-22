import 'package:flutter/material.dart';

/// Pass as [Camera360.captureResetButtonBuilder] to hide the reset control.
Widget hideCaptureResetButton(VoidCallback onReset) => const SizedBox.shrink();

/// Default shutter-style control shown before capture starts.
class DefaultCaptureStartButton extends StatelessWidget {
  final VoidCallback onStart;
  final bool enabled;

  const DefaultCaptureStartButton({
    super.key,
    required this.onStart,
    this.enabled = true,
  });

  static const double _outerSize = 72;
  static const double _ringWidth = 4;
  static const double _innerPadding = 6;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 16),
      child: Center(
        child: Material(
          color: Colors.transparent,
          child: InkWell(
            onTap: enabled ? onStart : null,
            customBorder: const CircleBorder(),
            child: Ink(
              width: _outerSize,
              height: _outerSize,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: Colors.black.withValues(alpha: 0.45),
                border: Border.all(
                  color: Colors.white.withValues(alpha: enabled ? 1 : 0.4),
                  width: _ringWidth,
                ),
              ),
              child: Padding(
                padding: const EdgeInsets.all(_innerPadding),
                child: DecoratedBox(
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: enabled
                        ? const Color(0xFFE53935)
                        : Colors.white.withValues(alpha: 0.25),
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// Default dark reset control shown at the bottom during capture.
class DefaultCaptureResetButton extends StatelessWidget {
  final VoidCallback onReset;

  const DefaultCaptureResetButton({
    super.key,
    required this.onReset,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: Center(
        child: TextButton(
          style: TextButton.styleFrom(
            backgroundColor: Colors.black.withValues(alpha: 0.65),
            foregroundColor: Colors.white,
            disabledForegroundColor: Colors.white54,
            padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(24),
            ),
          ),
          onPressed: onReset,
          child: const Text('Reset'),
        ),
      ),
    );
  }
}
