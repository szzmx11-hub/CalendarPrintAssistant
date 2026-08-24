#include "production_pipeline.hpp"

#if SCM_V9_PRODUCTION
#include <Eigen/Core>
#include <Eigen/Eigenvalues>
#include <boost/geometry.hpp>
#include <ceres/ceres.h>
#include <clipper2/clipper.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>

namespace scm::v9 {

#if SCM_V9_PRODUCTION
namespace {

namespace bg = boost::geometry;
constexpr double kPi = 3.1415926535897932384626433832795;

double pointDistance(const Point& a, const Point& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

Point pointOnEllipse(const Primitive& primitive, double angle) {
    const double cosine = std::cos(primitive.rotation);
    const double sine = std::sin(primitive.rotation);
    const double x = primitive.radiusX * std::cos(angle);
    const double y = primitive.radiusY * std::sin(angle);
    return {primitive.center.x + cosine * x - sine * y,
            primitive.center.y + sine * x + cosine * y};
}

Point pointOnPrimitive(const Primitive& primitive, double t) {
    if (primitive.type == PrimitiveType::Line) {
        return {primitive.start.x + (primitive.end.x - primitive.start.x) * t,
                primitive.start.y + (primitive.end.y - primitive.start.y) * t};
    }
    if (primitive.type == PrimitiveType::CubicBezier || primitive.type == PrimitiveType::CubicBSpline) {
        const double u = 1.0 - t;
        const double b0 = u * u * u;
        const double b1 = 3.0 * u * u * t;
        const double b2 = 3.0 * u * t * t;
        const double b3 = t * t * t;
        return {b0 * primitive.start.x + b1 * primitive.control1.x +
                    b2 * primitive.control2.x + b3 * primitive.end.x,
                b0 * primitive.start.y + b1 * primitive.control1.y +
                    b2 * primitive.control2.y + b3 * primitive.end.y};
    }
    const double angle = primitive.startAngle + primitive.sweepAngle * t;
    if (primitive.type == PrimitiveType::Circle || primitive.type == PrimitiveType::CircularArc) {
        return {primitive.center.x + primitive.radius * std::cos(angle),
                primitive.center.y + primitive.radius * std::sin(angle)};
    }
    return pointOnEllipse(primitive, angle);
}

std::vector<Point> samplePath(const PlannedPath& path, double maximumStep = 0.35) {
    std::vector<Point> samples;
    for (const Primitive& primitive : path.primitives) {
        double length = pointDistance(primitive.start, primitive.end);
        if (primitive.type == PrimitiveType::Circle || primitive.type == PrimitiveType::CircularArc)
            length = std::abs(primitive.sweepAngle) * primitive.radius;
        else if (primitive.type == PrimitiveType::Ellipse || primitive.type == PrimitiveType::EllipticArc)
            length = std::abs(primitive.sweepAngle) * std::max(primitive.radiusX, primitive.radiusY);
        else if (primitive.type == PrimitiveType::CubicBezier || primitive.type == PrimitiveType::CubicBSpline)
            length = pointDistance(primitive.start, primitive.control1) +
                     pointDistance(primitive.control1, primitive.control2) +
                     pointDistance(primitive.control2, primitive.end);
        const int pieces = std::clamp(static_cast<int>(std::ceil(length / maximumStep)), 1, 32768);
        if (samples.empty()) samples.push_back(pointOnPrimitive(primitive, 0.0));
        for (int i = 1; i <= pieces; ++i)
            samples.push_back(pointOnPrimitive(primitive, static_cast<double>(i) / pieces));
    }
    if (!samples.empty() && pointDistance(samples.front(), samples.back()) > 1e-7)
        samples.push_back(samples.front());
    return samples;
}

float bilinear(const cv::Mat& image, double x, double y) {
    x = std::clamp(x, 0.0, static_cast<double>(image.cols - 1));
    y = std::clamp(y, 0.0, static_cast<double>(image.rows - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, image.cols - 1);
    const int y1 = std::min(y0 + 1, image.rows - 1);
    const double tx = x - x0;
    const double ty = y - y0;
    const double a = image.at<float>(y0, x0) * (1.0 - tx) + image.at<float>(y0, x1) * tx;
    const double b = image.at<float>(y1, x0) * (1.0 - tx) + image.at<float>(y1, x1) * tx;
    return static_cast<float>(a * (1.0 - ty) + b * ty);
}

std::vector<Point> refineMultiscale(const RgbaImage& image, const std::vector<Point>& contour) {
    if (contour.size() < 8) return contour;
    cv::Mat gray(image.height, image.width, CV_32F);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * image.width + x) * 4;
            const float alpha = image.pixels[index + 3] / 255.0F;
            const float luminance = (0.2126F * image.pixels[index] +
                                     0.7152F * image.pixels[index + 1] +
                                     0.0722F * image.pixels[index + 2]) / 255.0F;
            gray.at<float>(y, x) = luminance * alpha + (1.0F - alpha);
        }
    }
    std::array<cv::Mat, 3> magnitude;
    constexpr std::array<double, 3> sigmas{0.55, 1.0, 1.6};
    for (std::size_t scale = 0; scale < sigmas.size(); ++scale) {
        cv::Mat smooth, gx, gy;
        cv::GaussianBlur(gray, smooth, cv::Size(), sigmas[scale], sigmas[scale], cv::BORDER_REPLICATE);
        cv::Scharr(smooth, gx, CV_32F, 1, 0);
        cv::Scharr(smooth, gy, CV_32F, 0, 1);
        cv::magnitude(gx, gy, magnitude[scale]);
    }

