#include "contour_engine.hpp"
#include "geometry_planner.hpp"

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

scm::RgbaImage transparentImage(int w, int h) {
    scm::RgbaImage image;
    image.width = w;
    image.height = h;
    image.pixels.assign(static_cast<std::size_t>(w) * h * 4, 0);
    return image;
}

scm::RgbaImage whiteImage(int w, int h) {
    scm::RgbaImage image;
    image.width = w;
    image.height = h;
    image.pixels.assign(static_cast<std::size_t>(w) * h * 4, 255);
    return image;
}

void setRgba(scm::RgbaImage& image, int x, int y, unsigned char r, unsigned char g,
             unsigned char b, unsigned char a = 255) {
    const auto i = (static_cast<std::size_t>(y) * image.width + x) * 4;
    image.pixels[i] = r; image.pixels[i + 1] = g; image.pixels[i + 2] = b; image.pixels[i + 3] = a;
}

void setAlpha(scm::RgbaImage& image, int x, int y, unsigned char alpha) {
    const auto i = (static_cast<std::size_t>(y) * image.width + x) * 4;
    image.pixels[i] = image.pixels[i + 1] = image.pixels[i + 2] = 255;
    image.pixels[i + 3] = alpha;
}

void rectangleTest() {
    auto image = transparentImage(40, 30);
    for (int y = 7; y < 23; ++y) for (int x = 5; x < 35; ++x) setAlpha(image, x, y, 255);
    scm::ContourOptions options;
    options.sourceMode = scm::SourceMode::Alpha;
    options.gaussianSigma = 0.0;
    options.simplifyTolerance = 0.01;
    const auto result = scm::traceContours(image, options);
    require(bool(result), "rectangle trace returned an error");
    require(result.loops.size() == 1, "rectangle must produce one loop");
    require(result.quality.topologyClean(), "rectangle topology must be clean");
    require(result.loops.front().points.size() <= 8, "rectangle should simplify to a small polygon");
}

void donutTest() {
    auto image = transparentImage(80, 80);
    for (int y = 0; y < 80; ++y) for (int x = 0; x < 80; ++x) {
        const double r = std::hypot(x - 39.5, y - 39.5);
        if (r <= 28.0 && r >= 12.0) setAlpha(image, x, y, 255);
    }
    scm::ContourOptions options;
    options.sourceMode = scm::SourceMode::Alpha;
    options.gaussianSigma = 0.55;
    options.simplifyTolerance = 0.2;
    const auto result = scm::traceContours(image, options);
    require(bool(result), "donut trace returned an error");
    require(result.loops.size() == 2, "donut must preserve outer and hole loops");
    require(result.quality.topologyClean(), "donut topology must be clean");
    require(result.quality.maxDeviationPixels <= 0.21, "simplification exceeded configured error");
}

void diagonalAmbiguityTest() {
    auto image = transparentImage(24, 24);
    for (int y = 2; y < 22; ++y) for (int x = 2; x < 22; ++x)
        if (((x / 3) + (y / 3)) % 2 == 0) setAlpha(image, x, y, 255);
    scm::ContourOptions options;
    options.sourceMode = scm::SourceMode::Alpha;
    options.gaussianSigma = 0.0;
    options.simplifyTolerance = 0.0;
    options.minArea = 0.1;
    const auto result = scm::traceContours(image, options);
    require(bool(result), "ambiguous-cell trace returned an error");
    require(result.quality.openPaths == 0, "ambiguous cells must not create open paths");
    require(result.quality.selfIntersections == 0, "ambiguous cells must not self-intersect");
}

void randomizedTopologyTest() {
    std::uint32_t state = 0x51C0A7u;
    for (int sample = 0; sample < 40; ++sample) {
        auto image = transparentImage(32, 32);
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            state = state * 1664525u + 1013904223u;
            if ((state >> 24) > 178) setAlpha(image, x, y, 255);
        }
        scm::ContourOptions options;
        options.sourceMode = scm::SourceMode::Alpha;
        options.gaussianSigma = sample % 2 ? 0.45 : 0.0;
        options.simplifyTolerance = sample % 3 ? 0.15 : 0.0;
        options.minArea = 0.1;
        const auto result = scm::traceContours(image, options);
        require(bool(result), "random mask trace returned an error");
        require(result.quality.openPaths == 0, "random mask created an open path");
        require(result.quality.selfIntersections == 0, "random mask created an intersection");
        require(result.quality.duplicateSegments == 0, "random mask created a duplicate segment");
    }
}

void edgeTouchingBlurTest() {
    auto image = transparentImage(40, 40);
    for (int y = 0; y < 18; ++y) for (int x = 0; x < 24; ++x) setAlpha(image, x, y, 255);
    scm::ContourOptions options;
    options.sourceMode = scm::SourceMode::Alpha;
    options.gaussianSigma = 3.0;
    options.threshold = 0.15;
    options.simplifyTolerance = 0.1;
    options.minArea = 0.1;
    const auto result = scm::traceContours(image, options);
    require(bool(result), "edge-touching blur trace returned an error");
    require(result.loops.size() == 1, "edge-touching shape must remain one closed loop");
    require(result.quality.topologyClean(), "padded blur must preserve topology at image edge");
}

