#include "Stitcher.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "ExternalFeatures.hpp"
#include "MatchFallback.hpp"
#include "OpenCvMatcher.hpp"
#include "PanoramicStitcher.hpp"

namespace {

// Strip surrounding whitespace + optional matching quotes/brackets from `s`.
std::string trim(const std::string& s) {
    size_t i = 0, j = s.size();
    while (i < j && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    while (j > i && std::isspace(static_cast<unsigned char>(s[j - 1]))) --j;
    if (i < j && (s[i] == '"' || s[i] == '\'') && s[j - 1] == s[i]) {
        ++i; --j;
    }
    return s.substr(i, j - i);
}

// Parse the image path list passed in over the FFI. Accepts either:
//   - A bare comma/newline-separated list:  "a.jpg, b.jpg, c.jpg"
//   - The Dart `List.toString()` format:    "[a.jpg, b.jpg, c.jpg]"
// Empty tokens are dropped.
std::vector<std::string> parseImagePaths(const char* image_paths) {
    std::vector<std::string> out;
    if (!image_paths) return out;

    std::string s(image_paths);
    // Drop surrounding brackets if present.
    {
        size_t i = 0, j = s.size();
        while (i < j && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        while (j > i && std::isspace(static_cast<unsigned char>(s[j - 1]))) --j;
        if (i < j && s[i] == '[' && s[j - 1] == ']') {
            s = s.substr(i + 1, j - i - 2);
        }
    }

    std::string token;
    for (char c : s) {
        if (c == ',' || c == '\n' || c == '\r') {
            std::string t = trim(token);
            if (!t.empty()) out.push_back(t);
            token.clear();
        } else {
            token.push_back(c);
        }
    }
    std::string t = trim(token);
    if (!t.empty()) out.push_back(t);
    return out;
}

// Lowercase + validate the model keyword. Returns "sift" for null/empty.
std::string resolveModel(const char* model) {
    std::string name = (model && *model) ? std::string(model) : std::string("sift");
    for (auto& c : name) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    static const std::array<const char*, 4> kAllowed = {"sift", "orb", "akaze", "brisk"};
    for (const char* n : kAllowed) {
        if (name == n) return name;
    }
    throw std::runtime_error("stitch: invalid model '" + name +
                             "'. Choose sift | orb | akaze | brisk");
}

// Map the `crop` keyword to the StitcherOptions::Crop enum.
// Recognised values: "none" | "bbox" | "inner". Defaults to "inner".
// The `fill` toggle is now an independent stitch() parameter and no longer
// piggybacks on this keyword.
void applyCrop(const char* crop, camera360::StitcherOptions& opt) {
    std::string c = (crop && *crop) ? std::string(crop) : std::string("inner");
    for (auto& ch : c) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));

    if      (c == "none") opt.crop_mode = camera360::StitcherOptions::Crop::None;
    else if (c == "bbox") opt.crop_mode = camera360::StitcherOptions::Crop::Bbox;
    else                  opt.crop_mode = camera360::StitcherOptions::Crop::Inner;
}

// Decode `image_paths` and load every JPEG/PNG from disk. Returns false (with
// a logged reason) when the list is too short or any file fails to decode.
bool loadImages(const char*               image_paths,
                std::vector<std::string>& names,
                std::vector<cv::Mat>&     images) {
    names = parseImagePaths(image_paths);
    if (names.size() < 2) {
        std::cerr << "stitch: need at least 2 image paths, got "
                  << names.size() << "\n";
        return false;
    }
    images.clear();
    images.reserve(names.size());
    for (const auto& n : names) {
        cv::Mat im = cv::imread(n, cv::IMREAD_COLOR);
        if (im.empty()) {
            std::cerr << "stitch: cannot read " << n << "\n";
            return false;
        }
        images.push_back(im);
    }
    std::cout << "stitch: loaded " << images.size() << " images, size: "
              << images[0].cols << "x" << images[0].rows << "\n";
    return true;
}

// Build StitcherOptions from the FFI knobs. Shared by both entry points.
camera360::StitcherOptions makeOptions(bool        loop_360,
                                       const char* crop,
                                       bool        fill,
                                       double      fill_ratio,
                                       int         fill_source,
                                       int         fill_blur,
                                       int         fill_fade,
                                       double      fill_blur_ratio,
                                       double      fill_fade_power,
                                       double      fill_vertical_camera_deg)
{
    camera360::StitcherOptions opt;
    applyCrop(crop, opt);
    opt.loop_360                  = loop_360;
    opt.fill_enabled              = fill;
    opt.fill_target_ratio         = fill_ratio;
    opt.fill_source_px            = fill_source;
    opt.fill_blur_kernel          = fill_blur;
    opt.fill_fade_px              = fill_fade;
    opt.fill_blur_ratio           = fill_blur_ratio;
    opt.fill_fade_power           = fill_fade_power;
    opt.fill_vertical_camera_deg  = fill_vertical_camera_deg;
    return opt;
}

// Apply the optional fallback pass and run the PanoramicStitcher. The caller
// owns `images` (kept by const reference) and donates `features` /
// `pairwise_matches` by move.
bool composeAndWrite(const std::vector<cv::Mat>&             images,
                     std::vector<cv::detail::ImageFeatures>  features,
                     std::vector<cv::detail::MatchesInfo>    pairwise_matches,
                     const camera360::StitcherOptions&       opt,
                     bool                                    fallback,
                     const std::string&                      out_path)
{
    using Clock = std::chrono::steady_clock;

    // Fill in any adjacent pair whose real match failed using the highest-
    // confidence pair's homography as a donor. Done before the stitcher
    // sees the matches so HomographyBasedEstimator + BundleAdjusterRay
    // treat the synthesized pairs identically to real ones.
    if (fallback) {
        camera360::FallbackOptions fopt;
        fopt.loop_360 = opt.loop_360;
        camera360::fillFailedAdjacentPairs(
            features, pairwise_matches,
            static_cast<float>(opt.conf_thresh), fopt);
    }

    const auto t_compose_start = Clock::now();
    camera360::PanoramicStitcher stitcher(opt);
    cv::Mat pano;
    if (!stitcher.stitch(images, std::move(features),
                         std::move(pairwise_matches), pano)) {
        std::cerr << "stitch: stitching failed\n";
        return false;
    }
    std::cout << "stitch: compose stage: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     Clock::now() - t_compose_start).count()
              << " ms\n";

    if (!cv::imwrite(out_path, pano)) {
        std::cerr << "stitch: imwrite failed for " << out_path << "\n";
        return false;
    }
    std::cout << "stitch: saved panorama " << out_path
              << "  (" << pano.cols << " x " << pano.rows << " px)\n";
    return true;
}

}  // namespace