    std::vector<Point> refined(contour);
    for (std::size_t i = 0; i < contour.size(); ++i) {
        const Point& before = contour[(i + contour.size() - 3) % contour.size()];
        const Point& after = contour[(i + 3) % contour.size()];
        const double tx = after.x - before.x;
        const double ty = after.y - before.y;
        const double length = std::hypot(tx, ty);
        if (length < 1e-9) continue;
        const double nx = -ty / length;
        const double ny = tx / length;
        constexpr int kSamples = 13;
        std::array<double, kSamples> scores{};
        for (int sample = 0; sample < kSamples; ++sample) {
            const double displacement = -0.75 + sample * 0.125;
            for (const cv::Mat& level : magnitude)
                scores[sample] += bilinear(level, contour[i].x + nx * displacement,
                                           contour[i].y + ny * displacement);
        }
        const auto best = static_cast<int>(std::distance(scores.begin(),
                                  std::max_element(scores.begin(), scores.end())));
        double displacement = -0.75 + best * 0.125;
        if (best > 0 && best + 1 < kSamples) {
            const double a = scores[best - 1];
            const double b = scores[best];
            const double c = scores[best + 1];
            const double denominator = a - 2.0 * b + c;
            if (std::abs(denominator) > 1e-12)
                displacement += std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5) * 0.125;
        }
        refined[i] = {contour[i].x + nx * displacement, contour[i].y + ny * displacement};
    }
    return refined;
}

std::vector<Point> sourceSpan(const std::vector<Point>& source, std::size_t begin, std::size_t end) {
    std::vector<Point> points;
    if (source.empty()) return points;
    begin %= source.size(); end %= source.size();
    points.push_back(source[begin]);
    while (begin != end) {
        begin = (begin + 1) % source.size();
        points.push_back(source[begin]);
        if (points.size() > source.size() + 1) break;
    }
    return points;
}

void eigenTlsRefitLines(PlannedPath& path, const std::vector<Point>& source) {
    for (Primitive& primitive : path.primitives) {
        if (primitive.type != PrimitiveType::Line) continue;
        const std::vector<Point> samples = sourceSpan(source, primitive.sourceBegin, primitive.sourceEnd);
        if (samples.size() < 2) continue;
        Eigen::Vector2d center = Eigen::Vector2d::Zero();
        for (const Point& point : samples) center += Eigen::Vector2d(point.x, point.y);
        center /= static_cast<double>(samples.size());
        Eigen::Matrix2d covariance = Eigen::Matrix2d::Zero();
        for (const Point& point : samples) {
            const Eigen::Vector2d delta(point.x - center.x(), point.y - center.y());
            covariance.noalias() += delta * delta.transpose();
        }
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> solver(covariance);
        if (solver.info() != Eigen::Success) continue;
        const Eigen::Vector2d direction = solver.eigenvectors().col(1).normalized();
        auto project = [&](const Point& point) {
            const Eigen::Vector2d delta(point.x - center.x(), point.y - center.y());
            const Eigen::Vector2d projected = center + direction * direction.dot(delta);
            return Point{projected.x(), projected.y()};
        };
        primitive.start = project(samples.front());
        primitive.end = project(samples.back());
    }
}

