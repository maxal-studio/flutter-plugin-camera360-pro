import 'package:flutter/material.dart';

/// Degree slider with a fixed step size for developer-tuned controls.
Widget optionDegSlider({
  required String label,
  required double value,
  required double min,
  required double max,
  double step = 5,
  required ValueChanged<double> onChanged,
  String? hint,
}) {
  final divisions = ((max - min) / step).round();

  double snap(double v) {
    return ((v / step).round() * step).clamp(min, max);
  }

  final double snappedValue = snap(value);

  return Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label, style: const TextStyle(color: Colors.white70)),
          Text(
            '${snappedValue.round()}°',
            style: const TextStyle(color: Colors.white),
          ),
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
        value: snappedValue,
        min: min,
        max: max,
        divisions: divisions > 0 ? divisions : null,
        onChanged: (v) => onChanged(snap(v)),
      ),
    ],
  );
}

Widget optionSliderTile({
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

Widget optionIntSlider({
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

Widget optionSwitchTile(
  String label,
  bool value,
  ValueChanged<bool> onChanged, {
  String? subtitle,
}) {
  return SwitchListTile(
    contentPadding: EdgeInsets.zero,
    title: Text(label, style: const TextStyle(color: Colors.white)),
    subtitle: subtitle != null
        ? Text(
            subtitle,
            style: TextStyle(color: Colors.grey.shade500, fontSize: 12),
          )
        : null,
    value: value,
    onChanged: onChanged,
  );
}

Widget optionDropdown<T>({
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
