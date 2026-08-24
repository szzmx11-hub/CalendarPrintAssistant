#include "contour_engine.hpp"
#include "geometry_planner.hpp"

#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::cerr << "usage: sample_image_check image.rgba width height [overlay.svg]\n";
        return 2;
    }
    scm::RgbaImage image;
    image.width = std::atoi(argv[2]);
    image.height = std::atoi(argv[3]);
    std::ifstream input(argv[1], std::ios::binary);
    image.pixels.assign(std::istreambuf_iterator<char>(input), {});
    if (!image.valid()) {
        std::cerr << "invalid raw RGBA input\n";
        return 2;
    }

    scm::ContourOptions options;
    options.sourceMode = scm::SourceMode::AutoProduct;
    options.pathMode = scm::PathMode::OuterOnly;
    options.gaussianSigma = 0.55;
    options.simplifyTolerance = 0.12;
    options.minArea = 16.0;
    const auto contours = scm::traceContours(image, options);
    if (!contours) {
        std::cerr << contours.error << '\n';
        return 1;
    }

    std::size_t primitives = 0, lines = 0, circles = 0, ellipses = 0, beziers = 0, corners = 0;
    double maxError = 0.0;
    std::vector<scm::v9::PlannedPath> plans;
    for (const auto& loop : contours.loops) {
        const auto plan = scm::v9::planClosedContour(loop.points);
        if (!plan) {
            std::cerr << plan.error << '\n';
            return 1;
        }
        primitives += plan.quality.outputPrimitives;
        lines += plan.quality.lines;
        circles += plan.quality.circles + plan.quality.circularArcs;
        ellipses += plan.quality.ellipses + plan.quality.ellipticArcs;
        beziers += plan.quality.cubicBeziers + plan.quality.cubicBSplines;
        corners += plan.quality.corners;
        maxError = std::max(maxError, plan.quality.maxErrorPixels);
        plans.push_back(plan);
    }
    std::cout << std::fixed << std::setprecision(4)
              << "loops=" << contours.loops.size()
              << " samples=" << contours.quality.outputSegments
              << " primitives=" << primitives
              << " lines=" << lines
              << " circles_arcs=" << circles
              << " ellipses_arcs=" << ellipses
              << " beziers=" << beziers
              << " corners=" << corners
              << " topology=" << (contours.quality.topologyClean() ? "PASS" : "FAIL")
              << " contour_max_px=" << contours.quality.maxDeviationPixels
              << " planner_max_px=" << maxError << '\n';
    if (argc >= 5) {
        std::ofstream svg(argv[4]);
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "
            << image.width << ' ' << image.height << "\">\n"
            << "<image href=\"sample.png\" width=\"" << image.width << "\" height=\""
            << image.height << "\" opacity=\"0.45\"/>\n"
            << "<g fill=\"none\" stroke=\"red\" stroke-width=\"1\">\n";
        for (const auto& plan : plans) {
            if (plan.primitives.empty()) continue;
            svg << "<path d=\"M " << plan.primitives.front().start.x << ' '
                << plan.primitives.front().start.y << ' ';
            for (const auto& p : plan.primitives) {
                if (p.type == scm::v9::PrimitiveType::Line)
                    svg << "L " << p.end.x << ' ' << p.end.y << ' ';
                else if (p.type == scm::v9::PrimitiveType::CubicBezier ||
                         p.type == scm::v9::PrimitiveType::CubicBSpline)
                    svg << "C " << p.control1.x << ' ' << p.control1.y << ' '
                        << p.control2.x << ' ' << p.control2.y << ' '
                        << p.end.x << ' ' << p.end.y << ' ';
                else {
                    const bool ellipse = p.type == scm::v9::PrimitiveType::Ellipse ||
                                         p.type == scm::v9::PrimitiveType::EllipticArc;
                    const double rx = ellipse ? p.radiusX : p.radius;
                    const double ry = ellipse ? p.radiusY : p.radius;
                    const double rotation = ellipse ? p.rotation * 180.0 / 3.14159265358979323846 : 0.0;
                    const int large = std::abs(p.sweepAngle) > 3.14159265358979323846 ? 1 : 0;
                    const int sweep = p.sweepAngle >= 0.0 ? 1 : 0;
                    if (p.type == scm::v9::PrimitiveType::Circle || p.type == scm::v9::PrimitiveType::Ellipse) {
                        const double midAngle = p.startAngle + p.sweepAngle * 0.5;
                        const double c = std::cos(p.rotation), s = std::sin(p.rotation);
                        const double mx = p.center.x + c * rx * std::cos(midAngle) - s * ry * std::sin(midAngle);
                        const double my = p.center.y + s * rx * std::cos(midAngle) + c * ry * std::sin(midAngle);
                        svg << "A " << rx << ' ' << ry << ' ' << rotation << " 0 " << sweep << ' '
                            << mx << ' ' << my << ' '
                            << "A " << rx << ' ' << ry << ' ' << rotation << " 0 " << sweep << ' '
                            << p.start.x << ' ' << p.start.y << ' ';
                    } else {
                        svg << "A " << rx << ' ' << ry << ' ' << rotation << ' ' << large << ' ' << sweep << ' '
                            << p.end.x << ' ' << p.end.y << ' ';
                    }
                }
            }
            svg << "Z\"/>\n";
        }
        svg << "</g></svg>\n";
    }
    return contours.quality.topologyClean() ? 0 : 1;
}
