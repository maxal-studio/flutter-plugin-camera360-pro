import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

/// iOS-specific helpers for the camera_360 plugin.
class IosPlatform {
  IosPlatform._();

  static const MethodChannel _channel = MethodChannel('camera_360/platform');

  static bool? _cachedIsSimulator;

  /// True when running on the iOS Simulator.
  ///
  /// Uses a native check via [MethodChannel] on iOS. Always false on other
  /// platforms.
  static Future<bool> get isSimulator async {
    if (!Platform.isIOS) {
      return false;
    }
    if (_cachedIsSimulator != null) {
      return _cachedIsSimulator!;
    }
    try {
      final fromChannel =
          await _channel.invokeMethod<bool>('isSimulator') ?? false;
      _cachedIsSimulator = fromChannel || _homeLooksLikeSimulator();
    } on PlatformException catch (e) {
      debugPrint('IosPlatform: isSimulator channel failed: $e');
      _cachedIsSimulator = _homeLooksLikeSimulator();
    } catch (e) {
      debugPrint('IosPlatform: isSimulator failed: $e');
      _cachedIsSimulator = _homeLooksLikeSimulator();
    }
    return _cachedIsSimulator!;
  }

  static bool _homeLooksLikeSimulator() {
    final home = Platform.environment['HOME'];
    return home != null && home.contains('CoreSimulator');
  }

  /// Clears the cached simulator flag (mainly for tests).
  @visibleForTesting
  static void resetCacheForTesting() {
    _cachedIsSimulator = null;
  }
}
