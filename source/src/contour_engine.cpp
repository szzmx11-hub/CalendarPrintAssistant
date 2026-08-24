#include "contour_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace scm {
namespace {

constexpr double kEps = 1e-9;

struct Segment { Point a; Point b; };

struct Key {
    std::int64_t x{};
    std::int64_t y{};
    bool operator==(const Key& rhs) const noexcept { return x == rhs.x && y == rhs.y; }
};

struct KeyHash {
    std::size_t operator()(const Key& k) const noexcept {
        const auto a = static_cast<std::uint64_t>(k.x);
        const auto b = static_cast<std::uint64_t>(k.y);
        return static_cast<std::size_t>(a * 0x9E3779B185EBCA87ULL ^ (b + 0xC2B2AE3D27D4EB4FULL));
    }
};

Key keyOf(const Point& p) {
    constexpr double scale = 1000000.0;
    return {static_cast<std::int64_t>(std::llround(p.x * scale)),
            static_cast<std::int64_t>(std::llround(p.y * scale))};
}

double sqr(double x) { return x * x; }
double dist2(const Point& a, const Point& b) { return sqr(a.x - b.x) + sqr(a.y - b.y); }

double signedArea(const std::vector<Point>& p) {
    if (p.size() < 3) return 0.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto& a = p[i];
        const auto& b = p[(i + 1) % p.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return 0.5 * sum;
}

double pointSegmentDistance(const Point& p, const Point& a, const Point& b) {
    const double vx = b.x - a.x;
    const double vy = b.y - a.y;
    const double len2 = vx * vx + vy * vy;
    if (len2 <= kEps) return std::sqrt(dist2(p, a));
    const double t = std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / len2, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * vx), p.y - (a.y + t * vy));
}

std::vector<double> gaussianKernel(double sigma) {
    if (sigma <= 0.0) return {1.0};
    const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
    std::vector<double> k(static_cast<std::size_t>(radius * 2 + 1));
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double value = std::exp(-(i * i) / (2.0 * sigma * sigma));
        k[static_cast<std::size_t>(i + radius)] = value;
        sum += value;
    }
    for (double& value : k) value /= sum;
    return k;
}

void gaussianBlur(std::vector<double>& f, int w, int h, double sigma) {
    const auto kernel = gaussianKernel(sigma);
    if (kernel.size() == 1) return;
    const int radius = static_cast<int>(kernel.size() / 2);
    std::vector<double> temp(f.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double sum = 0.0;
            for (int i = -radius; i <= radius; ++i) {
                const int sx = std::clamp(x + i, 0, w - 1);
                sum += f[static_cast<std::size_t>(y) * w + sx] * kernel[static_cast<std::size_t>(i + radius)];
            }
            temp[static_cast<std::size_t>(y) * w + x] = sum;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double sum = 0.0;
            for (int i = -radius; i <= radius; ++i) {
                const int sy = std::clamp(y + i, 0, h - 1);
                sum += temp[static_cast<std::size_t>(sy) * w + x] * kernel[static_cast<std::size_t>(i + radius)];
            }
            f[static_cast<std::size_t>(y) * w + x] = sum;
        }
    }
}

double srgbToLinear(double value) {
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    double result = *middle;
    if ((values.size() & 1U) == 0U) {
        const auto lower = std::max_element(values.begin(), middle);
        result = 0.5 * (result + *lower);
    }
    return result;
}