void whiteBackgroundProductTest() {
    auto image = whiteImage(120, 90);
    for (int y = 15; y < 75; ++y) for (int x = 18; x < 102; ++x)
        setRgba(image, x, y, 155, 18, 22);
    // White decoration inside the product must not become an outer cutting path.
    for (int y = 30; y < 60; ++y) for (int x = 38; x < 82; ++x)
        setRgba(image, x, y, 255, 255, 255);
    // A detached dark compression/noise island must be rejected.
    setRgba(image, 5, 5, 30, 30, 30);
    scm::ContourOptions options;
    options.sourceMode = scm::SourceMode::AutoProduct;
    options.pathMode = scm::PathMode::OuterOnly;
    options.backgroundDistance = 0.03;
    options.simplifyTolerance = 0.15;
    const auto result = scm::traceContours(image, options);
    require(bool(result), "auto white-background trace returned an error");
    require(result.loops.size() == 1, "auto product mode must emit one exterior silhouette");
    require(result.quality.topologyClean(), "auto product silhouette topology must be clean");
    const auto plan = scm::v9::planClosedContour(result.loops.front().points);
    require(bool(plan), "auto product rectangle planning returned an error");
    if (plan.quality.lines != 4 || plan.quality.outputPrimitives > 12) {
        std::cerr << "auto rectangle diagnostics: samples=" << result.loops.front().points.size()
                  << " primitives=" << plan.quality.outputPrimitives
                  << " lines=" << plan.quality.lines
                  << " arcs=" << plan.quality.circularArcs
                  << " ellipses=" << plan.quality.ellipticArcs
                  << " beziers=" << plan.quality.cubicBeziers << '\n';
    }
    require(plan.quality.lines == 4, "auto product rectangle must become four long lines");
    require(plan.quality.outputPrimitives == 4, "auto product rectangle must not be fragmented");
    require(plan.quality.cubicBeziers == 0, "auto product rectangle must not use Beziers");
}

void whiteBackgroundRoundHoleTest() {
    auto image = whiteImage(140, 120);
    for (int y = 10; y < 110; ++y) for (int x = 10; x < 130; ++x)
        setRgba(image, x, y, 145, 18, 25);
    for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x)
        if (std::hypot(x - 70.0, y - 58.0) < 17.0) setRgba(image, x, y, 255, 255, 255);
    // Irregular internal white decoration is deliberately not a cutting hole.
    for (int y = 78; y < 96; ++y) for (int x = 25; x < 55; ++x)
        if (x < 42 || y > 88) setRgba(image, x, y, 255, 255, 255);
    scm::ContourOptions options;
    options.sourceMode = scm::SourceMode::AutoProduct;
    options.pathMode = scm::PathMode::All;
    options.backgroundDistance = 0.03;
    options.simplifyTolerance = 0.18;
    const auto result = scm::traceContours(image, options);
    require(bool(result), "auto round-hole trace returned an error");
    require(result.loops.size() == 2, "auto product must preserve one round manufactured hole only");
    require(result.quality.topologyClean(), "auto round-hole topology must be clean");
}

std::vector<scm::Point> sampledRectangle(double left, double top, double right, double bottom, int samplesPerSide) {
    std::vector<scm::Point> points;
    auto side = [&](scm::Point a, scm::Point b) {
        for (int i = 0; i < samplesPerSide; ++i) {
            const double t = static_cast<double>(i) / samplesPerSide;
            const double staircase = (i % 2 == 0 ? 0.07 : -0.07);
            const double nx = -(b.y - a.y) / std::hypot(b.x - a.x, b.y - a.y);
            const double ny = (b.x - a.x) / std::hypot(b.x - a.x, b.y - a.y);
            points.push_back({a.x + (b.x - a.x) * t + nx * staircase,
                              a.y + (b.y - a.y) * t + ny * staircase});
        }
    };
    side({left, top}, {right, top});
    side({right, top}, {right, bottom});
    side({right, bottom}, {left, bottom});
    side({left, bottom}, {left, top});
    return points;
}

void primitiveRectangleTest() {
    scm::v9::PlannerOptions options;
    options.cornerAngleDegrees = 18.0;
    const auto plan = scm::v9::planClosedContour(sampledRectangle(3, 5, 103, 65, 50), options);
    require(bool(plan), "V9 rectangle planning returned an error");
    require(plan.quality.lines == 4, "V9 rectangle must contain exactly four long lines");
    require(plan.quality.outputPrimitives == 4, "V9 rectangle must not contain short line fragments");
    require(plan.quality.cubicBeziers == 0, "V9 rectangle must not use Beziers");
}