template <typename T>
T primitiveConstraint(const Primitive& primitive, const T* point) {
    using std::sqrt;
    if (primitive.type == PrimitiveType::Line) {
        const double dx = primitive.end.x - primitive.start.x;
        const double dy = primitive.end.y - primitive.start.y;
        const double length = std::max(1e-12, std::hypot(dx, dy));
        return ((point[0] - T(primitive.start.x)) * T(dy) -
                (point[1] - T(primitive.start.y)) * T(dx)) / T(length);
    }
    if (primitive.type == PrimitiveType::Circle || primitive.type == PrimitiveType::CircularArc) {
        const T dx = point[0] - T(primitive.center.x);
        const T dy = point[1] - T(primitive.center.y);
        return sqrt(dx * dx + dy * dy + T(1e-18)) - T(primitive.radius);
    }
    if (primitive.type == PrimitiveType::Ellipse || primitive.type == PrimitiveType::EllipticArc) {
        const T dx = point[0] - T(primitive.center.x);
        const T dy = point[1] - T(primitive.center.y);
        const T cosine = T(std::cos(primitive.rotation));
        const T sine = T(std::sin(primitive.rotation));
        const T localX = cosine * dx + sine * dy;
        const T localY = -sine * dx + cosine * dy;
        const T normalized = sqrt(localX * localX / T(primitive.radiusX * primitive.radiusX) +
                                  localY * localY / T(primitive.radiusY * primitive.radiusY) + T(1e-18));
        return (normalized - T(1.0)) * T(std::min(primitive.radiusX, primitive.radiusY));
    }
    return T(0.0);
}

struct JunctionResidual {
    Primitive before;
    Primitive after;
    Point target;

    template <typename T>
    bool operator()(const T* const point, T* residual) const {
        residual[0] = T(0.35) * (point[0] - T(target.x));
        residual[1] = T(0.35) * (point[1] - T(target.y));
        residual[2] = primitiveConstraint(before, point);
        residual[3] = primitiveConstraint(after, point);
        return true;
    }
};

double ellipseParameter(const Primitive& primitive, const Point& point) {
    const double dx = point.x - primitive.center.x;
    const double dy = point.y - primitive.center.y;
    const double cosine = std::cos(primitive.rotation);
    const double sine = std::sin(primitive.rotation);
    const double x = (cosine * dx + sine * dy) / std::max(1e-12, primitive.radiusX);
    const double y = (-sine * dx + cosine * dy) / std::max(1e-12, primitive.radiusY);
    return std::atan2(y, x);
}

double unwrapSweep(double start, double end, double oldSweep) {
    double sweep = end - start;
    if (oldSweep >= 0.0) while (sweep < 0.0) sweep += 2.0 * kPi;
    else while (sweep > 0.0) sweep -= 2.0 * kPi;
    if (std::abs(oldSweep) > kPi && std::abs(sweep) < kPi) sweep += oldSweep > 0.0 ? 2.0 * kPi : -2.0 * kPi;
    return sweep;
}

void setPrimitiveStart(Primitive& primitive, const Point& point) {
    const Point old = primitive.start;
    primitive.start = point;
    if (primitive.type == PrimitiveType::CubicBezier || primitive.type == PrimitiveType::CubicBSpline) {
        primitive.control1.x += point.x - old.x;
        primitive.control1.y += point.y - old.y;
    }
    if (primitive.type == PrimitiveType::CircularArc || primitive.type == PrimitiveType::EllipticArc) {
        const double oldEnd = primitive.startAngle + primitive.sweepAngle;
        const double nextStart = ellipseParameter(primitive, point);
        primitive.sweepAngle = unwrapSweep(nextStart, oldEnd, primitive.sweepAngle);
        primitive.startAngle = nextStart;
    }
}

void setPrimitiveEnd(Primitive& primitive, const Point& point) {
    const Point old = primitive.end;
    primitive.end = point;
    if (primitive.type == PrimitiveType::CubicBezier || primitive.type == PrimitiveType::CubicBSpline) {
        primitive.control2.x += point.x - old.x;
        primitive.control2.y += point.y - old.y;
    }
    if (primitive.type == PrimitiveType::CircularArc || primitive.type == PrimitiveType::EllipticArc) {
        const double nextEnd = ellipseParameter(primitive, point);
        primitive.sweepAngle = unwrapSweep(primitive.startAngle, nextEnd, primitive.sweepAngle);
    }
}

