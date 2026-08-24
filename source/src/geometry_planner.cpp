#include "geometry_planner.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>

namespace scm::v9 {
namespace {

constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kEps = 1e-10;

double sqr(double x) { return x * x; }
double distance(const Point& a, const Point& b) { return std::hypot(a.x - b.x, a.y - b.y); }
Point add(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
Point sub(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
Point mul(Point a, double s) { return {a.x * s, a.y * s}; }
double dot(Point a, Point b) { return a.x * b.x + a.y * b.y; }
double cross(Point a, Point b) { return a.x * b.y - a.y * b.x; }

Point normalized(Point a) {
    const double length = std::hypot(a.x, a.y);
    return length > kEps ? Point{a.x / length, a.y / length} : Point{1.0, 0.0};
}

double percentile95(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const std::size_t index = static_cast<std::size_t>(
        std::floor(0.95 * static_cast<double>(values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

struct Errors {
    double p95{};
    double maximum{};
    double sumSquared{};
};

Errors summarize(const std::vector<double>& residuals) {
    Errors result;
    if (residuals.empty()) return result;
    result.p95 = percentile95(residuals);
    for (double value : residuals) {
        result.maximum = std::max(result.maximum, value);
        result.sumSquared += value * value;
    }
    return result;
}

bool solveLinear(double* matrix, double* rhs, double* solution, int n) {
    std::vector<double> a(static_cast<std::size_t>(n) * (n + 1));
    for (int row = 0; row < n; ++row) {
        for (int col = 0; col < n; ++col) a[static_cast<std::size_t>(row) * (n + 1) + col] = matrix[row * n + col];
        a[static_cast<std::size_t>(row) * (n + 1) + n] = rhs[row];
    }
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int row = col + 1; row < n; ++row)
            if (std::abs(a[static_cast<std::size_t>(row) * (n + 1) + col]) >
                std::abs(a[static_cast<std::size_t>(pivot) * (n + 1) + col])) pivot = row;
        if (std::abs(a[static_cast<std::size_t>(pivot) * (n + 1) + col]) < 1e-12) return false;
        if (pivot != col)
            for (int k = col; k <= n; ++k)
                std::swap(a[static_cast<std::size_t>(pivot) * (n + 1) + k],
                          a[static_cast<std::size_t>(col) * (n + 1) + k]);
        const double divisor = a[static_cast<std::size_t>(col) * (n + 1) + col];
        for (int k = col; k <= n; ++k) a[static_cast<std::size_t>(col) * (n + 1) + k] /= divisor;
        for (int row = 0; row < n; ++row) {
            if (row == col) continue;
            const double factor = a[static_cast<std::size_t>(row) * (n + 1) + col];
            for (int k = col; k <= n; ++k)
                a[static_cast<std::size_t>(row) * (n + 1) + k] -=
                    factor * a[static_cast<std::size_t>(col) * (n + 1) + k];
        }
    }
    for (int row = 0; row < n; ++row) solution[row] = a[static_cast<std::size_t>(row) * (n + 1) + n];
    return true;
}

struct LineModel {
    Point origin{};
    Point direction{1.0, 0.0};
    Errors errors{};
};

LineModel tlsLine(const std::vector<Point>& points, const std::vector<std::size_t>* indices = nullptr) {
    LineModel model;
    const std::size_t count = indices ? indices->size() : points.size();
    if (count == 0) return model;
    for (std::size_t k = 0; k < count; ++k) model.origin = add(model.origin, points[indices ? (*indices)[k] : k]);
    model.origin = mul(model.origin, 1.0 / static_cast<double>(count));
    double xx = 0.0, xy = 0.0, yy = 0.0;
    for (std::size_t k = 0; k < count; ++k) {
        const Point q = sub(points[indices ? (*indices)[k] : k], model.origin);
        xx += q.x * q.x; xy += q.x * q.y; yy += q.y * q.y;
    }
    const double angle = 0.5 * std::atan2(2.0 * xy, xx - yy);
    model.direction = {std::cos(angle), std::sin(angle)};
    if (dot(sub(points.back(), points.front()), model.direction) < 0.0) model.direction = mul(model.direction, -1.0);
    const Point normal{-model.direction.y, model.direction.x};
    std::vector<double> residuals;
    residuals.reserve(points.size());
    for (const Point& point : points) residuals.push_back(std::abs(dot(sub(point, model.origin), normal)));
    model.errors = summarize(residuals);
    return model;
}

LineModel ransacTlsLine(const std::vector<Point>& points, double inlierTolerance) {
    if (points.size() < 4) return tlsLine(points);
    std::vector<std::size_t> best;
    double bestSum = std::numeric_limits<double>::infinity();
    const std::size_t n = points.size();
    const std::size_t trials = std::min<std::size_t>(96, n * 3);
    for (std::size_t trial = 0; trial < trials; ++trial) {
        const std::size_t a = (trial * 37 + 3) % n;
        std::size_t b = (trial * 71 + n / 2 + 1) % n;
        if (a == b) b = (b + 1) % n;
        const Point direction = normalized(sub(points[b], points[a]));
        if (distance(points[a], points[b]) < 0.2 * distance(points.front(), points.back())) continue;
        const Point normal{-direction.y, direction.x};
        std::vector<std::size_t> inliers;
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double residual = std::abs(dot(sub(points[i], points[a]), normal));
            if (residual <= inlierTolerance) { inliers.push_back(i); sum += residual; }
        }
        if (inliers.size() > best.size() || (inliers.size() == best.size() && sum < bestSum)) {
            best = std::move(inliers); bestSum = sum;
        }
    }
    if (best.size() * 100 < points.size() * 72) return tlsLine(points);
    return tlsLine(points, &best);
}

Primitive linePrimitive(const std::vector<Point>& points, const LineModel& line,
                        std::size_t begin, std::size_t end) {
    Primitive primitive;
    primitive.type = PrimitiveType::Line;
    primitive.sourceBegin = begin; primitive.sourceEnd = end;
    const double t0 = dot(sub(points.front(), line.origin), line.direction);
    const double t1 = dot(sub(points.back(), line.origin), line.direction);
    primitive.start = add(line.origin, mul(line.direction, t0));
    primitive.end = add(line.origin, mul(line.direction, t1));
    primitive.p95ErrorPixels = line.errors.p95;
    primitive.maxErrorPixels = line.errors.maximum;
    return primitive;
}

struct CircleModel {
    Point center{};
    double radius{};
    Errors errors{};
    bool valid{};
};

CircleModel fitCircle(const std::vector<Point>& points) {
    CircleModel model;
    if (points.size() < 3) return model;
    double m[9]{}; double b[3]{};
    for (const Point& p : points) {
        const double row[3]{p.x, p.y, 1.0};
        const double target = -(p.x * p.x + p.y * p.y);
        for (int i = 0; i < 3; ++i) {
            b[i] += row[i] * target;
            for (int j = 0; j < 3; ++j) m[i * 3 + j] += row[i] * row[j];
        }
    }
    double x[3]{};
    if (!solveLinear(m, b, x, 3)) return model;
    model.center = {-0.5 * x[0], -0.5 * x[1]};
    const double radiusSquared = sqr(model.center.x) + sqr(model.center.y) - x[2];
    if (!(radiusSquared > 1e-8) || !std::isfinite(radiusSquared)) return model;
    model.radius = std::sqrt(radiusSquared);

    for (int iteration = 0; iteration < 12; ++iteration) {
        double h[9]{}; double g[3]{};
        for (const Point& p : points) {
            const double dx = model.center.x - p.x;
            const double dy = model.center.y - p.y;
            const double length = std::max(kEps, std::hypot(dx, dy));
            const double residual = length - model.radius;
            const double weight = std::abs(residual) <= 0.65 ? 1.0 : 0.65 / std::abs(residual);
            const double j[3]{dx / length, dy / length, -1.0};
            for (int r = 0; r < 3; ++r) {
                g[r] += weight * j[r] * residual;
                for (int c = 0; c < 3; ++c) h[r * 3 + c] += weight * j[r] * j[c];
            }
        }
        for (int i = 0; i < 3; ++i) g[i] = -g[i];
        double delta[3]{};
        if (!solveLinear(h, g, delta, 3)) break;
        model.center.x += delta[0]; model.center.y += delta[1]; model.radius += delta[2];
        if (std::hypot(delta[0], delta[1]) + std::abs(delta[2]) < 1e-7) break;
    }
    std::vector<double> residuals;
    residuals.reserve(points.size());
    for (const Point& p : points) residuals.push_back(std::abs(distance(p, model.center) - model.radius));
    model.errors = summarize(residuals);
    model.valid = model.radius > 0.25 && std::isfinite(model.radius);
    return model;
}

double unwrapSweep(const std::vector<Point>& points, Point center) {
    if (points.size() < 2) return 0.0;
    double previous = std::atan2(points.front().y - center.y, points.front().x - center.x);
    double sweep = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const double current = std::atan2(points[i].y - center.y, points[i].x - center.x);
        double delta = current - previous;
        while (delta <= -kPi) delta += 2.0 * kPi;
        while (delta > kPi) delta -= 2.0 * kPi;
        sweep += delta; previous = current;
    }
    return sweep;
}

Primitive circlePrimitive(const std::vector<Point>& points, const CircleModel& circle,
                          bool closed, std::size_t begin, std::size_t end) {
    Primitive primitive;
    primitive.type = closed ? PrimitiveType::Circle : PrimitiveType::CircularArc;
    primitive.sourceBegin = begin; primitive.sourceEnd = end;
    primitive.center = circle.center; primitive.radius = circle.radius;
    primitive.start = points.front(); primitive.end = closed ? points.front() : points.back();
    primitive.startAngle = std::atan2(points.front().y - circle.center.y, points.front().x - circle.center.x);
    primitive.sweepAngle = closed ? (unwrapSweep(points, circle.center) >= 0.0 ? 2.0 * kPi : -2.0 * kPi)
                                  : unwrapSweep(points, circle.center);
    primitive.p95ErrorPixels = circle.errors.p95;
    primitive.maxErrorPixels = circle.errors.maximum;
    return primitive;
}

struct EllipseModel {
    Point center{};
    double radiusX{};
    double radiusY{};
    double rotation{};
    Errors errors{};
    bool valid{};
};

double ellipseSignedResidual(const Point& point, const std::array<double, 5>& parameters) {
    const double cx = parameters[0], cy = parameters[1];
    const double rx = std::exp(parameters[2]), ry = std::exp(parameters[3]);
    const double angle = parameters[4];
    const double cosine = std::cos(angle), sine = std::sin(angle);
    const double dx = point.x - cx, dy = point.y - cy;
    const double x = cosine * dx + sine * dy;
    const double y = -sine * dx + cosine * dy;
    return (std::hypot(x / rx, y / ry) - 1.0) * std::min(rx, ry);
}

EllipseModel fitEllipse(const std::vector<Point>& points) {
    EllipseModel model;
    if (points.size() < 8) return model;
    Point center{};
    for (const Point& point : points) center = add(center, point);
    center = mul(center, 1.0 / static_cast<double>(points.size()));
    double xx = 0.0, xy = 0.0, yy = 0.0;
    for (const Point& point : points) {
        const Point q = sub(point, center);
        xx += q.x * q.x; xy += q.x * q.y; yy += q.y * q.y;
    }
    const double angle = 0.5 * std::atan2(2.0 * xy, xx - yy);
    const double cosine = std::cos(angle), sine = std::sin(angle);
    double meanX2 = 0.0, meanY2 = 0.0;
    for (const Point& point : points) {
        const Point q = sub(point, center);
        const double x = cosine * q.x + sine * q.y;
        const double y = -sine * q.x + cosine * q.y;
        meanX2 += x * x; meanY2 += y * y;
    }
    meanX2 /= points.size(); meanY2 /= points.size();
    std::array<double, 5> p{center.x, center.y,
                            std::log(std::max(0.25, std::sqrt(2.0 * meanX2))),
                            std::log(std::max(0.25, std::sqrt(2.0 * meanY2))), angle};
    for (int iteration = 0; iteration < 24; ++iteration) {
        double h[25]{}; double g[5]{};
        for (const Point& point : points) {
            const double residual = ellipseSignedResidual(point, p);
            const double weight = std::abs(residual) <= 0.75 ? 1.0 : 0.75 / std::abs(residual);
            double jacobian[5]{};
            for (int parameter = 0; parameter < 5; ++parameter) {
                std::array<double, 5> shifted = p;
                const double step = parameter < 2 ? 1e-5 : 2e-6;
                shifted[parameter] += step;
                jacobian[parameter] = (ellipseSignedResidual(point, shifted) - residual) / step;
            }
            for (int row = 0; row < 5; ++row) {
                g[row] += weight * jacobian[row] * residual;
                for (int col = 0; col < 5; ++col)
                    h[row * 5 + col] += weight * jacobian[row] * jacobian[col];
            }
        }
        for (int i = 0; i < 5; ++i) { h[i * 5 + i] += 1e-8; g[i] = -g[i]; }
        double delta[5]{};
        if (!solveLinear(h, g, delta, 5)) break;
        double norm = 0.0;
        for (int i = 0; i < 5; ++i) { p[i] += delta[i]; norm += delta[i] * delta[i]; }
        p[2] = std::clamp(p[2], std::log(0.25), std::log(1e7));
        p[3] = std::clamp(p[3], std::log(0.25), std::log(1e7));
        if (norm < 1e-13) break;
    }
    model.center = {p[0], p[1]}; model.radiusX = std::exp(p[2]);
    model.radiusY = std::exp(p[3]); model.rotation = p[4];
    if (model.radiusY > model.radiusX) {
        std::swap(model.radiusX, model.radiusY);
        model.rotation += kPi * 0.5;
    }
    std::array<double, 5> normalizedParameters{model.center.x, model.center.y,
        std::log(model.radiusX), std::log(model.radiusY), model.rotation};
    std::vector<double> residuals;
    residuals.reserve(points.size());
    for (const Point& point : points) residuals.push_back(std::abs(ellipseSignedResidual(point, normalizedParameters)));
    model.errors = summarize(residuals);
    const double aspect = model.radiusX / std::max(0.001, model.radiusY);
    model.valid = std::isfinite(aspect) && model.radiusY > 0.25 && aspect < 50.0;
    return model;
}

double ellipseParameterAngle(const Point& point, const EllipseModel& ellipse) {
    const double cosine = std::cos(ellipse.rotation), sine = std::sin(ellipse.rotation);
    const Point q = sub(point, ellipse.center);
    const double x = (cosine * q.x + sine * q.y) / ellipse.radiusX;
    const double y = (-sine * q.x + cosine * q.y) / ellipse.radiusY;
    return std::atan2(y, x);
}

double ellipseSweep(const std::vector<Point>& points, const EllipseModel& ellipse) {
    if (points.size() < 2) return 0.0;
    double previous = ellipseParameterAngle(points.front(), ellipse);
    double sweep = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const double current = ellipseParameterAngle(points[i], ellipse);
        double delta = current - previous;
        while (delta <= -kPi) delta += 2.0 * kPi;
        while (delta > kPi) delta -= 2.0 * kPi;
        sweep += delta; previous = current;
    }
    return sweep;
}

Primitive ellipsePrimitive(const std::vector<Point>& points, const EllipseModel& ellipse,
                           bool closed, std::size_t begin, std::size_t end) {
    Primitive primitive;
    primitive.type = closed ? PrimitiveType::Ellipse : PrimitiveType::EllipticArc;
    primitive.sourceBegin = begin; primitive.sourceEnd = end;
    primitive.center = ellipse.center; primitive.radiusX = ellipse.radiusX;
    primitive.radiusY = ellipse.radiusY; primitive.rotation = ellipse.rotation;
    primitive.start = points.front(); primitive.end = closed ? points.front() : points.back();
    primitive.startAngle = ellipseParameterAngle(points.front(), ellipse);
    primitive.sweepAngle = closed ? (ellipseSweep(points, ellipse) >= 0.0 ? 2.0 * kPi : -2.0 * kPi)
                                  : ellipseSweep(points, ellipse);
    primitive.p95ErrorPixels = ellipse.errors.p95;
    primitive.maxErrorPixels = ellipse.errors.maximum;
    return primitive;
}

Point cubicPoint(const std::array<Point, 4>& c, double t) {
    const double u = 1.0 - t;
    return add(add(mul(c[0], u * u * u), mul(c[1], 3.0 * u * u * t)),
               add(mul(c[2], 3.0 * u * t * t), mul(c[3], t * t * t)));
}

struct BezierModel {
    std::array<Point, 4> controls{};
    Errors errors{};
};

BezierModel fitBezier(const std::vector<Point>& points) {
    BezierModel model;
    model.controls[0] = points.front(); model.controls[3] = points.back();
    if (points.size() < 2) return model;
    Point startTangent = normalized(sub(points[std::min<std::size_t>(3, points.size() - 1)], points.front()));
    Point endTangent = normalized(sub(points[points.size() > 4 ? points.size() - 4 : 0], points.back()));
    std::vector<double> parameter(points.size());
    for (std::size_t i = 1; i < points.size(); ++i) parameter[i] = parameter[i - 1] + distance(points[i - 1], points[i]);
    const double total = std::max(kEps, parameter.back());
    for (double& value : parameter) value /= total;
    double c00 = 0.0, c01 = 0.0, c11 = 0.0, x0 = 0.0, x1 = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double t = parameter[i], u = 1.0 - t;
        const double b0 = u * u * u, b1 = 3.0 * u * u * t;
        const double b2 = 3.0 * u * t * t, b3 = t * t * t;
        const Point base = add(mul(model.controls[0], b0 + b1), mul(model.controls[3], b2 + b3));
        const Point rhs = sub(points[i], base);
        const Point a1 = mul(startTangent, b1), a2 = mul(endTangent, b2);
        c00 += dot(a1, a1); c01 += dot(a1, a2); c11 += dot(a2, a2);
        x0 += dot(a1, rhs); x1 += dot(a2, rhs);
    }
    const double determinant = c00 * c11 - c01 * c01;
    double alpha = total / 3.0, beta = total / 3.0;
    if (std::abs(determinant) > 1e-10) {
        alpha = (x0 * c11 - x1 * c01) / determinant;
        beta = (c00 * x1 - c01 * x0) / determinant;
    }
    const double minimumHandle = total * 0.02;
    if (!(alpha > minimumHandle) || !(beta > minimumHandle)) alpha = beta = total / 3.0;
    model.controls[1] = add(model.controls[0], mul(startTangent, alpha));
    model.controls[2] = add(model.controls[3], mul(endTangent, beta));
    std::vector<double> residuals;
    residuals.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i)
        residuals.push_back(distance(points[i], cubicPoint(model.controls, parameter[i])));
    model.errors = summarize(residuals);
    return model;
}

Primitive bezierPrimitive(const BezierModel& bezier, std::size_t begin, std::size_t end,
                          PrimitiveType representation = PrimitiveType::CubicBezier) {
    Primitive primitive;
    primitive.type = representation;
    primitive.start = bezier.controls[0]; primitive.control1 = bezier.controls[1];
    primitive.control2 = bezier.controls[2]; primitive.end = bezier.controls[3];
    primitive.sourceBegin = begin; primitive.sourceEnd = end;
    primitive.p95ErrorPixels = bezier.errors.p95;
    primitive.maxErrorPixels = bezier.errors.maximum;
    return primitive;
}

std::vector<std::size_t> detectCorners(const std::vector<Point>& contour, const PlannerOptions& options) {
    std::vector<std::pair<double, std::size_t>> candidates;
    const std::size_t n = contour.size();
    if (n < 8) return {};
    const std::array<std::size_t, 3> radii = n < 40
        ? std::array<std::size_t, 3>{1, 2, 3}
        : std::array<std::size_t, 3>{
            std::clamp<std::size_t>(n / 320, 2, 5),
            std::clamp<std::size_t>(n / 160, 4, 10),
            std::clamp<std::size_t>(n / 80, 7, 18)};
    for (std::size_t i = 0; i < n; ++i) {
        std::array<double, 3> angles{};
        for (std::size_t scale = 0; scale < radii.size(); ++scale) {
            const std::size_t radius = radii[scale];
            const Point incoming = normalized(sub(contour[i], contour[(i + n - radius) % n]));
            const Point outgoing = normalized(sub(contour[(i + radius) % n], contour[i]));
            angles[scale] = std::atan2(std::abs(cross(incoming, outgoing)),
                std::clamp(dot(incoming, outgoing), -1.0, 1.0)) * 180.0 / kPi;
        }
        std::sort(angles.begin(), angles.end());
        // A real construction corner persists across scale.  Pixel staircases
        // and local texture dents normally spike at only one radius.
        if (angles[1] >= options.cornerAngleDegrees && angles[0] >= options.cornerAngleDegrees * 0.42)
            candidates.push_back({angles[1] + 0.25 * angles[2], i});
    }
    std::sort(candidates.begin(), candidates.end(), std::greater<>());
    std::vector<std::size_t> corners;
    const std::size_t spacing = std::max<std::size_t>(2, radii[1]);
    for (const auto& candidate : candidates) {
        bool near = false;
        for (std::size_t existing : corners) {
            const std::size_t delta = existing > candidate.second ? existing - candidate.second : candidate.second - existing;
            if (std::min(delta, n - delta) <= spacing) { near = true; break; }
        }
        if (!near) corners.push_back(candidate.second);
    }
    std::sort(corners.begin(), corners.end());
    return corners;
}

std::vector<Point> cyclicSlice(const std::vector<Point>& contour, std::size_t begin, std::size_t end) {
    std::vector<Point> points;
    const std::size_t n = contour.size();
    for (std::size_t index = begin;; index = (index + 1) % n) {
        points.push_back(contour[index]);
        if (index == end) break;
        if (points.size() > n + 1) break;
    }
    return points;
}

struct Candidate {
    Primitive primitive;
    double mdlCost{std::numeric_limits<double>::infinity()};
    bool accepted{};
};

Candidate bestCandidate(const std::vector<Point>& points, const PlannerOptions& options,
                        std::size_t begin, std::size_t end, bool allowBezier) {
    Candidate best;
    const double sampleCount = static_cast<double>(points.size());
    auto consider = [&](Primitive primitive, const Errors& errors, double p95Limit, double maxLimit, double bias) {
        if (errors.p95 > p95Limit || errors.maximum > maxLimit) return;
        const double cost = errors.sumSquared / std::max(1.0, sampleCount) + options.mdlPrimitivePenalty + bias;
        if (cost < best.mdlCost) { best = {primitive, cost, true}; }
    };

    const LineModel line = ransacTlsLine(points, options.ransacInlierTolerance);
    if (distance(points.front(), points.back()) >= options.minimumLineLength) {
        consider(linePrimitive(points, line, begin, end), line.errors,
                 options.lineP95Tolerance, options.lineMaxTolerance, 0.0);
        // Manufacturing rule: if a complete run is already straight within
        // tolerance, keep it as one LINE.  A high-radius arc can always reduce
        // residual slightly, but replacing a straight edge with that arc makes
        // editing and laser motion worse.
        if (best.accepted) return best;
    }

    const CircleModel circle = fitCircle(points);
    if (circle.valid) {
        const double sweep = std::abs(unwrapSweep(points, circle.center));
        if (sweep >= options.minimumArcSweepDegrees * kPi / 180.0 && sweep <= 1.98 * kPi)
            consider(circlePrimitive(points, circle, false, begin, end), circle.errors,
                     options.arcP95Tolerance, options.arcMaxTolerance, 0.08);
    }

    const EllipseModel ellipse = fitEllipse(points);
    if (ellipse.valid) {
        const double sweep = std::abs(ellipseSweep(points, ellipse));
        if (sweep >= options.minimumArcSweepDegrees * kPi / 180.0 && sweep <= 1.98 * kPi)
            consider(ellipsePrimitive(points, ellipse, false, begin, end), ellipse.errors,
                     options.ellipseP95Tolerance, options.ellipseMaxTolerance, 0.15);
    }

    if (allowBezier) {
        const BezierModel bezier = fitBezier(points);
        // A long irregular run is retained as a clamped cubic B-spline span.
        // Its exact Bezier-equivalent handles are stored in control1/control2
        // so CorelDRAW can emit it without tessellating the curve.
        const PrimitiveType representation = points.size() >= 80
            ? PrimitiveType::CubicBSpline : PrimitiveType::CubicBezier;
        consider(bezierPrimitive(bezier, begin, end, representation), bezier.errors,
                 options.bezierP95Tolerance, options.bezierMaxTolerance, 0.22);
    }
    return best;
}

std::size_t splitIndex(const std::vector<Point>& points) {
    if (points.size() <= 4) return points.size() / 2;
    const LineModel line = tlsLine(points);
    const Point normal{-line.direction.y, line.direction.x};
    double maximum = -1.0; std::size_t split = points.size() / 2;
    for (std::size_t i = 2; i + 2 < points.size(); ++i) {
        const double residual = std::abs(dot(sub(points[i], line.origin), normal));
        if (residual > maximum) { maximum = residual; split = i; }
    }
    return std::clamp<std::size_t>(split, 2, points.size() - 3);
}

std::size_t bezierSplitIndex(const std::vector<Point>& points, const BezierModel& bezier) {
    if (points.size() <= 4) return points.size() / 2;
    std::vector<double> parameter(points.size());
    for (std::size_t i = 1; i < points.size(); ++i)
        parameter[i] = parameter[i - 1] + distance(points[i - 1], points[i]);
    const double total = std::max(kEps, parameter.back());
    double maximum = -1.0;
    std::size_t split = points.size() / 2;
    for (std::size_t i = 2; i + 2 < points.size(); ++i) {
        const double residual = distance(points[i], cubicPoint(bezier.controls, parameter[i] / total));
        if (residual > maximum) { maximum = residual; split = i; }
    }
    return std::clamp<std::size_t>(split, 2, points.size() - 3);
}

void appendExactPolyline(const std::vector<Point>& points, std::size_t sourceBegin,
                         std::vector<Primitive>& output) {
    for (std::size_t i = 1; i < points.size(); ++i) {
        Primitive primitive;
        primitive.type = PrimitiveType::Line;
        primitive.start = points[i - 1];
        primitive.end = points[i];
        primitive.sourceBegin = sourceBegin + i - 1;
        primitive.sourceEnd = sourceBegin + i;
        output.push_back(primitive);
    }
}

void planRun(const std::vector<Point>& points, const PlannerOptions& options,
             std::size_t sourceBegin, std::size_t sourceEnd, std::size_t depth,
             std::vector<Primitive>& output) {
    if (points.size() < 2) return;
    Candidate candidate = bestCandidate(points, options, sourceBegin, sourceEnd, true);
    if (candidate.accepted) { output.push_back(candidate.primitive); return; }
    if (points.size() <= options.minimumSamplesPerPrimitive || depth >= options.maximumRecursionDepth) {
        // Never emit a curve that failed its own manufacturing tolerance.  A
        // tiny exact polyline is the last-resort topology-safe representation;
        // ordinary straight and curved runs are handled by the analytic models
        // above and therefore do not reach this branch.
        appendExactPolyline(points, sourceBegin, output);
        return;
    }
    const BezierModel bezier = fitBezier(points);
    std::size_t split = bezierSplitIndex(points, bezier);
    if (split < 2 || split + 2 >= points.size()) split = splitIndex(points);
    std::vector<Point> left(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(split + 1));
    std::vector<Point> right(points.begin() + static_cast<std::ptrdiff_t>(split), points.end());
    const std::size_t sourceSplit = sourceBegin + split;
    planRun(left, options, sourceBegin, sourceSplit, depth + 1, output);
    planRun(right, options, sourceSplit, sourceEnd, depth + 1, output);
}

void planRunGlobal(const std::vector<Point>& points, const PlannerOptions& options,
                   std::size_t sourceBegin, std::vector<Primitive>& output) {
    if (points.size() < 2) return;
    const std::size_t n = points.size();
    constexpr std::array<std::size_t, 15> kCandidateSampleCounts{
        2, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 320};
    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> cost(n, infinity);
    std::vector<std::size_t> previous(n, n);
    std::vector<Primitive> chosen(n);
    cost[0] = 0.0;

    for (std::size_t end = 1; end < n; ++end) {
        std::vector<std::size_t> begins;
        begins.reserve(kCandidateSampleCounts.size() + 1);
        begins.push_back(0); // Always test the complete run ending here.
        for (const std::size_t count : kCandidateSampleCounts)
            if (count <= end + 1) begins.push_back(end + 1 - count);
        std::sort(begins.begin(), begins.end());
        begins.erase(std::unique(begins.begin(), begins.end()), begins.end());
        for (const std::size_t begin : begins) {
            if (begin >= end) continue;
            if (!std::isfinite(cost[begin])) continue;
            const std::size_t count = end - begin + 1;
            Candidate candidate;
            if (count >= 2) {
                const std::vector<Point> samples(points.begin() + static_cast<std::ptrdiff_t>(begin),
                                                 points.begin() + static_cast<std::ptrdiff_t>(end + 1));
                candidate = bestCandidate(samples, options, sourceBegin + begin,
                                          sourceBegin + end,
                                          count >= options.minimumSamplesPerPrimitive);
            }
            if (!candidate.accepted && count == 2) {
                Primitive exact;
                exact.type = PrimitiveType::Line;
                exact.start = points[begin]; exact.end = points[end];
                exact.sourceBegin = sourceBegin + begin;
                exact.sourceEnd = sourceBegin + end;
                candidate = {exact, options.mdlPrimitivePenalty * 7.0, true};
            }
            if (!candidate.accepted) continue;
            // The fixed per-primitive term is the MDL model-description cost.
            // It makes one long valid LINE cheaper than many locally perfect
            // pixel segments while residual terms still enforce accuracy.
            const double proposed = cost[begin] + candidate.mdlCost;
            if (proposed + 1e-12 < cost[end]) {
                cost[end] = proposed;
                previous[end] = begin;
                chosen[end] = candidate.primitive;
            }
        }
    }

    if (previous[n - 1] == n) {
        appendExactPolyline(points, sourceBegin, output);
        return;
    }
    std::vector<Primitive> reverse;
    for (std::size_t cursor = n - 1; cursor != 0;) {
        reverse.push_back(chosen[cursor]);
        cursor = previous[cursor];
    }
    output.insert(output.end(), reverse.rbegin(), reverse.rend());
}

bool sameLine(const Primitive& a, const Primitive& b, const PlannerOptions& options) {
    if (a.type != PrimitiveType::Line || b.type != PrimitiveType::Line) return false;
    const Point da = normalized(sub(a.end, a.start));
    const Point db = normalized(sub(b.end, b.start));
    const double angle = std::acos(std::clamp(std::abs(dot(da, db)), 0.0, 1.0)) * 180.0 / kPi;
    const double offset = std::abs(cross(da, sub(b.end, a.start)));
    return angle <= 1.00 && offset <= std::max(0.50, options.lineP95Tolerance);
}

void mergeCollinear(std::vector<Primitive>& primitives, const PlannerOptions& options) {
    if (primitives.size() < 2) return;
    std::vector<Primitive> merged;
    merged.reserve(primitives.size());
    for (const Primitive& primitive : primitives) {
        if (!merged.empty() && sameLine(merged.back(), primitive, options)) {
            merged.back().end = primitive.end;
            merged.back().sourceEnd = primitive.sourceEnd;
            merged.back().p95ErrorPixels = std::max(merged.back().p95ErrorPixels, primitive.p95ErrorPixels);
            merged.back().maxErrorPixels = std::max(merged.back().maxErrorPixels, primitive.maxErrorPixels);
        } else merged.push_back(primitive);
    }
    primitives.swap(merged);
}

} // namespace

