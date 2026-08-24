#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace scm {

struct Point {
    double x{};
    double y{};
};

struct RgbaImage {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels; // RGBA, top-to-bottom

    bool valid() const noexcept {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<std::size_t>(width) * height * 4;
    }
};

enum class SourceMode {
    AutoProduct,
    Alpha,
    DarkPixels,
    LightPixels,
};

enum class PathMode {
    All,
    OuterOnly,
    HolesOnly,
};

struct ContourOptions {
    SourceMode sourceMode{SourceMode::AutoProduct};
    PathMode pathMode{PathMode::All};
    double threshold{0.5};          // normalized 0..1
    double gaussianSigma{0.55};     // pixels, 0 disables filtering
    double backgroundDistance{0.045}; // normalized linear-RGB distance
    bool multiScaleSubpixel{true};
    std::array<double, 3> subpixelSigmas{0.35, 0.70, 1.20};
    double simplifyTolerance{0.12}; // pixels, guaranteed source-to-result bound
    double minArea{4.0};            // square pixels
    std::size_t maxNodes{500000};
};

struct Loop {
    std::vector<Point> points;
    double signedArea{};
    int nestingDepth{};
};

struct QualityReport {
    std::size_t sourceSegments{};
    std::size_t outputSegments{};
    std::size_t openPaths{};
    std::size_t selfIntersections{};
    std::size_t duplicateSegments{};
    double maxDeviationPixels{};
    bool nodeLimitExceeded{};

    bool topologyClean() const noexcept {
        return openPaths == 0 && selfIntersections == 0 && duplicateSegments == 0;
    }
};

struct ContourResult {
    std::vector<Loop> loops;
    QualityReport quality;
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

ContourResult traceContours(const RgbaImage& image, const ContourOptions& options);

} // namespace scm