void ceresOptimizeJunctions(PlannedPath& path) {
    const std::size_t count = path.primitives.size();
    if (count < 2) return;
    std::vector<std::array<double, 2>> junctions(count);
    ceres::Problem problem;
    for (std::size_t i = 0; i < count; ++i) {
        const Primitive& before = path.primitives[i];
        const Primitive& after = path.primitives[(i + 1) % count];
        junctions[i] = {(before.end.x + after.start.x) * 0.5,
                        (before.end.y + after.start.y) * 0.5};
        auto* cost = new ceres::AutoDiffCostFunction<JunctionResidual, 4, 2>(
            new JunctionResidual{before, after, {junctions[i][0], junctions[i][1]}});
        problem.AddResidualBlock(cost, new ceres::HuberLoss(0.35), junctions[i].data());
    }
    ceres::Solver::Options options;
    options.linear_solver_type = ceres::DENSE_QR;
    options.max_num_iterations = 40;
    options.function_tolerance = 1e-10;
    options.gradient_tolerance = 1e-12;
    options.parameter_tolerance = 1e-10;
    options.num_threads = 1;
    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary);
    if (!summary.IsSolutionUsable()) return;
    for (std::size_t i = 0; i < count; ++i) {
        const Point target{(path.primitives[i].end.x +
                            path.primitives[(i + 1) % count].start.x) * 0.5,
                           (path.primitives[i].end.y +
                            path.primitives[(i + 1) % count].start.y) * 0.5};
        Point junction{junctions[i][0], junctions[i][1]};
        // A mathematical line/line intersection can lie far outside the
        // raster-supported corner when two almost-parallel fits meet.  The
        // previous implementation accepted that remote intersection and was
        // able to move a valid edge by 10+ pixels.  Ceres is only allowed to
        // make a sub-pixel continuity correction; larger corrections must be
        // handled by splitting/refitting the primitives instead.
        constexpr double kMaximumJunctionMovePixels = 0.75;
        const double moveX = junction.x - target.x;
        const double moveY = junction.y - target.y;
        const double move = std::hypot(moveX, moveY);
        if (move > kMaximumJunctionMovePixels) {
            const double scale = kMaximumJunctionMovePixels / move;
            junction = {target.x + moveX * scale, target.y + moveY * scale};
        }
        setPrimitiveEnd(path.primitives[i], junction);
        setPrimitiveStart(path.primitives[(i + 1) % count], junction);
    }
}

double signedArea(const std::vector<Point>& points) {
    double area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5;
}

bool lineIntersection(const Point& a, const Point& b, const Point& c, const Point& d, Point& output) {
    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double cdx = d.x - c.x;
    const double cdy = d.y - c.y;
    const double denominator = abx * cdy - aby * cdx;
    if (std::abs(denominator) < 1e-10) return false;
    const double t = ((c.x - a.x) * cdy - (c.y - a.y) * cdx) / denominator;
    output = {a.x + t * abx, a.y + t * aby};
    return true;
}

bool analyticOffset(const PlannedPath& input, double delta, PlannedPath& output) {
    if (std::abs(delta) < 1e-12) { output = input; return true; }
    if (input.primitives.size() == 1 && input.primitives.front().type == PrimitiveType::Circle) {
        output = input;
        const std::vector<Point> sampled = samplePath(input, 1.0);
        const double direction = signedArea(sampled) >= 0.0 ? 1.0 : -1.0;
        Primitive& circle = output.primitives.front();
        circle.radius += direction * delta;
        if (circle.radius <= 1e-6) return false;
        circle.start = {circle.center.x + circle.radius * std::cos(circle.startAngle),
                        circle.center.y + circle.radius * std::sin(circle.startAngle)};
        circle.end = circle.start;
        return true;
    }
    if (!std::all_of(input.primitives.begin(), input.primitives.end(),
                     [](const Primitive& primitive) { return primitive.type == PrimitiveType::Line; }))
        return false;
    const std::vector<Point> sampled = samplePath(input, 1.0);
    const double orientation = signedArea(sampled) >= 0.0 ? 1.0 : -1.0;
    std::vector<std::pair<Point, Point>> shifted;
    shifted.reserve(input.primitives.size());
    for (const Primitive& line : input.primitives) {
        const double dx = line.end.x - line.start.x;
        const double dy = line.end.y - line.start.y;
        const double length = std::hypot(dx, dy);
        if (length < 1e-9) return false;
        const double nx = orientation * (-dy / length) * delta;
        const double ny = orientation * (dx / length) * delta;
        shifted.push_back({{line.start.x + nx, line.start.y + ny},
                           {line.end.x + nx, line.end.y + ny}});
    }
    std::vector<Point> corners(shifted.size());
    for (std::size_t i = 0; i < shifted.size(); ++i) {
        const std::size_t before = (i + shifted.size() - 1) % shifted.size();
        if (!lineIntersection(shifted[before].first, shifted[before].second,
                              shifted[i].first, shifted[i].second, corners[i]))
            corners[i] = {(shifted[before].second.x + shifted[i].first.x) * 0.5,
                          (shifted[before].second.y + shifted[i].first.y) * 0.5};
    }
    output = {};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        Primitive primitive;
        primitive.type = PrimitiveType::Line;
        primitive.start = corners[i];
        primitive.end = corners[(i + 1) % corners.size()];
        output.primitives.push_back(primitive);
    }
    output.quality.closed = true;
    output.quality.lines = corners.size();
    output.quality.outputPrimitives = corners.size();
    output.quality.outputNodes = corners.size();
    return true;
}

