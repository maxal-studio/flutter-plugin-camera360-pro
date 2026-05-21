import 'dart:async';
import 'dart:io';

import 'package:camera/camera.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter_onnxruntime/flutter_onnxruntime.dart';
import 'package:image/image.dart' as img;

/// Runs the bundled SuperPoint + LightGlue ONNX pipeline on adjacent image
/// pairs and serialises the resulting keypoints and matches into the
/// `features_blob` byte layout that `stitch_with_features` expects on the
/// C++ side (see `src/umbrella/Stitcher/ExternalFeatures.hpp`).
///
/// Expected model I/O (matches the bundled
/// `superpoint_lightglue_pipeline.ort.onnx`):
///   inputs  : `images`     [batch_size, 1, H, W]  float32 (grayscale, 0..1)
///   outputs : `keypoints`  [batch_size, K, 2]     int64   (K = 1024 per image)
///             `matches`    [M, 3]                 int64   (pair_idx, q, t)
///             `mscores`    [M]                    float32
///
/// Each call to [compute] runs the model once per adjacent pair (so each
/// image participates in two pair runs and ends up with two independent
/// keypoint sets — they are concatenated into a single per-image array, with
/// the corresponding pair's match indices rewritten by the slice offset).
class OnnxFeaturePipeline {
  OnnxFeaturePipeline({
    this.modelInputSize = 512,
    this.imageInputName = 'images',
    this.keypointsOutputName = 'keypoints',
    this.matchesOutputName = 'matches',
  });

  /// Square side the captured photos are resized to before being fed to the
  /// model. Must be a multiple of 8 (SuperPoint downsamples by 8). 512 is a
  /// good speed/quality balance on phones; bump to 1024 for more keypoints.
  final int modelInputSize;

  /// ONNX input / output names. Kept configurable so a swapped-in model can
  /// reuse the pipeline without code changes.
  final String imageInputName;
  final String keypointsOutputName;
  final String matchesOutputName;

  OrtSession? _session;

  /// Load (or reload) the model. Safe to call multiple times.
  Future<void> load(String modelFilePath) async {
    final file = File(modelFilePath);
    if (!await file.exists()) {
      throw ArgumentError(
        'OnnxFeaturePipeline: model file not found at $modelFilePath',
      );
    }
    await _session?.close();
    final ort = OnnxRuntime();
    _session = await ort.createSession(modelFilePath);

    if (kDebugMode) {
      final inputs = await _session!.getInputInfo();
      final outputs = await _session!.getOutputInfo();
      debugPrint('OnnxFeaturePipeline: inputs  = $inputs');
      debugPrint('OnnxFeaturePipeline: outputs = $outputs');
    }
  }

