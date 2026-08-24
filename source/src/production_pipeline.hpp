#pragma once

#include "contour_engine.hpp"
#include "geometry_planner.hpp"

#include <string>
#include <vector>

namespace scm::v9 {

struct ProductionOptions {
    ContourOptions contour;
    PlannerOptions planner;
    double offsetPixels{};
    double verificationP95Pixels{0.45};
    double verificationMaximumPixels{1.20};
    // Analytic LINE/LINE intersections may legitimately extend beyond the
    // rounded or antialiased raster corner.  This is checked independently;
    // it never relaxes the ordinary supported-edge tolerance above.
    double verificationCornerExtensionPixels{6.0};
    bool enableOpenCvMultiscaleRefinement{true};
    bool enableCeresGlobalOptimization{true};
    bool rejectOnVerificationFailure{true};
};

struct ProductionQuality {
    double forwardP95Pixels{};
    double reverseP95Pixels{};
    double hausdorffPixels{};
    double rawReverseP95Pixels{};
    double rawHausdorffPixels{};
    double cornerReconstructionMaximumPixels{};
    std::size_t openPaths{};
    std::size_t selfIntersections{};
    std::size_t duplicateSegments{};
    std::size_t sourceSamples{};
    std::size_t outputPrimitives{};
    bool usedAnalyticOffset{};
    bool usedClipperFallback{};
    bool topologyClean{};
    bool cornerReconstructionPassed{};
    bool verificationPassed{};
    std::string topologyMessage;
};

struct ProductionResult {
    std::vector<PlannedPath> paths;
    QualityReport contourQuality;
    ProductionQuality quality;
    std::string error;
    explicit operator bool() const noexcept { return error.empty(); }
};

ProductionResult buildManufacturingPaths(const RgbaImage& image,
                                         const ProductionOptions& options);

} // namespace scm::v9