void primitiveCircleTest() {
    std::vector<scm::Point> points;
    for (int i = 0; i < 360; ++i) {
        const double angle = i * 3.14159265358979323846 / 180.0;
        const double noise = (i % 3 - 1) * 0.045;
        points.push_back({80.0 + (35.0 + noise) * std::cos(angle),
                          60.0 + (35.0 + noise) * std::sin(angle)});
    }
    const auto plan = scm::v9::planClosedContour(points);
    require(bool(plan), "V9 circle planning returned an error");
    require(plan.quality.circles == 1, "V9 round hole must be one exact circle");
    require(plan.quality.outputPrimitives == 1, "V9 round hole must not be line segments");
    require(plan.quality.maxErrorPixels < 0.10, "V9 fitted circle exceeded expected error");
}

void primitiveEllipseTest() {
    std::vector<scm::Point> points;
    const double rotation = 0.37;
    for (int i = 0; i < 360; ++i) {
        const double angle = i * 3.14159265358979323846 / 180.0;
        const double x = 48.0 * std::cos(angle);
        const double y = 23.0 * std::sin(angle);
        const double noise = (i % 5 - 2) * 0.025;
        points.push_back({110.0 + std::cos(rotation) * x - std::sin(rotation) * y + noise,
                          90.0 + std::sin(rotation) * x + std::cos(rotation) * y - noise});
    }
    const auto plan = scm::v9::planClosedContour(points);
    require(bool(plan), "V9 ellipse planning returned an error");
    require(plan.quality.ellipses == 1, "V9 ellipse must be one exact ellipse primitive");
    require(plan.quality.outputPrimitives == 1, "V9 ellipse must not be fragmented");
    require(plan.quality.maxErrorPixels < 0.15, "V9 fitted ellipse exceeded expected error");
}

void primitiveDiagonalLineTest() {
    std::vector<scm::Point> points;
    for (int i = 0; i <= 180; ++i) {
        const double x = 10.0 + i;
        const double y = 20.0 + i * 0.63 + (i % 2 == 0 ? 0.12 : -0.12);
        points.push_back({x, y});
    }
    // Close with a distant, simple return edge to make a thin manufacturing loop.
    for (int i = 180; i >= 0; --i) points.push_back({10.0 + i, 26.0 + i * 0.63});
    scm::v9::PlannerOptions options;
    options.cornerAngleDegrees = 12.0;
    const auto plan = scm::v9::planClosedContour(points, options);
    require(bool(plan), "V9 diagonal planning returned an error");
    require(plan.quality.lines <= 4, "V9 diagonal edges were fragmented into short lines");
    require(plan.quality.cubicBeziers == 0, "V9 diagonal lines must not become Beziers");
}

void primitiveLongStraightEdgeNoiseTest() {
    std::vector<scm::Point> points;
    auto side = [&](scm::Point a, scm::Point b) {
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        const double nx = -(b.y - a.y) / length;
        const double ny = (b.x - a.x) / length;
        for (int i = 0; i < 240; ++i) {
            const double t = static_cast<double>(i) / 240.0;
            const double rasterWobble = (i % 4 < 2 ? 0.38 : -0.38);
            points.push_back({a.x + (b.x - a.x) * t + nx * rasterWobble,
                              a.y + (b.y - a.y) * t + ny * rasterWobble});
        }
    };
    side({10, 10}, {310, 10});
    side({310, 10}, {310, 190});
    side({310, 190}, {10, 190});
    side({10, 190}, {10, 10});
    scm::v9::PlannerOptions options;
    options.cornerAngleDegrees = 18.0;
    const auto plan = scm::v9::planClosedContour(points, options);
    require(bool(plan), "noisy manufacturing rectangle planning returned an error");
    if (plan.quality.lines != 4 || plan.quality.outputPrimitives != 4) {
        std::cerr << "noisy rectangle diagnostics: primitives=" << plan.quality.outputPrimitives
                  << " lines=" << plan.quality.lines << " arcs=" << plan.quality.circularArcs
                  << " beziers=" << plan.quality.cubicBeziers
                  << " corners=" << plan.quality.corners << '\n';
    }
    require(plan.quality.lines == 4, "sub-pixel raster wobble fragmented a straight edge");
    require(plan.quality.outputPrimitives <= 12,
            "sub-pixel corner transitions created an excessive primitive count");
}

} // namespace

int main() {
    rectangleTest();
    donutTest();
    diagonalAmbiguityTest();
    randomizedTopologyTest();
    edgeTouchingBlurTest();
    whiteBackgroundProductTest();
    whiteBackgroundRoundHoleTest();
    primitiveRectangleTest();
    primitiveCircleTest();
    primitiveEllipseTest();
    primitiveDiagonalLineTest();
    primitiveLongStraightEdgeNoiseTest();
    std::cout << "All contour engine tests passed.\n";
    return 0;
}
