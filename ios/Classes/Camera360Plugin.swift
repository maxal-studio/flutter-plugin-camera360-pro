import Flutter
import UIKit

public class Camera360Plugin: NSObject, FlutterPlugin {
  public static func register(with registrar: FlutterPluginRegistrar) {
    let channel = FlutterMethodChannel(
      name: "camera_360/platform",
      binaryMessenger: registrar.messenger()
    )
    let instance = Camera360Plugin()
    registrar.addMethodCallDelegate(instance, channel: channel)
  }

  public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    switch call.method {
    case "isSimulator":
      #if targetEnvironment(simulator)
      result(true)
      #else
      result(false)
      #endif
    default:
      result(FlutterMethodNotImplemented)
    }
  }
}