extern "C" bool stitch(const char *image_paths,
                       const char *model,
                       const char *out,
                       bool        loop_360,
                       const char *crop,
                       bool        fallback,
                       bool        fill,
                       double      fill_ratio,
                       int         fill_source,
                       int         fill_blur,
                       int         fill_fade,
                       double      fill_blur_ratio,
                       double      fill_fade_power,
                       double      fill_vertical_camera_deg)
{
    using Clock = std::chrono::steady_clock;

    // Wrap the whole body: exceptions must not cross the C ABI.
    try {
        if (!out || !*out) {
            std::cerr << "stitch: missing output path\n";
            return false;
        }

        std::vector<std::string> names;
        std::vector<cv::Mat>     images;
        if (!loadImages(image_paths, names, images)) return false;

        const std::string model_name = resolveModel(model);
        std::cout << "stitch: model=" << model_name
                  << " loop_360=" << (loop_360 ? "true" : "false") << "\n";

        // ── Feature extraction + adjacent-pair matching ─────────────────────
        const auto t0 = Clock::now();
        camera360::CvMatcher matcher(model_name);
        std::vector<cv::detail::ImageFeatures> features;
        std::vector<cv::detail::MatchesInfo>    pairwise_matches;
        matcher.compute(images, features, pairwise_matches, loop_360);
        std::cout << "stitch: match stage: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         Clock::now() - t0).count()
                  << " ms\n";

        const camera360::StitcherOptions opt = makeOptions(
            loop_360, crop, fill, fill_ratio, fill_source, fill_blur,
            fill_fade, fill_blur_ratio, fill_fade_power, fill_vertical_camera_deg);

        return composeAndWrite(images, std::move(features),
                               std::move(pairwise_matches),
                               opt, fallback, out);
    } catch (const std::exception& e) {
        std::cerr << "stitch: exception: " << e.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "stitch: unknown exception\n";
        return false;
    }
}

extern "C" bool stitch_with_features(const char    *image_paths,
                                     const uint8_t *features_blob,
                                     size_t         features_blob_len,
                                     const char    *out,
                                     bool           loop_360,
                                     const char    *crop,
                                     bool           fallback,
                                     bool           fill,
                                     double         fill_ratio,
                                     int            fill_source,
                                     int            fill_blur,
                                     int            fill_fade,
                                     double         fill_blur_ratio,
                                     double         fill_fade_power,
                                     double         fill_vertical_camera_deg)
{
    using Clock = std::chrono::steady_clock;

    try {
        if (!out || !*out) {
            std::cerr << "stitch_with_features: missing output path\n";
            return false;
        }
        if (!features_blob || features_blob_len == 0) {
            std::cerr << "stitch_with_features: missing features blob\n";
            return false;
        }

        std::vector<std::string> names;
        std::vector<cv::Mat>     images;
        if (!loadImages(image_paths, names, images)) return false;

        std::cout << "stitch_with_features: loop_360="
                  << (loop_360 ? "true" : "false")
                  << "  blob=" << features_blob_len << " bytes\n";

        // ── Rebuild features + matches from the blob ────────────────────────
        const auto t0 = Clock::now();
        std::vector<cv::detail::ImageFeatures> features;
        std::vector<cv::detail::MatchesInfo>    pairwise_matches;
        if (!camera360::buildFromExternalBlob(features_blob, features_blob_len,
                                              images, features, pairwise_matches)) {
            return false;
        }
        std::cout << "stitch_with_features: rebuild stage: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         Clock::now() - t0).count()
                  << " ms\n";

        const camera360::StitcherOptions opt = makeOptions(
            loop_360, crop, fill, fill_ratio, fill_source, fill_blur,
            fill_fade, fill_blur_ratio, fill_fade_power, fill_vertical_camera_deg);

        return composeAndWrite(images, std::move(features),
                               std::move(pairwise_matches),
                               opt, fallback, out);
    } catch (const std::exception& e) {
        std::cerr << "stitch_with_features: exception: " << e.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "stitch_with_features: unknown exception\n";
        return false;
    }
}
