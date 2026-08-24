#pragma once

#include "contour_engine.hpp"
#include "production_pipeline.hpp"
#include "vgcore.hpp"

#include <string>

namespace scm {

struct RunOptions {
    ContourOptions contour;
    v9::PlannerOptions planner;
    double simplifyMillimeters{0.03};
    double minimumAreaSquareMillimeters{0.01};
    double offsetMillimeters{0.0};
    double verificationP95Pixels{1.25};
    double verificationMaximumPixels{2.25};
    double verificationCornerExtensionPixels{6.0};
};

struct RunSummary {
    QualityReport quality;
    std::size_t loopCount{};
    double maximumDeviationMillimeters{};
    std::size_t primitiveCount{};
    std::size_t lineCount{};
    std::size_t circleCount{};
    std::size_t arcCount{};
    std::size_t ellipseCount{};
    std::size_t bezierCount{};
};

bool runOnCorelSelection(VGCore::IVGApplication* app, const RunOptions& options,
                         RunSummary& summary, std::wstring& error);

} // namespace scm