// Build a manufacturing silhouette, not a collection of colour islands.  The
// border estimates the photographed/rendered background.  Exterior flood fill
// then seals white highlights and snow that belong to the product, which is the
// behaviour required for an outer laser-cut contour.
std::vector<double> exteriorProductMask(const RgbaImage& image, double distanceThreshold,
                                        bool preserveRoundHoles) {
    const int w = image.width;
    const int h = image.height;
    std::array<std::vector<double>, 4> border;
    auto addBorder = [&](int x, int y) {
        const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
        border[0].push_back(srgbToLinear(image.pixels[i] / 255.0));
        border[1].push_back(srgbToLinear(image.pixels[i + 1] / 255.0));
        border[2].push_back(srgbToLinear(image.pixels[i + 2] / 255.0));
        border[3].push_back(image.pixels[i + 3] / 255.0);
    };
    for (int x = 0; x < w; ++x) { addBorder(x, 0); if (h > 1) addBorder(x, h - 1); }
    for (int y = 1; y + 1 < h; ++y) { addBorder(0, y); if (w > 1) addBorder(w - 1, y); }

    const double br = median(std::move(border[0]));
    const double bg = median(std::move(border[1]));
    const double bb = median(std::move(border[2]));
    const double ba = median(std::move(border[3]));
    distanceThreshold = std::clamp(distanceThreshold, 0.005, 0.40);

    std::vector<std::uint8_t> barrier(static_cast<std::size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t p = static_cast<std::size_t>(y) * w + x;
            const std::size_t i = p * 4;
            const double r = srgbToLinear(image.pixels[i] / 255.0);
            const double g = srgbToLinear(image.pixels[i + 1] / 255.0);
            const double b = srgbToLinear(image.pixels[i + 2] / 255.0);
            const double a = image.pixels[i + 3] / 255.0;
            const double colour = std::sqrt((sqr(r - br) + sqr(g - bg) + sqr(b - bb)) / 3.0);
            const double alpha = std::abs(a - ba);
            barrier[p] = std::max(colour, alpha) >= distanceThreshold ? 1 : 0;
        }
    }

    std::vector<std::uint8_t> exterior(static_cast<std::size_t>(w) * h, 0);
    std::queue<std::pair<int, int>> pending;
    auto seed = [&](int x, int y) {
        const std::size_t p = static_cast<std::size_t>(y) * w + x;
        if (!barrier[p] && !exterior[p]) { exterior[p] = 1; pending.emplace(x, y); }
    };
    for (int x = 0; x < w; ++x) { seed(x, 0); if (h > 1) seed(x, h - 1); }
    for (int y = 1; y + 1 < h; ++y) { seed(0, y); if (w > 1) seed(w - 1, y); }
    constexpr std::array<int, 4> dx{1, -1, 0, 0};
    constexpr std::array<int, 4> dy{0, 0, 1, -1};
    while (!pending.empty()) {
        const auto [x, y] = pending.front(); pending.pop();
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            const std::size_t p = static_cast<std::size_t>(ny) * w + nx;
            if (!barrier[p] && !exterior[p]) { exterior[p] = 1; pending.emplace(nx, ny); }
        }
    }

    // Keep the largest sealed component.  Isolated compression/noise pixels at
    // the page border must never become separate laser paths.
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(w) * h, 0);
    std::vector<std::size_t> largest;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t start = static_cast<std::size_t>(y) * w + x;
        if (exterior[start] || seen[start]) continue;
        std::vector<std::size_t> component;
        seen[start] = 1; pending.emplace(x, y);
        while (!pending.empty()) {
            const auto [cx, cy] = pending.front(); pending.pop();
            const std::size_t cp = static_cast<std::size_t>(cy) * w + cx;
            component.push_back(cp);
            for (int k = 0; k < 4; ++k) {
                const int nx = cx + dx[k], ny = cy + dy[k];
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                const std::size_t p = static_cast<std::size_t>(ny) * w + nx;
                if (!exterior[p] && !seen[p]) { seen[p] = 1; pending.emplace(nx, ny); }
            }
        }
        if (component.size() > largest.size()) largest = std::move(component);
    }
    std::vector<double> mask(static_cast<std::size_t>(w) * h, 0.0);
    for (const std::size_t p : largest) mask[p] = 1.0;

    if (preserveRoundHoles) {
        // Recover only regular enclosed background components.  Irregular white
        // highlights and snow stay part of the product, while manufactured
        // hanging holes are cut back out of the sealed silhouette.
        std::fill(seen.begin(), seen.end(), std::uint8_t{0});
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const std::size_t start = static_cast<std::size_t>(y) * w + x;
            if (!mask[start] || barrier[start] || exterior[start] || seen[start]) continue;
            std::vector<std::size_t> cavity;
            seen[start] = 1; pending.emplace(x, y);
            int minX = x, maxX = x, minY = y, maxY = y;
            double sumX = 0.0, sumY = 0.0;
            while (!pending.empty()) {
                const auto [cx, cy] = pending.front(); pending.pop();
                const std::size_t cp = static_cast<std::size_t>(cy) * w + cx;
                cavity.push_back(cp); sumX += cx; sumY += cy;
                minX = std::min(minX, cx); maxX = std::max(maxX, cx);
                minY = std::min(minY, cy); maxY = std::max(maxY, cy);
                for (int k = 0; k < 4; ++k) {
                    const int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    const std::size_t p = static_cast<std::size_t>(ny) * w + nx;
                    if (mask[p] && !barrier[p] && !exterior[p] && !seen[p]) {
                        seen[p] = 1; pending.emplace(nx, ny);
                    }
                }
            }
            const int boxW = maxX - minX + 1, boxH = maxY - minY + 1;
            if (cavity.size() < 20 || std::min(boxW, boxH) < 5) continue;
            const double aspect = static_cast<double>(boxW) / boxH;
            const double fill = static_cast<double>(cavity.size()) / (boxW * boxH);
            const double cx = sumX / cavity.size(), cy = sumY / cavity.size();
            std::vector<double> radii;
            for (const std::size_t p : cavity) {
                const int px = static_cast<int>(p % w), py = static_cast<int>(p / w);
                bool boundary = false;
                for (int k = 0; k < 4; ++k) {
                    const int nx = px + dx[k], ny = py + dy[k];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) { boundary = true; break; }
                    const std::size_t q = static_cast<std::size_t>(ny) * w + nx;
                    if (barrier[q] || exterior[q]) { boundary = true; break; }
                }
                if (boundary) radii.push_back(std::hypot(px - cx, py - cy));
            }
            if (radii.size() < 8) continue;
            const double mean = std::accumulate(radii.begin(), radii.end(), 0.0) / radii.size();
            double variance = 0.0;
            for (double radius : radii) variance += sqr(radius - mean);
            const double relativeStd = std::sqrt(variance / radii.size()) / std::max(1.0, mean);
            if (aspect >= 0.72 && aspect <= 1.39 && fill >= 0.55 && fill <= 0.90 && relativeStd <= 0.17)
                for (const std::size_t p : cavity) mask[p] = 0.0;
        }
    }
    return mask;
}