  /// Run the model on every adjacent pair and return a packed
  /// `features_blob` buffer ready to ship over FFI.
  ///
  /// `images` order is preserved. When [loop360] is true the closing pair
  /// (last, 0) is also processed.
  ///
  /// [onProgress] is called from the main isolate with a stage label and
  /// an overall percent in the 0..65 range — the upstream
  /// [Stitcher.stitchImages] poller continues into 65..100 once the
  /// native compose FFI starts.
  Future<Uint8List> compute(
    List<XFile> images, {
    required bool loop360,
    void Function(String stage, int percent)? onProgress,
  }) async {
    if (_session == null) {
      throw StateError(
        'OnnxFeaturePipeline.compute called before load(): the ONNX session '
        'has not been initialised.',
      );
    }
    final n = images.length;
    if (n < 2) {
      throw ArgumentError(
        'OnnxFeaturePipeline.compute needs at least 2 images, got $n',
      );
    }

    debugPrint(
      'OnnxFeaturePipeline: compute start — $n images, loop360=$loop360, '
      'inputSize=${modelInputSize}x$modelInputSize',
    );

    // ONNX progress budget within the global 0..100 (feature work is slow):
    //   Preprocessing     1..5 %
    //   Matching features 5..65 %
    // The downstream stitch poller picks up at 65 % once the native FFI runs.
    // Avoid emitting 0 % here — it follows "Preparing" and makes the bar look
    // like it reset when the stage label changes.

    // Decode + preprocess each image exactly once and reuse the tensors
    // across the two pairs each image participates in.
    final preprocessed = <_PreprocessedImage>[];
    for (int i = 0; i < n; ++i) {
      preprocessed.add(await _preprocess(images[i], index: i));
      final preprocessPct = ((i + 1) * 5) ~/ n;
      onProgress?.call(
        'Preprocessing',
        preprocessPct > 0 ? preprocessPct : 1,
      );
    }

    // Per-image keypoint arrays — interleaved (x0, y0, x1, y1, ...) so they
    // can be `setAll`-copied straight into the blob's keypoints region.
    final perImageKpts = List<List<double>>.generate(n, (_) => <double>[]);
    final perPairMatches = <_PairMatches>[];

    // Adjacent pairs: (0,1), (1,2), …, and when loop360 the closing (N-1, 0).
    // For N=2 + loop360, (1,0) is the reverse of (0,1); C++ already builds the
    // reverse MatchesInfo via mirrorReversePair, so running ONNX twice would
    // duplicate matches into the same matrix slot and corrupt homographies.
    final pairCount = loop360 ? (n == 2 ? 1 : n) : n - 1;
    for (int p = 0; p < pairCount; ++p) {
      final a = p;
      final b = (p + 1) % n;

      final inferred = await _runPair(preprocessed[a], preprocessed[b]);
      final offsetA = perImageKpts[a].length ~/ 2;
      final offsetB = perImageKpts[b].length ~/ 2;

      perImageKpts[a].addAll(inferred.kptsA);
      perImageKpts[b].addAll(inferred.kptsB);

      // Rewrite match indices to point into the concatenated per-image
      // arrays. SuperPoint emits a fixed K per image, so offsetA / offsetB
      // are always a multiple of K — but we don't assume that here.
      final matches = Int32List(inferred.matches.length);
      for (int m = 0; m < inferred.matches.length; m += 2) {
        matches[m] = inferred.matches[m] + offsetA;
        matches[m + 1] = inferred.matches[m + 1] + offsetB;
      }

      perPairMatches.add(_PairMatches(srcIdx: a, dstIdx: b, indices: matches));

      debugPrint(
        'OnnxFeaturePipeline: pair $a->$b — '
        'kptsA=${inferred.kptsA.length ~/ 2}, '
        'kptsB=${inferred.kptsB.length ~/ 2}, '
        'matches=${inferred.matches.length ~/ 2}',
      );
      onProgress?.call(
        'Matching features',
        5 + (((p + 1) * 60) ~/ pairCount),
      );
    }

    final blob = _serialize(preprocessed, perImageKpts, perPairMatches);
    debugPrint(
      'OnnxFeaturePipeline: features blob ${blob.length} bytes '
      '($n images, $pairCount pairs)',
    );
    for (int i = 0; i < n; ++i) {
      debugPrint(
        'OnnxFeaturePipeline:   image $i — '
        '${preprocessed[i].originalWidth}x${preprocessed[i].originalHeight}, '
        '${perImageKpts[i].length ~/ 2} keypoints',
      );
    }
    return blob;
  }

  /// Release the underlying ONNX session. Safe to call when not loaded.
  Future<void> dispose() async {
    await _session?.close();
    _session = null;
  }

  // ── Internals ───────────────────────────────────────────────────────────

  Future<_PreprocessedImage> _preprocess(XFile xfile, {required int index}) async {
    final bytes = await xfile.readAsBytes();
    final decoded = img.decodeImage(bytes);
    if (decoded == null) {
      throw StateError(
        'OnnxFeaturePipeline: failed to decode image $index at ${xfile.path}',
      );
    }
    final origW = decoded.width;
    final origH = decoded.height;
    debugPrint(
      'OnnxFeaturePipeline: image $index decoded ${origW}x$origH '
      '(${bytes.length} bytes) → model ${modelInputSize}x$modelInputSize',
    );

    final resized = img.copyResize(
      decoded,
      width: modelInputSize,
      height: modelInputSize,
      interpolation: img.Interpolation.linear,
    );

    final gray = img.grayscale(resized);
    final tensor = Float32List(modelInputSize * modelInputSize);
    // `image: ^4.x` stores grayscale as a single Luminance channel — read it
    // via `r` (which mirrors luminance for greyscale images).
    int idx = 0;
    for (int y = 0; y < modelInputSize; ++y) {
      for (int x = 0; x < modelInputSize; ++x) {
        final px = gray.getPixel(x, y);
        tensor[idx++] = px.r / 255.0;
      }
    }
    return _PreprocessedImage(
      originalWidth: origW,
      originalHeight: origH,
      modelTensor: tensor,
    );
  }

