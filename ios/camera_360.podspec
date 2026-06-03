#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint camera_360.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'camera_360'
  s.version          = '0.0.1'
  s.summary          = 'Flutter 360 Camera'
  s.description      = <<-DESC
Flutter 360 Camera
                       DESC
  s.homepage         = 'https://max.al'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'MaxAl' => 'info@max.al' }

  s.source           = { :path => '.' }
  s.source_files     = 'Classes/Camera360Plugin.swift'
  s.dependency 'Flutter'
  s.platform = :ios, '16.0'
  s.ios.deployment_target = '16.0'

  s.vendored_frameworks = 'camera_360.xcframework'

  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES', 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386' }
  s.swift_version = '5.0'
end