std::vector<double> scalarField(const RgbaImage& image, const ContourOptions& options,
                                int& w, int& h, int& padding, double sigma = -1.0) {
    if (sigma < 0.0) sigma = options.gaussianSigma;
    padding = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)) + 1);
    w = image.width + 2 * padding;
    h = image.height + 2 * padding;
    std::vector<double> f(static_cast<std::size_t>(w) * h, 0.0);
    const bool sealedAutoSilhouette = options.sourceMode == SourceMode::AutoProduct;
    const auto autoMask = sealedAutoSilhouette
        ? exteriorProductMask(image, options.backgroundDistance,
                              options.pathMode != PathMode::OuterOnly) : std::vector<double>{};
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::size_t src = (static_cast<std::size_t>(y) * image.width + x) * 4;
            const double r = image.pixels[src] / 255.0;
            const double g = image.pixels[src + 1] / 255.0;
            const double b = image.pixels[src + 2] / 255.0;
            const double a = image.pixels[src + 3] / 255.0;
            const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            double value = a;
            if (sealedAutoSilhouette)
                value = autoMask[static_cast<std::size_t>(y) * image.width + x];
            if (options.sourceMode == SourceMode::DarkPixels) value = (1.0 - luma) * a;
            if (options.sourceMode == SourceMode::LightPixels) value = luma * a;
            f[static_cast<std::size_t>(y + padding) * w + (x + padding)] = value;
        }
    }
    gaussianBlur(f, w, h, sigma);
    return f;
}

