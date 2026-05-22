import 'package:camera_360/camera_360.dart';
import 'package:flutter/material.dart';
import 'package:image_picker/image_picker.dart';
import 'package:panoramai/preview.dart';
import 'package:panoramai/widgets/capture_options_panel.dart';
import 'package:panoramai/widgets/options_menu_button.dart';
import 'package:panoramai/widgets/stitch_options_panel.dart';

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
  bool _debugMode = true;
  bool _saveCapturedImages = false;
  DebugInfoPosition _debugInfoPosition = DebugInfoPosition.bottomRight;

  StitchFeatureModel _stitchModel = StitchFeatureModel.onnx;
  StitchCropMode _stitchCrop = StitchCropMode.none;
  bool _stitchLoop360 = true;
  bool _stitchLoopClosure = true;
  bool _stitchFallback = true;
  bool _stitchFill = true;
  double _stitchFillRatio = 2.0;
  int _stitchFillSource = 30;
  int _stitchFillBlur = 401;
  int _stitchFillFade = 50;
  double _stitchFillBlurRatio = 0.01;
  double _stitchFillFadePower = 5.0;

  /// Rebuilds [Camera360] when any constructor option changes. The widget is
  /// not designed to update its configuration while mounted.
  Key get _camera360Key => ValueKey(Object.hash(
        _captureNrPhotos,
        _captureVerticalDeg,
        _debugMode,
        _saveCapturedImages,
        _debugInfoPosition.alignment,
        _debugInfoPosition.padding,
        _stitchModel,
        _stitchCrop,
        _stitchLoop360,
        _stitchLoopClosure,
        _stitchFallback,
        _stitchFill,
        _stitchFillRatio,
        _stitchFillSource,
        _stitchFillBlur,
        _stitchFillFade,
        _stitchFillBlurRatio,
        _stitchFillFadePower,
      ));

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
          key: _camera360Key,
          photoCount: _captureNrPhotos,
          captureVerticalDeg: _captureVerticalDeg,
          debug: _debugMode,
          debugInfoPosition: _debugInfoPosition,
          saveCapturedImages: _saveCapturedImages,
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
          stitchLoopClosure: _stitchLoopClosure,
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
                      child: Material(
                        color: Colors.black54,
                        borderRadius: BorderRadius.circular(8),
                        child: Padding(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 12,
                            vertical: 8,
                          ),
                          child: Text(
                            'Progress: $progressPercentage%',
                            style: const TextStyle(
                              color: Colors.white,
                              fontWeight: FontWeight.w600,
                            ),
                          ),
                        ),
                      ),
                    ),
                    const SizedBox(width: 6),
                    OptionsMenuButton(
                      label: 'Capture',
                      icon: Icons.camera_outlined,
                      isOpen: _showCaptureMenu,
                      onTap: () => setState(() {
                        _showCaptureMenu = !_showCaptureMenu;
                        if (_showCaptureMenu) _showStitchMenu = false;
                      }),
                    ),
                    const SizedBox(width: 6),
                    OptionsMenuButton(
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
                  child: CaptureOptionsPanel(
                    nrPhotos: _captureNrPhotos,
                    verticalDeg: _captureVerticalDeg,
                    debug: _debugMode,
                    saveCapturedImages: _saveCapturedImages,
                    debugInfoPosition: _debugInfoPosition,
                    onChanged: (options) => setState(() {
                      _captureNrPhotos = options.nrPhotos;
                      _captureVerticalDeg = options.verticalDeg;
                      _debugMode = options.debug;
                      _saveCapturedImages = options.saveCapturedImages;
                      _debugInfoPosition = options.debugInfoPosition;
                      progressPercentage = 0;
                    }),
                  ),
                ),
              if (_showStitchMenu)
                Expanded(
                  child: StitchOptionsPanel(
                    model: _stitchModel,
                    crop: _stitchCrop,
                    loop360: _stitchLoop360,
                    loopClosure: _stitchLoopClosure,
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
                      _stitchLoopClosure = options.loopClosure;
                      _stitchFallback = options.fallback;
                      _stitchFill = options.fill;
                      _stitchFillRatio = options.fillRatio;
                      _stitchFillSource = options.fillSource;
                      _stitchFillBlur = options.fillBlur;
                      _stitchFillFade = options.fillFade;
                      _stitchFillBlurRatio = options.fillBlurRatio;
                      _stitchFillFadePower = options.fillFadePower;
                      progressPercentage = 0;
                    }),
                  ),
                ),
            ],
          ),
        ),
      ],
    );
  }
}
