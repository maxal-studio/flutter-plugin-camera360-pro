#include "ExternalFeatures.hpp"

#include <cstring>
#include <iostream>

#include "OpenCvMatcher.hpp"  // detail::computeForwardHomography / mirrorReversePair

namespace camera360 {

namespace {

constexpr std::uint32_t kMagic   = 0x4346504Eu;  // "CFPN"
constexpr std::int32_t  kVersion = 1;

// Little-endian readers from a raw byte cursor. We don't assume the host is
// LE, even though every platform Flutter targets is.
inline std::int32_t readI32LE(const std::uint8_t* p) {
    return static_cast<std::int32_t>(
        (static_cast<std::uint32_t>(p[0]))       |
        (static_cast<std::uint32_t>(p[1]) << 8)  |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24));
}

inline float readF32LE(const std::uint8_t* p) {
    std::uint32_t u =
        (static_cast<std::uint32_t>(p[0]))       |
        (static_cast<std::uint32_t>(p[1]) << 8)  |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

}  // namespace

bool buildFromExternalBlob(const std::uint8_t*                     feat_blob,
                           std::size_t                             feat_len,
                           const std::vector<cv::Mat>&             images,
                           std::vector<cv::detail::ImageFeatures>& features,
                           std::vector<cv::detail::MatchesInfo>&   pairwise_matches)
{
    if (!feat_blob || feat_len < 16) {
        std::cerr << "ExternalFeatures: blob is null or shorter than header\n";
        return false;
    }

    // ── Header ───────────────────────────────────────────────────────────
    const std::uint32_t magic   = static_cast<std::uint32_t>(readI32LE(feat_blob + 0));
    const std::int32_t  version = readI32LE(feat_blob + 4);
    const std::int32_t  num_images = readI32LE(feat_blob + 8);
    const std::int32_t  num_pairs  = readI32LE(feat_blob + 12);

    if (magic != kMagic) {
        std::cerr << "ExternalFeatures: bad magic 0x" << std::hex << magic
                  << std::dec << " (expected 'CFPN')\n";
        return false;
    }
    if (version != kVersion) {
        std::cerr << "ExternalFeatures: unsupported version " << version
                  << " (expected " << kVersion << ")\n";
        return false;
    }

    const int N = num_images;
    const int K = num_pairs;
    if (N < 2 || static_cast<int>(images.size()) != N) {
        std::cerr << "ExternalFeatures: image count mismatch (blob N=" << N
                  << ", images.size()=" << images.size() << ")\n";
        return false;
    }
    if (K < 1) {
        std::cerr << "ExternalFeatures: need at least 1 pair, got " << K << "\n";
        return false;
    }

    // Per-table sizes (each table row is 16 bytes).
    constexpr std::size_t kHeader  = 16;
    constexpr std::size_t kImgRow  = 16;
    constexpr std::size_t kPairRow = 16;

    const std::size_t img_tbl_end  = kHeader + static_cast<std::size_t>(N) * kImgRow;
    const std::size_t pair_tbl_end = img_tbl_end + static_cast<std::size_t>(K) * kPairRow;
    if (feat_len < pair_tbl_end) {
        std::cerr << "ExternalFeatures: blob truncated in header/tables ("
                  << feat_len << " < " << pair_tbl_end << ")\n";
        return false;
    }

    // ── Image table → keypoints ──────────────────────────────────────────
    features.assign(static_cast<std::size_t>(N), cv::detail::ImageFeatures());

    for (int i = 0; i < N; ++i) {
        const std::uint8_t* row    = feat_blob + kHeader + static_cast<std::size_t>(i) * kImgRow;
        const std::int32_t  w      = readI32LE(row + 0);
        const std::int32_t  h      = readI32LE(row + 4);
        const std::int32_t  nkpts  = readI32LE(row + 8);
        const std::int32_t  offset = readI32LE(row + 12);

        if (w != images[i].cols || h != images[i].rows) {
            std::cerr << "ExternalFeatures: image " << i
                      << " size mismatch (blob " << w << "x" << h
                      << ", decoded " << images[i].cols << "x" << images[i].rows << ")\n";
            return false;
        }
        if (nkpts < 0) {
            std::cerr << "ExternalFeatures: image " << i
                      << " has negative kpt count " << nkpts << "\n";
            return false;
        }

        // 8 bytes per keypoint (2 × float32). Validate the entire run lives
        // inside the blob.
        const std::size_t kpts_bytes = static_cast<std::size_t>(nkpts) * 8u;
        const std::size_t kpts_off   = static_cast<std::size_t>(offset);
        if (kpts_off + kpts_bytes > feat_len) {
            std::cerr << "ExternalFeatures: image " << i
                      << " kpts run out of bounds (off=" << kpts_off
                      << ", bytes=" << kpts_bytes << ", blob=" << feat_len << ")\n";
            return false;
        }

        features[i].img_idx  = i;
        features[i].img_size = images[i].size();
        features[i].keypoints.reserve(static_cast<std::size_t>(nkpts));

        const std::uint8_t* p = feat_blob + kpts_off;
        for (int k = 0; k < nkpts; ++k, p += 8) {
            const float x = readF32LE(p + 0);
            const float y = readF32LE(p + 4);
            // size=1 mirrors the placeholder OpenCV's BestOf2NearestMatcher
            // would have written for sub-pixel point detectors.
            features[i].keypoints.emplace_back(x, y, 1.0f);
        }
    }

    // ── Pair table → MatchesInfo with RANSAC + mirrored reverse ──────────
    pairwise_matches.assign(static_cast<std::size_t>(N) * N,
                            cv::detail::MatchesInfo());
    auto idx = [&](int a, int b) {
        return static_cast<std::size_t>(a) * N + static_cast<std::size_t>(b);
    };

    constexpr int    kMinMatches   = 8;
    constexpr double kRansacThresh = 3.0;

    for (int k = 0; k < K; ++k) {
        const std::uint8_t* row =
            feat_blob + img_tbl_end + static_cast<std::size_t>(k) * kPairRow;
        const std::int32_t a       = readI32LE(row + 0);
        const std::int32_t b       = readI32LE(row + 4);
        const std::int32_t nm      = readI32LE(row + 8);
        const std::int32_t offset  = readI32LE(row + 12);

        if (a < 0 || a >= N || b < 0 || b >= N || a == b) {
            std::cerr << "ExternalFeatures: pair " << k
                      << " has invalid (src,dst)=(" << a << "," << b << ")\n";
            return false;
        }
        if (nm < 0) {
            std::cerr << "ExternalFeatures: pair " << k
                      << " has negative match count " << nm << "\n";
            return false;
        }

        const std::size_t m_bytes = static_cast<std::size_t>(nm) * 8u;
        const std::size_t m_off   = static_cast<std::size_t>(offset);
        if (m_off + m_bytes > feat_len) {
            std::cerr << "ExternalFeatures: pair " << k
                      << " matches run out of bounds (off=" << m_off
                      << ", bytes=" << m_bytes << ", blob=" << feat_len << ")\n";
            return false;
        }

        cv::detail::MatchesInfo& fwd = pairwise_matches[idx(a, b)];
        // Reset — a prior pair's mirrorReversePair may have filled idx(b,a),
        // and a duplicate blob entry (e.g. N=2 loop360) must not append.
        fwd = cv::detail::MatchesInfo();
        fwd.src_img_idx = a;
        fwd.dst_img_idx = b;

        // Build DMatch entries + the point arrays for RANSAC.
        std::vector<cv::Point2f> Lpts, Rpts;
        Lpts.reserve(static_cast<std::size_t>(nm));
        Rpts.reserve(static_cast<std::size_t>(nm));
        fwd.matches.reserve(static_cast<std::size_t>(nm));

        const int n_kpts_a = static_cast<int>(features[a].keypoints.size());
        const int n_kpts_b = static_cast<int>(features[b].keypoints.size());

        const std::uint8_t* p = feat_blob + m_off;
        bool indices_ok = true;
        for (int m = 0; m < nm; ++m, p += 8) {
            const std::int32_t qi = readI32LE(p + 0);
            const std::int32_t ti = readI32LE(p + 4);
            if (qi < 0 || qi >= n_kpts_a || ti < 0 || ti >= n_kpts_b) {
                std::cerr << "ExternalFeatures: pair " << a << "->" << b
                          << " match " << m
                          << " has out-of-range (queryIdx,trainIdx)=("
                          << qi << "," << ti << ") image kpt counts ("
                          << n_kpts_a << "," << n_kpts_b << ")\n";
                indices_ok = false;
                break;
            }
            fwd.matches.emplace_back(qi, ti, 0.0f);
            Lpts.push_back(features[a].keypoints[qi].pt);
            Rpts.push_back(features[b].keypoints[ti].pt);
        }
        if (!indices_ok) return false;

        if (static_cast<int>(fwd.matches.size()) < kMinMatches) {
            std::cout << "ExternalFeatures: pair " << a << "->" << b
                      << " only " << fwd.matches.size()
                      << " matches in blob, skipping\n";
            continue;
        }

        const bool ok = detail::computeForwardHomography(
            Lpts, Rpts,
            features[a].img_size, features[b].img_size,
            kMinMatches, kRansacThresh, fwd);
        if (!ok) {
            std::cout << "ExternalFeatures: pair " << a << "->" << b
                      << " homography failed (" << fwd.matches.size()
                      << " matches)\n";
            continue;
        }

        std::cout << "ExternalFeatures: pair " << a << "->" << b
                  << ": " << fwd.matches.size() << " matches, "
                  << fwd.num_inliers << " inliers, confidence="
                  << fwd.confidence << "\n";

        detail::mirrorReversePair(fwd, a, b, pairwise_matches[idx(b, a)]);
    }

    return true;
}

}  // namespace camera360