double bilinear(const std::vector<double>& field, int w, int h, double x, double y) {
    x = std::clamp(x, 0.0, static_cast<double>(w - 1));
    y = std::clamp(y, 0.0, static_cast<double>(h - 1));
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const double tx = x - x0, ty = y - y0;
    const double a = field[static_cast<std::size_t>(y0) * w + x0] * (1.0 - tx) +
                     field[static_cast<std::size_t>(y0) * w + x1] * tx;
    const double b = field[static_cast<std::size_t>(y1) * w + x0] * (1.0 - tx) +
                     field[static_cast<std::size_t>(y1) * w + x1] * tx;
    return a * (1.0 - ty) + b * ty;
}

void refineSubpixelMultiScale(std::vector<std::vector<Point>>& loops, const RgbaImage& image,
                              const ContourOptions& options) {
    // A real alpha channel already supplies a direct continuous coverage value;
    // mixing blurred scales would move that authoritative edge.  Multi-scale
    // fusion is reserved for inferred photographic/product silhouettes.
    if (!options.multiScaleSubpixel || options.sourceMode != SourceMode::AutoProduct || loops.empty()) return;
    struct ScaleField { std::vector<double> data; int w{}; int h{}; int padding{}; };
    std::array<ScaleField, 3> scales;
    for (std::size_t i = 0; i < scales.size(); ++i) {
        const double sigma = std::clamp(options.subpixelSigmas[i], 0.0, 4.0);
        scales[i].data = scalarField(image, options, scales[i].w, scales[i].h,
                                     scales[i].padding, sigma);
    }
    for (auto& loop : loops) for (Point& point : loop) {
        Point fused{};
        double totalWeight = 0.0;
        for (const auto& scale : scales) {
            const double gx0 = point.x + scale.padding - 0.5;
            const double gy0 = point.y + scale.padding - 0.5;
            const double value = bilinear(scale.data, scale.w, scale.h, gx0, gy0);
            const double gx = bilinear(scale.data, scale.w, scale.h, gx0 + 0.5, gy0) -
                              bilinear(scale.data, scale.w, scale.h, gx0 - 0.5, gy0);
            const double gy = bilinear(scale.data, scale.w, scale.h, gx0, gy0 + 0.5) -
                              bilinear(scale.data, scale.w, scale.h, gx0, gy0 - 0.5);
            const double gradient = std::hypot(gx, gy);
            if (gradient < 1e-5) continue;
            const double step = std::clamp((options.threshold - value) / gradient, -0.85, 0.85);
            const Point candidate{point.x + gx / gradient * step,
                                  point.y + gy / gradient * step};
            const double weight = gradient / (1.0 + std::abs(step));
            fused.x += candidate.x * weight;
            fused.y += candidate.y * weight;
            totalWeight += weight;
        }
        if (totalWeight > 0.0) point = {fused.x / totalWeight, fused.y / totalWeight};
    }
}

Point interpolate(Point a, Point b, double va, double vb, double threshold, double coordinateOffset) {
    const double denom = vb - va;
    const double t = std::abs(denom) < kEps ? 0.5 : std::clamp((threshold - va) / denom, 0.0, 1.0);
    return {a.x + (b.x - a.x) * t - coordinateOffset,
            a.y + (b.y - a.y) * t - coordinateOffset};
}

