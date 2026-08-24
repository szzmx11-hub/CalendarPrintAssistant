#include "production_pipeline.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

scm::RgbaImage whiteImage(int width, int height) {
    scm::RgbaImage image;
    image.width = width;
    image.height = height;
    image.pixels.assign(static_cast<std::size_t>(width) * height * 4, 255);
    return image;
}

void setRgb(scm::RgbaImage& image, int x, int y, unsigned char red,
            unsigned char green, unsigned char blue) {
    const std::size_t index = (static_cast<std::size_t>(y) * image.width + x) * 4;
    image.pixels[index] = red;
    image.pixels[index + 1] = green;
    image.pixels[index + 2] = blue;
}

scm::v9::ProductionOptions manufacturingOptions() {
    scm::v9::ProductionOptions options;
    options.contour.sourceMode = scm::SourceMode::AutoProduct;
    options.contour.pathMode = scm::PathMode::OuterOnly;
    options.contour.backgroundDistance = 0.03;
    options.contour.gaussianSigma = 0.55;
    options.contour.simplifyTolerance = 0.12;
    options.planner.cornerAngleDegrees = 18.0;
    options.verificationP95Pixels = 1.5;
    options.verificationMaximumPixels = 2.5;
    return options;
}

void printFailure(const char* name, const scm::v9::ProductionResult& result) {
    if (result) return;
    std::cerr << name << " diagnostics: error=\"" << result.error
              << "\", paths=" << result.paths.size()
              << ", primitives=" << result.quality.outputPrimitives
              << ", open=" << result.quality.openPaths
              << ", intersections=" << result.quality.selfIntersections
              << ", duplicates=" << result.quality.duplicateSegments
              << ", forward_p95=" << result.quality.forwardP95Pixels
              << ", reverse_p95=" << result.quality.reverseP95Pixels
              << ", hausdorff=" << result.quality.hausdorffPixels
              << ", raw_reverse_p95=" << result.quality.rawReverseP95Pixels
              << ", raw_hausdorff=" << result.quality.rawHausdorffPixels
              << ", corner_extension="
              << result.quality.cornerReconstructionMaximumPixels
              << ", corner_pass=" << result.quality.cornerReconstructionPassed << '\n';
}

void rectanglePipelineTest() {
    auto image = whiteImage(180, 130);
    for (int y = 24; y < 106; ++y)
        for (int x = 20; x < 160; ++x) setRgb(image, x, y, 120, 10, 18);
    auto options = manufacturingOptions();
    const auto result = scm::v9::buildManufacturingPaths(image, options);
    printFailure("rectangle", result);
    require(bool(result), "production rectangle failed");
    require(result.paths.size() == 1, "rectangle must produce one path");
    require(result.paths.front().quality.lines == 4, "rectangle must be four exact LINE primitives");
    require(result.paths.front().primitives.size() == 4, "rectangle line was fragmented");
    require(result.quality.topologyClean, "rectangle topology failed");
    require(result.quality.verificationPassed, "rectangle bidirectional verification failed");
    require(result.quality.cornerReconstructionPassed,
            "rectangle analytic corner reconstruction failed");
}

void cornerExtensionGateTest() {
    auto image = whiteImage(180, 130);
    for (int y = 24; y < 106; ++y)
        for (int x = 20; x < 160; ++x) setRgb(image, x, y, 120, 10, 18);
    auto options = manufacturingOptions();
    options.verificationCornerExtensionPixels = 0.05;
    options.rejectOnVerificationFailure = false;
    const auto result = scm::v9::buildManufacturingPaths(image, options);
    require(bool(result), "corner-gate diagnostic run failed");
    require(!result.quality.cornerReconstructionPassed,
            "corner extension gate did not reject an intentionally impossible limit");
    require(!result.quality.verificationPassed,
            "corner extension gate was bypassed by the supported-edge verifier");
}

void analyticOffsetTest() {
    auto image = whiteImage(180, 130);
    for (int y = 24; y < 106; ++y)
        for (int x = 20; x < 160; ++x) setRgb(image, x, y, 20, 70, 145);
    auto options = manufacturingOptions();
    options.offsetPixels = 2.0;
    options.rejectOnVerificationFailure = false;
    const auto result = scm::v9::buildManufacturingPaths(image, options);
    printFailure("analytic offset", result);
    require(bool(result), "analytic rectangle offset failed");
    require(result.quality.usedAnalyticOffset, "line polygon did not use analytic offset");
    require(!result.quality.usedClipperFallback, "line polygon unnecessarily used Clipper2");
    require(result.paths.front().primitives.size() == 4, "analytic offset fragmented rectangle");
}

void roundHolePipelineTest() {
    auto image = whiteImage(180, 160);
    for (int y = 16; y < 144; ++y)
        for (int x = 14; x < 166; ++x) setRgb(image, x, y, 150, 20, 25);
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
            if (std::hypot(x - 90.0, y - 78.0) < 24.0) setRgb(image, x, y, 255, 255, 255);
    auto options = manufacturingOptions();
    options.contour.pathMode = scm::PathMode::All;
    const auto result = scm::v9::buildManufacturingPaths(image, options);
    printFailure("round hole", result);
    require(bool(result), "round-hole production pipeline failed");
    require(result.paths.size() == 2, "product with hole must produce exterior plus one hole");
    bool foundCircle = false;
    for (const auto& path : result.paths)
        foundCircle = foundCircle || (path.primitives.size() == 1 &&
                                      path.primitives.front().type == scm::v9::PrimitiveType::Circle);
    require(foundCircle, "standard round hole was not one exact CIRCLE");
}

} // namespace

int main() {
    rectanglePipelineTest();
    cornerExtensionGateTest();
    analyticOffsetTest();
    roundHolePipelineTest();
    std::cout << "All production pipeline tests passed.\n";
    return 0;
}
