#pragma once

#include "contour_engine.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace scm::v9 {

enum class PrimitiveType {
    Line,
    Circle,
    CircularArc,
    Ellipse,
    EllipticArc,
    CubicBezier,
    CubicBSpline,
};

struct Primitive {
    PrimitiveType type{PrimitiveType::Line};
    Point start{};
    Point end{};
    Point control1{};
    Point control2{};
    Point center{};
    double radius{};
    double radiusX{};
    double radiusY{};
    double rotation{};
    double startAngle{};
    double sweepAngle{};
    double p95ErrorPixels{};
    double maxErrorPixels{};
    std::size_t sourceBegin{};
    std::size_t sourceEnd{};
};

struct PlannerOptions {
    // Primitive-local limits are deliberately tighter than the final
    // bidirectional verifier, but allow sub-pixel antialias/shadow wobble so a
    // visually straight manufacturing edge is not emitted as dozens of tiny
    // line segments.
    double lineP95Tolerance{0.55};
    double lineMaxTolerance{1.25};
    double arcP95Tolerance{0.60};
    double arcMaxTolerance{1.35};
    double ellipseP95Tolerance{0.65};
    double ellipseMaxTolerance{1.45};
    double bezierP95Tolerance{0.58};
    double bezierMaxTolerance{1.30};
    double ransacInlierTolerance{0.45};
    double cornerAngleDegrees{32.0};
    double minimumLineLength{8.0};
    double minimumArcSweepDegrees{12.0};
    double mdlPrimitivePenalty{1.0};
    std::size_t minimumSamplesPerPrimitive{5};
    std::size_t maximumRecursionDepth{16};
};

struct PlannerQuality {
    std::size_t sourcePoints{};
    std::size_t outputPrimitives{};
    std::size_t outputNodes{};
    std::size_t lines{};
    std::size_t circles{};
    std::size_t circularArcs{};
    std::size_t ellipses{};
    std::size_t ellipticArcs{};
    std::size_t cubicBeziers{};
    std::size_t cubicBSplines{};
    std::size_t corners{};
    double p95ErrorPixels{};
    double maxErrorPixels{};
    bool closed{};
};

struct PlannedPath {
    std::vector<Primitive> primitives;
    PlannerQuality quality;
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

// Plans a closed manufacturing path. The input must be ordered contour samples;
// the first sample must not be repeated at the end.
PlannedPath planClosedContour(const std::vector<Point>& contour,
                              const PlannerOptions& options = {});

const char* primitiveTypeName(PrimitiveType type) noexcept;

} // namespace scm::v9