std::vector<Segment> marchingSquares(const std::vector<double>& f, int w, int h,
                                     double threshold, int padding) {
    std::vector<Segment> out;
    out.reserve(static_cast<std::size_t>(w) * h / 2);
    for (int y = 0; y < h - 1; ++y) {
        for (int x = 0; x < w - 1; ++x) {
            const double v0 = f[static_cast<std::size_t>(y) * w + x];
            const double v1 = f[static_cast<std::size_t>(y) * w + x + 1];
            const double v2 = f[static_cast<std::size_t>(y + 1) * w + x + 1];
            const double v3 = f[static_cast<std::size_t>(y + 1) * w + x];
            const int c = (v0 >= threshold ? 1 : 0) | (v1 >= threshold ? 2 : 0) |
                          (v2 >= threshold ? 4 : 0) | (v3 >= threshold ? 8 : 0);
            if (c == 0 || c == 15) continue;

            const double offset = static_cast<double>(padding) - 0.5;
            const Point top = interpolate({double(x), double(y)}, {double(x + 1), double(y)}, v0, v1, threshold, offset);
            const Point right = interpolate({double(x + 1), double(y)}, {double(x + 1), double(y + 1)}, v1, v2, threshold, offset);
            const Point bottom = interpolate({double(x + 1), double(y + 1)}, {double(x), double(y + 1)}, v2, v3, threshold, offset);
            const Point left = interpolate({double(x), double(y + 1)}, {double(x), double(y)}, v3, v0, threshold, offset);
            auto add = [&](Point a, Point b) { if (dist2(a, b) > kEps) out.push_back({a, b}); };
            switch (c) {
                case 1: add(left, top); break;
                case 2: add(top, right); break;
                case 3: add(left, right); break;
                case 4: add(right, bottom); break;
                case 5: {
                    const double center = 0.25 * (v0 + v1 + v2 + v3);
                    if (center >= threshold) { add(top, right); add(bottom, left); }
                    else { add(left, top); add(right, bottom); }
                    break;
                }
                case 6: add(top, bottom); break;
                case 7: add(left, bottom); break;
                case 8: add(bottom, left); break;
                case 9: add(bottom, top); break;
                case 10: {
                    const double center = 0.25 * (v0 + v1 + v2 + v3);
                    if (center >= threshold) { add(left, top); add(right, bottom); }
                    else { add(top, right); add(bottom, left); }
                    break;
                }
                case 11: add(right, bottom); break;
                case 12: add(right, left); break;
                case 13: add(top, right); break;
                case 14: add(left, top); break;
                default: break;
            }
        }
    }
    return out;
}

std::vector<std::vector<Point>> connectSegments(const std::vector<Segment>& segments, std::size_t& openPaths) {
    struct Ref { std::size_t edge; bool atA; };
    std::unordered_map<Key, std::vector<Ref>, KeyHash> adjacency;
    adjacency.reserve(segments.size() * 2);
    for (std::size_t i = 0; i < segments.size(); ++i) {
        adjacency[keyOf(segments[i].a)].push_back({i, true});
        adjacency[keyOf(segments[i].b)].push_back({i, false});
    }
    std::vector<bool> used(segments.size(), false);
    std::vector<std::vector<Point>> loops;
    for (std::size_t seed = 0; seed < segments.size(); ++seed) {
        if (used[seed]) continue;
        std::vector<Point> path;
        path.reserve(128);
        used[seed] = true;
        path.push_back(segments[seed].a);
        path.push_back(segments[seed].b);
        const Key start = keyOf(path.front());
        Key current = keyOf(path.back());
        bool closed = current == start;
        while (!closed) {
            const auto it = adjacency.find(current);
            if (it == adjacency.end()) break;
            std::size_t next = std::numeric_limits<std::size_t>::max();
            bool atA = false;
            for (const Ref& ref : it->second) {
                if (!used[ref.edge]) { next = ref.edge; atA = ref.atA; break; }
            }
            if (next == std::numeric_limits<std::size_t>::max()) break;
            used[next] = true;
            const Point p = atA ? segments[next].b : segments[next].a;
            path.push_back(p);
            current = keyOf(p);
            closed = current == start;
            if (path.size() > segments.size() + 1) break;
        }
        if (!closed) { ++openPaths; continue; }
        if (path.size() > 1 && keyOf(path.front()) == keyOf(path.back())) path.pop_back();
        if (path.size() >= 3) loops.push_back(std::move(path));
    }
    return loops;
}

void rdpRange(const std::vector<Point>& p, std::size_t first, std::size_t last, double tol,
              std::vector<bool>& keep) {
    if (last <= first + 1) return;
    double maxDistance = -1.0;
    std::size_t index = first;
    for (std::size_t i = first + 1; i < last; ++i) {
        const double d = pointSegmentDistance(p[i], p[first], p[last]);
        if (d > maxDistance) { maxDistance = d; index = i; }
    }
    if (maxDistance > tol) {
        keep[index] = true;
        rdpRange(p, first, index, tol, keep);
        rdpRange(p, index, last, tol, keep);
    }
}