bool clipperOffset(const PlannedPath& input, double delta, const PlannerOptions& planner,
                   std::vector<PlannedPath>& output) {
    Clipper2Lib::PathD path;
    for (const Point& point : samplePath(input, 0.25)) path.push_back({point.x, point.y});
    if (path.size() < 4) return false;
    const Clipper2Lib::PathsD offset = Clipper2Lib::InflatePaths(
        {path}, delta, Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Polygon, 2.0, 5, 0.08);
    if (offset.empty()) return false;
    for (const Clipper2Lib::PathD& candidate : offset) {
        std::vector<Point> points;
        points.reserve(candidate.size());
        for (const auto& point : candidate) points.push_back({point.x, point.y});
        if (points.size() > 1 && pointDistance(points.front(), points.back()) < 1e-8) points.pop_back();
        PlannedPath planned = planClosedContour(points, planner);
        if (planned) output.push_back(std::move(planned));
    }
    return !output.empty();
}

struct TopologyResult {
    bool clean{};
    std::size_t open{};
    std::size_t selfIntersections{};
    std::size_t duplicates{};
    std::string message;
};

TopologyResult validateTopology(const PlannedPath& path) {
    TopologyResult result;
    for (std::size_t i = 0; i < path.primitives.size(); ++i) {
        const Primitive& current = path.primitives[i];
        const Primitive& next = path.primitives[(i + 1) % path.primitives.size()];
        if (pointDistance(current.end, next.start) > 1e-6) ++result.open;
    }
    const std::vector<Point> samples = samplePath(path, 0.30);
    if (samples.size() < 4) { result.message = "Too few samples."; return result; }
    using BoostPoint = bg::model::d2::point_xy<double>;
    bg::model::ring<BoostPoint, true, true> ring;
    for (const Point& point : samples) ring.emplace_back(point.x, point.y);
    bg::correct(ring);
    if (!bg::is_simple(ring)) result.selfIntersections = 1;
    for (std::size_t i = 1; i < samples.size(); ++i)
        if (pointDistance(samples[i - 1], samples[i]) < 1e-8) ++result.duplicates;
    result.clean = result.open == 0 && result.selfIntersections == 0 && result.duplicates == 0;
    if (!result.clean) {
        std::ostringstream stream;
        stream << "open=" << result.open << ", self_intersections=" << result.selfIntersections
               << ", duplicate_segments=" << result.duplicates;
        result.message = stream.str();
    } else result.message = "PASS";
    return result;
}

