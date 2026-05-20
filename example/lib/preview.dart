import 'dart:io';

import 'package:flutter/material.dart';
import 'package:gallery_saver_plus/gallery_saver.dart';
import 'package:panorama_viewer/panorama_viewer.dart';

class PanoramaPreviewScreen extends StatefulWidget {
  const PanoramaPreviewScreen({
    super.key,
    required this.panoramaPath,
  });

  final String panoramaPath;

  @override
  State<PanoramaPreviewScreen> createState() => _PanoramaPreviewScreenState();
}

class _PanoramaPreviewScreenState extends State<PanoramaPreviewScreen> {
  bool _isSaving = false;
  bool _saved = false;

  Future<void> _saveToGallery() async {
    if (_isSaving || _saved) return;

    setState(() => _isSaving = true);

    final saved = await GallerySaver.saveImage(widget.panoramaPath);

    if (!mounted) return;

    setState(() {
      _isSaving = false;
      _saved = saved == true;
    });

    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(
          saved == true ? 'Panorama saved to gallery' : 'Failed to save panorama',
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Stack(
        fit: StackFit.expand,
        children: [
          PanoramaViewer(
            child: Image.file(
              File(widget.panoramaPath),
              fit: BoxFit.cover,
            ),
          ),
          SafeArea(
            child: Align(
              alignment: Alignment.bottomCenter,
              child: Padding(
                padding: const EdgeInsets.fromLTRB(16, 8, 16, 16),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    _OverlayButton(
                      icon: Icons.arrow_back,
                      label: 'Back',
                      onPressed: () => Navigator.of(context).pop(),
                    ),
                    const SizedBox(width: 12),
                    _OverlayButton(
                      icon: _saved ? Icons.check : Icons.download,
                      label: _saved ? 'Saved' : 'Download',
                      onPressed: (_isSaving || _saved) ? null : _saveToGallery,
                      showProgress: _isSaving,
                    ),
                  ],
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _OverlayButton extends StatelessWidget {
  const _OverlayButton({
    required this.icon,
    required this.label,
    this.onPressed,
    this.showProgress = false,
  });

  final IconData icon;
  final String label;
  final VoidCallback? onPressed;
  final bool showProgress;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Colors.black54,
      borderRadius: BorderRadius.circular(8),
      child: InkWell(
        onTap: onPressed,
        borderRadius: BorderRadius.circular(8),
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (showProgress)
                const SizedBox(
                  width: 20,
                  height: 20,
                  child: CircularProgressIndicator(
                    strokeWidth: 2,
                    color: Colors.white,
                  ),
                )
              else
                Icon(icon, color: Colors.white, size: 20),
              const SizedBox(width: 6),
              Text(
                label,
                style: TextStyle(
                  color: onPressed == null ? Colors.white54 : Colors.white,
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