const char* primitiveTypeName(PrimitiveType type) noexcept {
    switch (type) {
        case PrimitiveType::Line: return "LINE";
        case PrimitiveType::Circle: return "CIRCLE";
        case PrimitiveType::CircularArc: return "ARC";
        case PrimitiveType::Ellipse: return "ELLIPSE";
        case PrimitiveType::EllipticArc: return "ELLIPTIC_ARC";
        case PrimitiveType::CubicBezier: return "CUBIC_BEZIER";
        case PrimitiveType::CubicBSpline: return "CUBIC_BSPLINE";
    }
    return "UNKNOWN";
}

PlannedPath planClosedContour(const std::vector<Point>& contour, const PlannerOptions& options) {
    PlannedPath result;
    result.quality.sourcePoints = contour.size();
    result.quality.closed = true;
    if (contour.size() < 6) { result.error = "A closed manufacturing contour requires at least six samples."; return result; }

    const CircleModel wholeCircle = fitCircle(contour);
    if (wholeCircle.valid && wholeCircle.errors.p95 <= options.arcP95Tolerance &&
        wholeCircle.errors.maximum <= options.arcMaxTolerance) {
        result.primitives.push_back(circlePrimitive(contour, wholeCircle, true, 0, contour.size() - 1));
    } else {
        const EllipseModel wholeEllipse = fitEllipse(contour);
        if (wholeEllipse.valid && wholeEllipse.errors.p95 <= options.ellipseP95Tolerance &&
            wholeEllipse.errors.maximum <= options.ellipseMaxTolerance) {
            result.primitives.push_back(ellipsePrimitive(contour, wholeEllipse, true, 0, contour.size() - 1));
        } else {
        const std::vector<std::size_t> corners = detectCorners(contour, options);
        result.quality.corners = corners.size();
        if (corners.size() >= 2) {
            for (std::size_t i = 0; i < corners.size(); ++i) {
                const std::size_t begin = corners[i];
                const std::size_t end = corners[(i + 1) % corners.size()];
                const std::vector<Point> run = cyclicSlice(contour, begin, end);
                planRunGlobal(run, options, begin, result.primitives);
            }
        } else {
            std::size_t opposite = 1;
            double maximum = 0.0;
            for (std::size_t i = 1; i < contour.size(); ++i) {
                const double d = distance(contour.front(), contour[i]);
                if (d > maximum) { maximum = d; opposite = i; }
            }
            planRunGlobal(cyclicSlice(contour, 0, opposite), options, 0, result.primitives);
            planRunGlobal(cyclicSlice(contour, opposite, 0), options, opposite, result.primitives);
        }
        mergeCollinear(result.primitives, options);
        }
    }

    std::vector<double> p95Values;
    for (const Primitive& primitive : result.primitives) {
        result.quality.maxErrorPixels = std::max(result.quality.maxErrorPixels, primitive.maxErrorPixels);
        p95Values.push_back(primitive.p95ErrorPixels);
        switch (primitive.type) {
            case PrimitiveType::Line: ++result.quality.lines; result.quality.outputNodes += 1; break;
            case PrimitiveType::Circle: ++result.quality.circles; result.quality.outputNodes += 4; break;
            case PrimitiveType::CircularArc: ++result.quality.circularArcs; result.quality.outputNodes += 1; break;
            case PrimitiveType::Ellipse: ++result.quality.ellipses; result.quality.outputNodes += 4; break;
            case PrimitiveType::EllipticArc: ++result.quality.ellipticArcs; result.quality.outputNodes += 1; break;
            case PrimitiveType::CubicBezier: ++result.quality.cubicBeziers; result.quality.outputNodes += 1; break;
            case PrimitiveType::CubicBSpline: ++result.quality.cubicBSplines; result.quality.outputNodes += 1; break;
        }
    }
    result.quality.outputPrimitives = result.primitives.size();
    result.quality.p95ErrorPixels = percentile95(std::move(p95Values));
    return result;
}

} // namespace scm::v9