double percentile95(std::vector<float> values) {
    if (values.empty()) return 0.0;
    const std::size_t index = static_cast<std::size_t>(std::floor(0.95 * (values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

struct DistanceQuality { double p95{}; double maximum{}; };

struct CornerZone {
    Point junction{};
    double radius{};
};

struct SupportedPathSamples {
    std::vector<Point> points;
    std::vector<CornerZone> cornerZones;
    double maximumCornerExtension{};
    bool cornerGeometryValid{true};
};

double lineAngleDegrees(const Primitive& before, const Primitive& after) {
    const double ax = before.end.x - before.start.x;
    const double ay = before.end.y - before.start.y;
    const double bx = after.end.x - after.start.x;
    const double by = after.end.y - after.start.y;
    const double lengths = std::hypot(ax, ay) * std::hypot(bx, by);
    if (lengths < 1e-12) return 0.0;
    const double cosine = std::clamp(std::abs((ax * bx + ay * by) / lengths), 0.0, 1.0);
    return std::acos(cosine) * 180.0 / kPi;
}

// Return only the portions of analytic primitives which have direct raster
// samples behind them.  A LINE/LINE junction is allowed to reconstruct the
// theoretical sharp corner hidden by antialiasing or a pixel bevel, but the
// extrapolated length is measured and gated separately below.
SupportedPathSamples sampleSupportedPath(const PlannedPath& path,
                                         const std::vector<Point>& source,
                                         double maximumStep,
                                         double maximumCornerExtension) {
    SupportedPathSamples result;
    const std::size_t count = path.primitives.size();
    if (count == 0) return result;
    for (std::size_t index = 0; index < count; ++index) {
        const Primitive& primitive = path.primitives[index];
        if (primitive.type != PrimitiveType::Line) {
            const double length = primitive.type == PrimitiveType::Circle ||
                                  primitive.type == PrimitiveType::CircularArc
                ? std::abs(primitive.sweepAngle) * primitive.radius
                : primitive.type == PrimitiveType::Ellipse ||
                  primitive.type == PrimitiveType::EllipticArc
                    ? std::abs(primitive.sweepAngle) *
                          std::max(primitive.radiusX, primitive.radiusY)
                    : pointDistance(primitive.start, primitive.control1) +
                          pointDistance(primitive.control1, primitive.control2) +
                          pointDistance(primitive.control2, primitive.end);
            const int pieces = std::clamp(
                static_cast<int>(std::ceil(length / maximumStep)), 1, 32768);
            for (int piece = 0; piece <= pieces; ++piece)
                result.points.push_back(pointOnPrimitive(
                    primitive, static_cast<double>(piece) / pieces));
            continue;
        }

        const double dx = primitive.end.x - primitive.start.x;
        const double dy = primitive.end.y - primitive.start.y;
        const double length = std::hypot(dx, dy);
        if (length < 1e-9) {
            result.cornerGeometryValid = false;
            continue;
        }
        const double ux = dx / length;
        const double uy = dy / length;
        // Analytic/Clipper offset paths are verified against their generated
        // offset reference and do not carry indices into the pre-offset loop.
        // Their raw bidirectional check is exact, so keep the complete line.
        if (source.empty() || primitive.sourceBegin == primitive.sourceEnd) {
            const int pieces = std::clamp(
                static_cast<int>(std::ceil(length / maximumStep)), 1, 32768);
            for (int piece = 0; piece <= pieces; ++piece)
                result.points.push_back(pointOnPrimitive(
                    primitive, static_cast<double>(piece) / pieces));
            continue;
        }
        const std::vector<Point> support =
            sourceSpan(source, primitive.sourceBegin, primitive.sourceEnd);
        double supportMinimum = length;
        double supportMaximum = 0.0;
        for (const Point& point : support) {
            const double projection =
                (point.x - primitive.start.x) * ux + (point.y - primitive.start.y) * uy;
            supportMinimum = std::min(supportMinimum, projection);
            supportMaximum = std::max(supportMaximum, projection);
        }

        const Primitive& before = path.primitives[(index + count - 1) % count];
        const Primitive& after = path.primitives[(index + 1) % count];
        const bool reconstructStart = before.type == PrimitiveType::Line;
        const bool reconstructEnd = after.type == PrimitiveType::Line;
        const double startExtension = reconstructStart
            ? std::max(0.0, std::min(length, supportMinimum)) : 0.0;
        const double endExtension = reconstructEnd
            ? std::max(0.0, length - std::max(0.0, supportMaximum)) : 0.0;
        result.maximumCornerExtension = std::max(
            {result.maximumCornerExtension, startExtension, endExtension});
        if (startExtension > 1e-6)
            result.cornerZones.push_back(
                {primitive.start, std::sqrt(2.0) * startExtension + maximumStep});
        if (endExtension > 1e-6)
            result.cornerZones.push_back(
                {primitive.end, std::sqrt(2.0) * endExtension + maximumStep});

        const double meaningfulCornerExtension = std::max(0.5, maximumStep * 2.0);
        if ((startExtension > 1e-6 &&
             (startExtension > maximumCornerExtension ||
              (startExtension > meaningfulCornerExtension &&
               lineAngleDegrees(before, primitive) < 3.0))) ||
            (endExtension > 1e-6 &&
             (endExtension > maximumCornerExtension ||
              (endExtension > meaningfulCornerExtension &&
               lineAngleDegrees(primitive, after) < 3.0))))
            result.cornerGeometryValid = false;

        const double verifiedBegin = reconstructStart
            ? std::clamp(supportMinimum, 0.0, length) : 0.0;
        const double verifiedEnd = reconstructEnd
            ? std::clamp(supportMaximum, 0.0, length) : length;
        if (verifiedEnd + 1e-9 < verifiedBegin) {
            result.cornerGeometryValid = false;
            continue;
        }
        const int pieces = std::clamp(
            static_cast<int>(std::ceil(length / maximumStep)), 1, 32768);
        for (int piece = 0; piece <= pieces; ++piece) {
            const double t = static_cast<double>(piece) / pieces;
            const double position = t * length;
            if (position + 1e-9 < verifiedBegin || position - 1e-9 > verifiedEnd)
                continue;
            result.points.push_back(pointOnPrimitive(primitive, t));
        }
    }
    return result;
}

DistanceQuality directedDistance(const std::vector<Point>& from, const std::vector<Point>& to,
                                 int width, int height,
                                 const std::vector<CornerZone>* ignoredCornerZones = nullptr) {
    (void)width;
    (void)height;
    std::vector<Point> contour = to;
    if (contour.size() > 1 && pointDistance(contour.front(), contour.back()) <= 1e-9)
        contour.pop_back();
    if (contour.size() < 3)
        return {std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()};

    auto pointToSegment = [](const Point& point, const Point& start, const Point& end) {
        const double dx = end.x - start.x;
        const double dy = end.y - start.y;
        const double lengthSquared = dx * dx + dy * dy;
        if (lengthSquared <= 1e-24) return pointDistance(point, start);
        const double parameter = std::clamp(
            ((point.x - start.x) * dx + (point.y - start.y) * dy) / lengthSquared,
            0.0, 1.0);
        return std::hypot(point.x - (start.x + parameter * dx),
                          point.y - (start.y + parameter * dy));
    };

    std::vector<float> values;
    values.reserve(from.size());
    for (const Point& point : from) {
        if (ignoredCornerZones && std::any_of(
                ignoredCornerZones->begin(), ignoredCornerZones->end(),
                [&](const CornerZone& zone) {
                    return pointDistance(point, zone.junction) <= zone.radius;
                }))
            continue;
        double nearest = std::numeric_limits<double>::infinity();
        for (std::size_t edge = 0; edge < contour.size(); ++edge)
            nearest = std::min(nearest, pointToSegment(
                point, contour[edge], contour[(edge + 1) % contour.size()]));
        values.push_back(static_cast<float>(nearest));
    }
    const double maximum = values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
    return {percentile95(std::move(values)), maximum};
}

void verifyPaths(const std::vector<std::vector<Point>>& source,
                 const std::vector<PlannedPath>& paths,
                 int width, int height, const ProductionOptions& options,
                 ProductionQuality& quality) {
    double forwardP95 = 0.0, reverseP95 = 0.0, hausdorff = 0.0;
    double rawReverseP95 = 0.0, rawHausdorff = 0.0;
    double cornerExtension = 0.0;
    bool cornerGeometryValid = true;
    for (std::size_t i = 0; i < paths.size() && i < source.size(); ++i) {
        const std::vector<Point> output = samplePath(paths[i], 0.25);
        std::vector<Point> sourceClosed = source[i];
        if (!sourceClosed.empty()) sourceClosed.push_back(sourceClosed.front());
        const DistanceQuality rawForward = directedDistance(sourceClosed, output, width, height);
        const DistanceQuality rawReverse = directedDistance(output, sourceClosed, width, height);
        DistanceQuality forward = rawForward;
        DistanceQuality reverse = rawReverse;
        // Always calculate LINE/LINE support and corner reconstruction quality.
        // Previously this diagnostic only ran after the ordinary distance gate
        // failed, which meant an otherwise-close analytic rectangle reported a
        // zero corner extension and could bypass a deliberately strict corner
        // limit. Distance relaxation remains conditional; only the independent
        // corner metric is now evaluated for every path.
        SupportedPathSamples supported = sampleSupportedPath(
            paths[i], source[i], 0.25, options.verificationCornerExtensionPixels);
        if (rawForward.maximum > options.verificationMaximumPixels ||
            rawReverse.p95 > options.verificationP95Pixels ||
            rawReverse.maximum > options.verificationMaximumPixels) {
            forward = directedDistance(sourceClosed, output, width, height,
                                       &supported.cornerZones);
            reverse = directedDistance(
                supported.points.empty() ? output : supported.points,
                sourceClosed, width, height);
        }
        forwardP95 = std::max(forwardP95, forward.p95);
        reverseP95 = std::max(reverseP95, reverse.p95);
        hausdorff = std::max({hausdorff, forward.maximum, reverse.maximum});
        rawReverseP95 = std::max(rawReverseP95, rawReverse.p95);
        rawHausdorff = std::max({rawHausdorff, rawForward.maximum, rawReverse.maximum});
        cornerExtension = std::max(cornerExtension, supported.maximumCornerExtension);
        cornerGeometryValid = cornerGeometryValid && supported.cornerGeometryValid;
    }
    quality.forwardP95Pixels = forwardP95;
    quality.reverseP95Pixels = reverseP95;
    quality.hausdorffPixels = hausdorff;
    quality.rawReverseP95Pixels = rawReverseP95;
    quality.rawHausdorffPixels = rawHausdorff;
    quality.cornerReconstructionMaximumPixels = cornerExtension;
    quality.cornerReconstructionPassed = cornerGeometryValid;
}

} // namespace
#endif

ProductionResult buildManufacturingPaths(const RgbaImage& image,
                                         const ProductionOptions& options) {
    ProductionResult result;
    const ContourResult contours = traceContours(image, options.contour);
    if (!contours) {
        result.error = contours.error;
        return result;
    }
    result.contourQuality = contours.quality;
#if SCM_V9_PRODUCTION
    std::vector<std::vector<Point>> referenceLoops;
    for (const Loop& loop : contours.loops) {
        std::vector<Point> refined = options.enableOpenCvMultiscaleRefinement
            ? refineMultiscale(image, loop.points) : loop.points;
        PlannedPath path = planClosedContour(refined, options.planner);
        if (!path) {
            result.error = path.error;
            return result;
        }
        eigenTlsRefitLines(path, refined);
        if (options.enableCeresGlobalOptimization) ceresOptimizeJunctions(path);

        if (std::abs(options.offsetPixels) < 1e-12) {
            referenceLoops.push_back(refined);
            result.paths.push_back(std::move(path));
        } else {
            PlannedPath offset;
            if (analyticOffset(path, options.offsetPixels, offset)) {
                result.quality.usedAnalyticOffset = true;
                referenceLoops.push_back(samplePath(offset, 0.25));
                if (!referenceLoops.back().empty()) referenceLoops.back().pop_back();
                result.paths.push_back(std::move(offset));
            } else {
                std::vector<PlannedPath> offsetPaths;
                if (!clipperOffset(path, options.offsetPixels, options.planner, offsetPaths)) {
                    result.error = "Clipper2 offset failed.";
                    return result;
                }
                result.quality.usedClipperFallback = true;
                for (PlannedPath& offsetPath : offsetPaths) {
                    referenceLoops.push_back(samplePath(offsetPath, 0.25));
                    if (!referenceLoops.back().empty()) referenceLoops.back().pop_back();
                    result.paths.push_back(std::move(offsetPath));
                }
            }
        }
    }
    if (result.paths.empty()) { result.error = "No manufacturing path was generated."; return result; }

    result.quality.topologyClean = true;
    for (const PlannedPath& path : result.paths) {
        const TopologyResult topology = validateTopology(path);
        result.quality.openPaths += topology.open;
        result.quality.selfIntersections += topology.selfIntersections;
        result.quality.duplicateSegments += topology.duplicates;
        result.quality.outputPrimitives += path.primitives.size();
        if (!topology.clean) {
            result.quality.topologyClean = false;
            if (!result.quality.topologyMessage.empty()) result.quality.topologyMessage += "; ";
            result.quality.topologyMessage += topology.message;
        }
    }
    for (const auto& loop : referenceLoops) result.quality.sourceSamples += loop.size();
    verifyPaths(referenceLoops, result.paths, image.width, image.height, options, result.quality);
    result.quality.verificationPassed =
        std::max(result.quality.forwardP95Pixels, result.quality.reverseP95Pixels) <=
            options.verificationP95Pixels &&
        result.quality.hausdorffPixels <= options.verificationMaximumPixels &&
        result.quality.cornerReconstructionPassed;
    if (!result.quality.topologyClean) {
        result.error = "Manufacturing topology verification failed: " + result.quality.topologyMessage;
        return result;
    }
    if (options.rejectOnVerificationFailure && !result.quality.verificationPassed) {
        std::ostringstream stream;
        stream << "双向贴合复检未通过：95%偏差="
               << std::max(result.quality.forwardP95Pixels, result.quality.reverseP95Pixels)
               << "/" << options.verificationP95Pixels << " 像素；最大偏差="
               << result.quality.hausdorffPixels << "/" << options.verificationMaximumPixels
               << " 像素；尖角延伸="
               << result.quality.cornerReconstructionMaximumPixels << "/"
               << options.verificationCornerExtensionPixels << " 像素"
               << (result.quality.cornerReconstructionPassed ? "。" : "（尖角结构无效）。")
               << " 原始栅格复检：95%偏差=" << result.quality.rawReverseP95Pixels
               << "，最大偏差=" << result.quality.rawHausdorffPixels << " 像素。"
               << " 已停止输出不合格切割线。";
        result.error = stream.str();
    }
#else
    for (const Loop& loop : contours.loops) {
        PlannedPath path = planClosedContour(loop.points, options.planner);
        if (!path) { result.error = path.error; return result; }
        result.paths.push_back(std::move(path));
    }
    if (result.paths.empty()) result.error = "No manufacturing path was generated.";
#endif
    return result;
}

} // namespace scm::v9