std::vector<Point> simplifyClosed(const std::vector<Point>& input, double tolerance) {
    if (input.size() <= 4 || tolerance <= 0.0) return input;
    std::size_t a = 0;
    std::size_t b = 1;
    double best = -1.0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const double d = dist2(input[a], input[i]);
        if (d > best) { best = d; b = i; }
    }
    std::vector<Point> ordered;
    ordered.reserve(input.size() + 1);
    for (std::size_t i = a; i <= a + input.size(); ++i) ordered.push_back(input[i % input.size()]);
    b = (b + input.size() - a) % input.size();
    std::vector<bool> keep(ordered.size(), false);
    keep[0] = keep[b] = keep.back() = true;
    rdpRange(ordered, 0, b, tolerance, keep);
    rdpRange(ordered, b, ordered.size() - 1, tolerance, keep);
    std::vector<Point> out;
    for (std::size_t i = 0; i + 1 < ordered.size(); ++i) if (keep[i]) out.push_back(ordered[i]);
    return out.size() >= 3 ? out : input;
}

double orient(const Point& a, const Point& b, const Point& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool properIntersection(const Point& a, const Point& b, const Point& c, const Point& d) {
    const double o1 = orient(a, b, c), o2 = orient(a, b, d);
    const double o3 = orient(c, d, a), o4 = orient(c, d, b);
    return ((o1 > kEps && o2 < -kEps) || (o1 < -kEps && o2 > kEps)) &&
           ((o3 > kEps && o4 < -kEps) || (o3 < -kEps && o4 > kEps));
}

std::size_t crossIntersections(const std::vector<Point>& a, const std::vector<Point>& b) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j)
            if (properIntersection(a[i], a[(i + 1) % a.size()], b[j], b[(j + 1) % b.size()])) ++count;
    return count;
}

std::size_t selfIntersections(const std::vector<Point>& p) {
    std::size_t count = 0;
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (j == i || j == (i + 1) % n || i == (j + 1) % n) continue;
            if (properIntersection(p[i], p[(i + 1) % n], p[j], p[(j + 1) % n])) ++count;
        }
    }
    return count;
}

bool pointInPolygon(const Point& p, const std::vector<Point>& poly) {
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const auto& a = poly[i]; const auto& b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            p.x < (b.x - a.x) * (p.y - a.y) / ((b.y - a.y) + kEps) + a.x) inside = !inside;
    }
    return inside;
}

double maxDeviation(const std::vector<Point>& source, const std::vector<Point>& simplified) {
    double maximum = 0.0;
    for (const Point& p : source) {
        double nearest = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < simplified.size(); ++i)
            nearest = std::min(nearest, pointSegmentDistance(p, simplified[i], simplified[(i + 1) % simplified.size()]));
        maximum = std::max(maximum, nearest);
    }
    return maximum;
}

std::size_t duplicateSegments(const std::vector<Loop>& loops) {
    struct EdgeKey { Key a; Key b; bool operator==(const EdgeKey& r) const noexcept { return a == r.a && b == r.b; } };
    struct EdgeHash { std::size_t operator()(const EdgeKey& e) const noexcept { return KeyHash{}(e.a) ^ (KeyHash{}(e.b) << 1); } };
    std::unordered_set<EdgeKey, EdgeHash> seen;
    std::size_t duplicates = 0;
    for (const Loop& loop : loops) {
        for (std::size_t i = 0; i < loop.points.size(); ++i) {
            Key a = keyOf(loop.points[i]), b = keyOf(loop.points[(i + 1) % loop.points.size()]);
            if (b.x < a.x || (b.x == a.x && b.y < a.y)) std::swap(a, b);
            if (!seen.insert({a, b}).second) ++duplicates;
        }
    }
    return duplicates;
}

} // namespace

