// Local macOS test driver for the umbrella stitcher.
//
// Discovers images in an input directory (sorted numerically by filename),
// builds the comma-separated path string that Stitcher.cpp expects, then
// calls the extern-C stitch() function directly.
//
// Build (from the workspace root):
//   cmake -S test_macos -B test_macos/build -DCMAKE_BUILD_TYPE=Release
//   cmake --build test_macos/build -j$(sysctl -n hw.logicalcpu)
//
// Run (defaults to ../test_input/bathroom, output panorama_out.jpg):
//   ./test_macos/build/stitch_test
//
// All parameters can be overridden on the command line:
//   ./test_macos/build/stitch_test \
//       --input ../test_input/bathroom \
//       --out   my_pano.jpg           \
//       --model sift                  \
//       --crop  fill                  \
//       --no-fallback

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "Stitcher.h"

namespace fs = std::filesystem;

// ── Defaults (mirror custom_cpp/src/main.cpp:25-42) ─────────────────────────

struct Args {
    std::string input      = "../test_input/bathroom";
    std::string model      = "sift";
    std::string out        = "panorama_out.jpg";
    bool        loop_360      = true;
    bool        loop_closure  = true;
    std::string crop       = "inner";
    bool        fallback   = true;
    bool        fill       = false;
    double      fill_ratio              = 2.0;
    int         fill_source             = 30;
    int         fill_blur               = 401;
    int         fill_fade               = 50;
    double      fill_blur_ratio         = 0.01;
    double      fill_fade_power         = 5.0;
    // Phone vertical pose at capture time, on the panorama UI's 0..180 scale
    // (90 = camera at the horizon, < 90 = tilted down, > 90 = tilted up).
    // Biases the top/bottom fill split — 90 gives the legacy symmetric split.
    double      fill_vertical_camera_deg = 90.0;
    bool        fill_flat_use_edge      = true;
    int         fill_flat_b             = 128;
    int         fill_flat_g             = 128;
    int         fill_flat_r             = 128;
};

// ── CLI parsing ──────────────────────────────────────────────────────────────

void printUsage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [options]\n"
        << "\n"
        << "  --input  DIR      image folder (jpg/jpeg/png, sorted numerically)\n"
        << "                    default: ../test_input/bathroom\n"
        << "  --out    FILE     output path (default: panorama_out.jpg)\n"
        << "  --model  NAME     sift | orb | akaze | brisk  (default: sift)\n"
        << "  --crop   MODE     none | bbox | inner          (default: inner)\n"
        << "  --no-loop         disable closing pair for 360\n"
        << "  --no-loop-closure disable wrap-seam closure (BA boost, ring canvas)\n"
        << "  --no-fallback     disable homography fallback for failed pairs\n"
        << "  --fill            enable post-crop blur->flat top/bottom fill (default)\n"
        << "  --no-fill         disable the post-crop fill\n"
        << "  --fill-ratio   R  target W/H aspect (default 2.0)\n"
        << "  --fill-source  N  rows sampled from each edge (default 30)\n"
        << "  --fill-blur    K  horizontal Gaussian kernel, forced odd (default 401)\n"
        << "  --fill-blur-ratio R  blur fraction [0,1] (default 0.01)\n"
        << "  --fill-fade    N  pano rows blended at join (default 50)\n"
        << "  --fill-fade-power P  flat-colour layers in fade >=1 (default 5.0)\n"
        << "  --fill-vertical-camera-deg D\n"
        << "                    phone pose during capture, 0..180 scale\n"
        << "                    (90 = horizon, <90 = tilted down, >90 = tilted up)\n"
        << "                    default 90 → symmetric top/bottom split\n"
        << "  --fill-flat-gray  use fixed 128,128,128 BGR flat fill (not edge mean)\n"
        << "  --fill-flat-b B   flat fill blue  channel 0..255 (with --no-fill-flat-edge)\n"
        << "  --fill-flat-g G   flat fill green channel 0..255\n"
        << "  --fill-flat-r R   flat fill red   channel 0..255\n"
        << "  --no-fill-flat-edge  use --fill-flat-b/g/r instead of edge mean\n"
        << "  -h, --help        show this message\n";
}

bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto next = [&](std::string& target) -> bool {
            if (i + 1 >= argc) { std::cerr << s << " requires an argument\n"; return false; }
            target = argv[++i]; return true;
        };
        std::string tmp;
        if      (s == "--input")           { if (!next(a.input))   return false; }
        else if (s == "--out")             { if (!next(a.out))     return false; }
        else if (s == "--model")           { if (!next(a.model))   return false; }
        else if (s == "--crop")            {
            if (!next(tmp)) return false;
            if (tmp == "fill") {
                std::cerr << "--crop fill is no longer a crop mode. "
                             "Use --crop none|bbox|inner together with --fill / --no-fill.\n";
                return false;
            }
            if (tmp != "none" && tmp != "bbox" && tmp != "inner") {
                std::cerr << "--crop must be none|bbox|inner\n"; return false;
            }
            a.crop = tmp;
        }
        else if (s == "--no-loop")         { a.loop_360     = false; }
        else if (s == "--no-loop-closure") { a.loop_closure = false; }
        else if (s == "--no-fallback")     { a.fallback  = false; }
        else if (s == "--fill")            { a.fill      = true;  }
        else if (s == "--no-fill")         { a.fill      = false; }
        else if (s == "--fill-ratio")      { if (!next(tmp)) return false; a.fill_ratio      = std::stod(tmp); }
        else if (s == "--fill-source")     { if (!next(tmp)) return false; a.fill_source     = std::stoi(tmp); }
        else if (s == "--fill-blur")       { if (!next(tmp)) return false; a.fill_blur       = std::stoi(tmp); }
        else if (s == "--fill-blur-ratio") {
            if (!next(tmp)) return false;
            a.fill_blur_ratio = std::stod(tmp);
            if (a.fill_blur_ratio < 0.0 || a.fill_blur_ratio > 1.0) {
                std::cerr << "--fill-blur-ratio must be in [0, 1]\n"; return false;
            }
        }
        else if (s == "--fill-fade")       { if (!next(tmp)) return false; a.fill_fade       = std::stoi(tmp); }
        else if (s == "--fill-fade-power") {
            if (!next(tmp)) return false;
            a.fill_fade_power = std::stod(tmp);
            if (a.fill_fade_power < 1.0) {
                std::cerr << "--fill-fade-power must be >= 1.0\n"; return false;
            }
        }
        else if (s == "--fill-vertical-camera-deg") {
            if (!next(tmp)) return false;
            a.fill_vertical_camera_deg = std::stod(tmp);
            if (a.fill_vertical_camera_deg < 0.0 || a.fill_vertical_camera_deg > 180.0) {
                std::cerr << "--fill-vertical-camera-deg must be in [0, 180]\n"; return false;
            }
        }
        else if (s == "--fill-flat-gray") {
            a.fill_flat_use_edge = false;
            a.fill_flat_b = a.fill_flat_g = a.fill_flat_r = 128;
        }
        else if (s == "--no-fill-flat-edge") { a.fill_flat_use_edge = false; }
        else if (s == "--fill-flat-b") {
            if (!next(tmp)) return false;
            a.fill_flat_use_edge = false;
            a.fill_flat_b = std::stoi(tmp);
        }
        else if (s == "--fill-flat-g") {
            if (!next(tmp)) return false;
            a.fill_flat_use_edge = false;
            a.fill_flat_g = std::stoi(tmp);
        }
        else if (s == "--fill-flat-r") {
            if (!next(tmp)) return false;
            a.fill_flat_use_edge = false;
            a.fill_flat_r = std::stoi(tmp);
        }
        else if (s == "-h" || s == "--help") { printUsage(argv[0]); return false; }
        else { std::cerr << "Unknown argument: " << s << "\n"; return false; }
    }
    return true;
}

// ── Image discovery ──────────────────────────────────────────────────────────

// Returns the numeric value embedded in a filename stem, or -1 when the stem
// contains no digits (sorts non-numeric names to the end).
static long long stemNumeric(const fs::path& p) {
    std::string stem = p.stem().string();
    std::string digits;
    for (char c : stem) if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
    if (digits.empty()) return -1;
    try { return std::stoll(digits); } catch (...) { return -1; }
}

static bool isImage(const fs::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    static const std::array<const char*, 3> kExts = {".jpg", ".jpeg", ".png"};
    for (const char* e : kExts) if (ext == e) return true;
    return false;
}

std::string buildImagePathList(const std::string& dir_path) {
    fs::path dir = dir_path;
    if (!fs::is_directory(dir)) {
        throw std::runtime_error("Input path is not a directory: " + dir_path);
    }

    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && isImage(entry.path()))
            paths.push_back(entry.path());
    }

    if (paths.empty())
        throw std::runtime_error("No image files found in: " + dir_path);

    std::sort(paths.begin(), paths.end(), [](const fs::path& a, const fs::path& b) {
        long long na = stemNumeric(a), nb = stemNumeric(b);
        if (na != nb) return na < nb;
        return a.string() < b.string();
    });

    std::string list;
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) list += ", ";
        list += fs::absolute(paths[i]).string();
    }
    return list;
}

// ── Entry point ──────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) {
        printUsage(argv[0]);
        return 1;
    }

    std::string image_list;
    try {
        image_list = buildImagePathList(args.input);
    } catch (const std::exception& e) {
        std::cerr << "Error discovering images: " << e.what() << "\n";
        return 1;
    }

    const size_t n_images = std::count(image_list.begin(), image_list.end(), ',') + 1;
    std::cout << "Input   : " << args.input << "  (" << n_images << " images)\n"
              << "Model   : " << args.model  << "\n"
              << "Crop    : " << args.crop   << "\n"
              << "Loop360 : " << (args.loop_360 ? "yes" : "no") << "\n"
              << "Closure : " << (args.loop_closure ? "yes" : "no") << "\n"
              << "Fallback: " << (args.fallback  ? "yes" : "no") << "\n"
              << "Fill    : " << (args.fill     ? "yes" : "no") << "\n"
              << "V-Cam   : " << args.fill_vertical_camera_deg << " deg\n"
              << "Output  : " << args.out    << "\n"
              << std::string(60, '-') << "\n";

    const auto t0 = std::chrono::steady_clock::now();

    const bool ok = stitch(
        image_list.c_str(),
        args.model.c_str(),
        args.out.c_str(),
        args.loop_360,
        args.loop_closure,
        args.crop.c_str(),
        args.fallback,
        args.fill,
        args.fill_ratio,
        args.fill_source,
        args.fill_blur,
        args.fill_fade,
        args.fill_blur_ratio,
        args.fill_fade_power,
        args.fill_vertical_camera_deg,
        args.fill_flat_use_edge,
        args.fill_flat_b,
        args.fill_flat_g,
        args.fill_flat_r
    );

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    std::cout << std::string(60, '-') << "\n";
    if (ok) {
        std::cout << "Done in " << ms << " ms  →  " << args.out << "\n";
        return 0;
    } else {
        std::cerr << "Stitching failed after " << ms << " ms\n";
        return 2;
    }
}