  /// Read an int64 ONNX output as a flat `List<int>`.
  ///
  /// [OrtValue.asList] reshapes multi-dimensional tensors into nested Dart
  /// lists — a `.cast<int>()` on that result always fails. Use the flattened
  /// 1-D view instead.
  Future<List<int>> _tensorAsFlatIntList(OrtValue value, String label) async {
    final flat = await value.asFlattenedList();
    if (flat.isEmpty) {
      debugPrint('OnnxFeaturePipeline: $label is empty (shape=${value.shape})');
      return const [];
    }
    try {
      return flat.map((e) => (e as num).toInt()).toList();
    } catch (e) {
      throw StateError(
        'OnnxFeaturePipeline: $label element type ${flat.first.runtimeType} '
        '(shape=${value.shape}, len=${flat.length}): $e',
      );
    }
  }

  Future<_PairResult> _runPair(
    _PreprocessedImage a,
    _PreprocessedImage b,
  ) async {
    // Stack the two grayscale tensors along the batch dim →
    // shape [2, 1, H, W].
    final batch = Float32List(2 * modelInputSize * modelInputSize);
    batch.setAll(0, a.modelTensor);
    batch.setAll(modelInputSize * modelInputSize, b.modelTensor);

    final input = await OrtValue.fromList(
      batch,
      [2, 1, modelInputSize, modelInputSize],
    );

    Map<String, OrtValue>? outputs;
    try {
      outputs = await _session!.run({imageInputName: input});

      final kptsValue = outputs[keypointsOutputName];
      final matchesValue = outputs[matchesOutputName];
      if (kptsValue == null || matchesValue == null) {
        throw StateError(
          'OnnxFeaturePipeline: missing expected outputs. '
          'Got keys: ${outputs.keys.toList()}',
        );
      }

      debugPrint(
        'OnnxFeaturePipeline: inference ok — '
        'keypoints shape=${kptsValue.shape} dtype=${kptsValue.dataType}, '
        'matches shape=${matchesValue.shape} dtype=${matchesValue.dataType}',
      );

      // keypoints: [batch, K, 2] int64 in model-input pixel coords.
      final kptsFlat = await _tensorAsFlatIntList(kptsValue, 'keypoints');
      final batchSize = kptsValue.shape.isNotEmpty ? kptsValue.shape[0] : 2;
      final kPerImage = kptsValue.shape.length >= 2 ? kptsValue.shape[1] : 0;

      int perImage;
      if (kPerImage > 0 && batchSize > 0) {
        perImage = kPerImage;
        if (kptsFlat.length != batchSize * perImage * 2) {
          debugPrint(
            'OnnxFeaturePipeline: keypoints flat len ${kptsFlat.length} '
            '!= expected ${batchSize * perImage * 2} from shape '
            '${kptsValue.shape} — using flat/(${batchSize * 2})',
          );
          perImage = kptsFlat.length ~/ (batchSize * 2);
        }
      } else {
        final totalKpts = kptsFlat.length ~/ 2;
        if (totalKpts.isOdd || totalKpts == 0) {
          throw StateError(
            'OnnxFeaturePipeline: keypoints tensor has unexpected flat length '
            '${kptsFlat.length} (shape=${kptsValue.shape})',
          );
        }
        perImage = totalKpts ~/ batchSize;
      }

      // Rescale keypoints from model-input pixel space back to the original
      // image's pixel space.
      final scaleAx = a.originalWidth / modelInputSize;
      final scaleAy = a.originalHeight / modelInputSize;
      final scaleBx = b.originalWidth / modelInputSize;
      final scaleBy = b.originalHeight / modelInputSize;

      final kptsA = List<double>.filled(perImage * 2, 0);
      final kptsB = List<double>.filled(perImage * 2, 0);
      for (int k = 0; k < perImage; ++k) {
        kptsA[k * 2] = kptsFlat[k * 2] * scaleAx;
        kptsA[k * 2 + 1] = kptsFlat[k * 2 + 1] * scaleAy;
      }
      final bBase = perImage * 2;
      for (int k = 0; k < perImage; ++k) {
        kptsB[k * 2] = kptsFlat[bBase + k * 2] * scaleBx;
        kptsB[k * 2 + 1] = kptsFlat[bBase + k * 2 + 1] * scaleBy;
      }

      // Log a few rescaled keypoints for sanity-checking.
      if (kptsFlat.isNotEmpty) {
        debugPrint(
          'OnnxFeaturePipeline:   kpts sample (model→orig): '
          'A[0]=(${kptsA[0].toStringAsFixed(1)}, ${kptsA[1].toStringAsFixed(1)}) '
          'B[0]=(${kptsB[0].toStringAsFixed(1)}, ${kptsB[1].toStringAsFixed(1)})',
        );
      }

      // matches: [M, 3] int64 (pair_idx, queryIdx, trainIdx). With batch=2
      // there is exactly one pair, so pair_idx is always 0. We strip it.
      final matchesFlat =
          await _tensorAsFlatIntList(matchesValue, 'matches');
      if (matchesFlat.isNotEmpty && matchesFlat.length % 3 != 0) {
        throw StateError(
          'OnnxFeaturePipeline: matches flat length '
          '${matchesFlat.length} is not a multiple of 3 '
          '(shape=${matchesValue.shape})',
        );
      }
      final m = matchesFlat.length ~/ 3;
      final matches = Int32List(m * 2);
      for (int i = 0; i < m; ++i) {
        final pairIdx = matchesFlat[i * 3];
        final q = matchesFlat[i * 3 + 1];
        final t = matchesFlat[i * 3 + 2];
        if (pairIdx != 0 && i < 3) {
          debugPrint(
            'OnnxFeaturePipeline:   match[$i] pair_idx=$pairIdx (expected 0)',
          );
        }
        if (q >= perImage || t >= perImage) {
          throw StateError(
            'OnnxFeaturePipeline: match[$i] (q=$q, t=$t) out of range '
            'for per-image kpt count $perImage',
          );
        }
        matches[i * 2] = q;
        matches[i * 2 + 1] = t;
      }

      if (m == 0) {
        debugPrint(
          'OnnxFeaturePipeline: WARNING — 0 matches for this pair; '
          'native stitch may fail unless fallback synthesizes homography',
        );
      } else {
        debugPrint(
          'OnnxFeaturePipeline:   first matches (q,t): '
          '(${matches[0]}, ${matches[1]}), '
          '(${matches[2]}, ${matches[3]}), '
          '(${matches[4]}, ${matches[5]})',
        );
      }

      return _PairResult(kptsA: kptsA, kptsB: kptsB, matches: matches);
    } finally {
      input.dispose();
      if (outputs != null) {
        for (final t in outputs.values) {
          t.dispose();
        }
      }
    }
  }