ContourResult traceContours(const RgbaImage& image, const ContourOptions& inOptions) {
    ContourResult result;
    if (!image.valid()) { result.error = "Invalid RGBA image buffer."; return result; }
    ContourOptions options = inOptions;
    options.threshold = std::clamp(options.threshold, 0.001, 0.999);
    options.gaussianSigma = std::clamp(options.gaussianSigma, 0.0, 8.0);
    options.simplifyTolerance = std::max(0.0, options.simplifyTolerance);
    options.minArea = std::max(0.0, options.minArea);

    int fieldW = 0, fieldH = 0, padding = 0;
    auto field = scalarField(image, options, fieldW, fieldH, padding);
    auto segments = marchingSquares(field, fieldW, fieldH, options.threshold, padding);
    result.quality.sourceSegments = segments.size();
    auto rawLoops = connectSegments(segments, result.quality.openPaths);
    refineSubpixelMultiScale(rawLoops, image, options);

    result.loops.reserve(rawLoops.size());
    std::vector<std::vector<Point>> acceptedRawLoops;
    acceptedRawLoops.reserve(rawLoops.size());
    for (const auto& raw : rawLoops) {
        if (std::abs(signedArea(raw)) < options.minArea) continue;
        double tolerance = options.simplifyTolerance;
        std::vector<Point> simplified = raw;
        for (int attempt = 0; attempt < 8; ++attempt) {
            simplified = simplifyClosed(raw, tolerance);
            if (selfIntersections(simplified) == 0) break;
            tolerance *= 0.5;
        }
        result.quality.maxDeviationPixels = std::max(result.quality.maxDeviationPixels,
                                                      maxDeviation(raw, simplified));
        result.loops.push_back({std::move(simplified), 0.0, 0});
        result.loops.back().signedArea = signedArea(result.loops.back().points);
        acceptedRawLoops.push_back(raw);
    }

    // Independent RDP simplification can make two very close loops cross. In that
    // case retain the original subpixel samples for both loops instead of emitting
    // laser-invalid topology.
    std::vector<bool> restore(result.loops.size(), false);
    for (std::size_t i = 0; i < result.loops.size(); ++i) {
        for (std::size_t j = i + 1; j < result.loops.size(); ++j) {
            if (crossIntersections(result.loops[i].points, result.loops[j].points) != 0)
                restore[i] = restore[j] = true;
        }
    }
    for (std::size_t i = 0; i < result.loops.size(); ++i) {
        if (restore[i]) {
            result.loops[i].points = acceptedRawLoops[i];
            result.loops[i].signedArea = signedArea(result.loops[i].points);
        }
    }

    for (std::size_t i = 0; i < result.loops.size(); ++i) {
        Point probe = result.loops[i].points.front();
        int depth = 0;
        for (std::size_t j = 0; j < result.loops.size(); ++j) {
            if (i != j && std::abs(result.loops[j].signedArea) > std::abs(result.loops[i].signedArea) &&
                pointInPolygon(probe, result.loops[j].points)) ++depth;
        }
        result.loops[i].nestingDepth = depth;
        const bool outer = (depth % 2) == 0;
        if ((outer && result.loops[i].signedArea < 0.0) ||
            (!outer && result.loops[i].signedArea > 0.0)) {
            std::reverse(result.loops[i].points.begin(), result.loops[i].points.end());
            result.loops[i].signedArea = -result.loops[i].signedArea;
        }
    }

    result.loops.erase(std::remove_if(result.loops.begin(), result.loops.end(), [&](const Loop& loop) {
        if (options.pathMode == PathMode::OuterOnly) return (loop.nestingDepth % 2) != 0;
        if (options.pathMode == PathMode::HolesOnly) return (loop.nestingDepth % 2) == 0;
        return false;
    }), result.loops.end());

    for (const Loop& loop : result.loops) {
        result.quality.outputSegments += loop.points.size();
        result.quality.selfIntersections += selfIntersections(loop.points);
    }
    for (std::size_t i = 0; i < result.loops.size(); ++i)
        for (std::size_t j = i + 1; j < result.loops.size(); ++j)
            result.quality.selfIntersections += crossIntersections(result.loops[i].points,
                                                                    result.loops[j].points);
    result.quality.duplicateSegments = duplicateSegments(result.loops);
    if (result.quality.outputSegments > options.maxNodes) {
        result.quality.nodeLimitExceeded = true;
        result.error = "Output node limit exceeded; increase tolerance or minimum area.";
    }
    return result;
}

} // namespace scm