  // ── Blob serialisation (matches ExternalFeatures.hpp layout) ────────────

  /// Pack the per-image keypoints and per-pair matches into a single
  /// little-endian byte buffer in the layout `ExternalFeatures::buildFromExternalBlob`
  /// parses. See that header for the canonical spec.
  Uint8List _serialize(
    List<_PreprocessedImage> images,
    List<List<double>> perImageKpts,
    List<_PairMatches> perPairMatches,
  ) {
    const headerBytes = 16;
    const imgRowBytes = 16;
    const pairRowBytes = 16;

    final n = images.length;
    final k = perPairMatches.length;

    final kptsRegionStart = headerBytes + n * imgRowBytes + k * pairRowBytes;
    int kptsCursor = kptsRegionStart;
    final imgKptsOffsets = List<int>.filled(n, 0);
    int kptsRegionBytes = 0;
    for (int i = 0; i < n; ++i) {
      imgKptsOffsets[i] = kptsCursor;
      // Each keypoint is 2 × float32 = 8 bytes.
      final bytesForImage = (perImageKpts[i].length ~/ 2) * 8;
      kptsCursor += bytesForImage;
      kptsRegionBytes += bytesForImage;
    }

    int matchesCursor = kptsRegionStart + kptsRegionBytes;
    final pairMatchOffsets = List<int>.filled(k, 0);
    int matchesRegionBytes = 0;
    for (int p = 0; p < k; ++p) {
      pairMatchOffsets[p] = matchesCursor;
      // Each match is 2 × int32 = 8 bytes.
      final bytesForPair = (perPairMatches[p].indices.length ~/ 2) * 8;
      matchesCursor += bytesForPair;
      matchesRegionBytes += bytesForPair;
    }

    final totalBytes = kptsRegionStart + kptsRegionBytes + matchesRegionBytes;
    final buf = ByteData(totalBytes);

    // Header.
    buf.setInt32(0, 0x4346504E, Endian.little); // 'CFPN'
    buf.setInt32(4, 1, Endian.little); // version
    buf.setInt32(8, n, Endian.little);
    buf.setInt32(12, k, Endian.little);

    // Image table.
    for (int i = 0; i < n; ++i) {
      final off = headerBytes + i * imgRowBytes;
      buf.setInt32(off + 0, images[i].originalWidth, Endian.little);
      buf.setInt32(off + 4, images[i].originalHeight, Endian.little);
      buf.setInt32(off + 8, perImageKpts[i].length ~/ 2, Endian.little);
      buf.setInt32(off + 12, imgKptsOffsets[i], Endian.little);
    }

    // Pair table.
    for (int p = 0; p < k; ++p) {
      final off = headerBytes + n * imgRowBytes + p * pairRowBytes;
      buf.setInt32(off + 0, perPairMatches[p].srcIdx, Endian.little);
      buf.setInt32(off + 4, perPairMatches[p].dstIdx, Endian.little);
      buf.setInt32(off + 8, perPairMatches[p].indices.length ~/ 2,
          Endian.little);
      buf.setInt32(off + 12, pairMatchOffsets[p], Endian.little);
    }

    // Keypoints region: per-image float32 (x, y) pairs.
    for (int i = 0; i < n; ++i) {
      int o = imgKptsOffsets[i];
      final flat = perImageKpts[i];
      for (int j = 0; j < flat.length; ++j) {
        buf.setFloat32(o, flat[j], Endian.little);
        o += 4;
      }
    }

    // Matches region: per-pair int32 (queryIdx, trainIdx) pairs.
    for (int p = 0; p < k; ++p) {
      int o = pairMatchOffsets[p];
      final idx = perPairMatches[p].indices;
      for (int j = 0; j < idx.length; ++j) {
        buf.setInt32(o, idx[j], Endian.little);
        o += 4;
      }
    }

    return buf.buffer.asUint8List();
  }
}

class _PreprocessedImage {
  _PreprocessedImage({
    required this.originalWidth,
    required this.originalHeight,
    required this.modelTensor,
  });

  final int originalWidth;
  final int originalHeight;
  final Float32List modelTensor;
}

class _PairResult {
  _PairResult({
    required this.kptsA,
    required this.kptsB,
    required this.matches,
  });

  /// Image A's keypoints in ORIGINAL pixel space, flat (x, y, x, y, ...).
  final List<double> kptsA;
  final List<double> kptsB;

  /// Match indices, flat (queryIdx, trainIdx, ...) into kptsA / kptsB.
  final Int32List matches;
}

class _PairMatches {
  _PairMatches({
    required this.srcIdx,
    required this.dstIdx,
    required this.indices,
  });

  final int srcIdx;
  final int dstIdx;

  /// Flat (queryIdx, trainIdx, ...) — already rewritten with the
  /// concatenated per-image keypoint offsets.
  final Int32List indices;
}
