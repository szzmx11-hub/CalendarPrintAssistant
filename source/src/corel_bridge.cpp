#include "corel_bridge.hpp"

#include "wic_image.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>

namespace scm {
namespace {

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return std::wstring(text.begin(), text.end());
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), length);
    return result;
}

class TempPng {
public:
    TempPng() {
        wchar_t folder[MAX_PATH]{};
        wchar_t file[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, folder) && GetTempFileNameW(folder, L"SCM", 0, file)) {
            path_ = file;
            path_ += L".png";
            DeleteFileW(file);
        }
    }
    ~TempPng() { if (!path_.empty()) DeleteFileW(path_.c_str()); }
    const std::wstring& path() const noexcept { return path_; }
private:
    std::wstring path_;
};

VGCore::IVGShapePtr findBitmapInShapes(VGCore::IVGShapesPtr shapes) {
    if (!shapes) return nullptr;
    for (long i = 1; i <= shapes->Count; ++i) {
        VGCore::IVGShapePtr found;
        try {
            auto shape = shapes->Item[i];
            if (shape->Type == VGCore::cdrBitmapShape) return shape;
            if (shape->Shapes && shape->Shapes->Count > 0) {
                found = findBitmapInShapes(shape->Shapes);
                if (found) return found;
            }
            try {
                auto powerClip = shape->PowerClip;
                if (powerClip && powerClip->Shapes && powerClip->Shapes->Count > 0) {
                    found = findBitmapInShapes(powerClip->Shapes);
                    if (found) return found;
                }
            } catch (const _com_error&) {}
        } catch (const _com_error&) {}
    }
    return nullptr;
}

VGCore::IVGShapePtr selectedBitmap(VGCore::IVGApplication* app) {
    if (!app || app->Documents->Count == 0 || app->ActiveSelectionRange->Count == 0) return nullptr;
    return findBitmapInShapes(app->ActiveSelectionRange->Shapes);
}

struct Placement {
    double cx{}, cy{};
    double ux{1.0}, uy{};
    double vx{}, vy{1.0};
    double width{}, height{};
};

Placement getPlacement(VGCore::IVGShapePtr shape) {
    Placement p;
    shape->GetPositionEx(VGCore::cdrCenter, &p.cx, &p.cy);
    double d11 = 1, d12 = 0, d21 = 0, d22 = 1, tx = 0, ty = 0;
    shape->GetMatrix(&d11, &d12, &d21, &d22, &tx, &ty);
    const double sx = std::hypot(d11, d12);
    const double sy = std::hypot(d21, d22);
    if (sx > 1e-12) { p.ux = d11 / sx; p.uy = d12 / sx; }
    if (sy > 1e-12) { p.vx = d21 / sy; p.vy = d22 / sy; }
    p.width = std::abs(shape->OriginalWidth * sx);
    p.height = std::abs(shape->OriginalHeight * sy);
    if (p.width <= 1e-12 || p.height <= 1e-12) {
        p.width = shape->SizeWidth;
        p.height = shape->SizeHeight;
    }
    return p;
}

Point toDocument(const Point& pixel, int imageW, int imageH, const Placement& p) {
    const double nx = pixel.x / imageW - 0.5;
    const double ny = 0.5 - pixel.y / imageH;
    return {p.cx + nx * p.width * p.ux + ny * p.height * p.vx,
            p.cy + nx * p.width * p.uy + ny * p.height * p.vy};
}

double degrees(double radians) {
    return radians * 180.0 / 3.1415926535897932384626433832795;
}

void appendCubic(VGCore::IVGSubPathPtr subPath, const Point& start,
                 const Point& control1, const Point& control2, const Point& end) {
    const double startLength = std::hypot(control1.x - start.x, control1.y - start.y);
    const double endLength = std::hypot(control2.x - end.x, control2.y - end.y);
    const double startAngle = degrees(std::atan2(control1.y - start.y, control1.x - start.x));
    const double endAngle = degrees(std::atan2(control2.y - end.y, control2.x - end.x));
    subPath->AppendCurveSegment(end.x, end.y, startLength, startAngle,
                                endLength, endAngle, VARIANT_FALSE);
}

Point ellipsePoint(const v9::Primitive& primitive, double angle) {
    const double cosine = std::cos(primitive.rotation);
    const double sine = std::sin(primitive.rotation);
    const double x = primitive.radiusX * std::cos(angle);
    const double y = primitive.radiusY * std::sin(angle);
    return {primitive.center.x + cosine * x - sine * y,
            primitive.center.y + sine * x + cosine * y};
}

Point ellipseDerivative(const v9::Primitive& primitive, double angle) {
    const double cosine = std::cos(primitive.rotation);
    const double sine = std::sin(primitive.rotation);
    const double x = -primitive.radiusX * std::sin(angle);
    const double y = primitive.radiusY * std::cos(angle);
    return {cosine * x - sine * y, sine * x + cosine * y};
}

void appendAnalyticArc(VGCore::IVGSubPathPtr subPath, const v9::Primitive& input,
                       int imageW, int imageH, const Placement& placement, Point& current) {
    v9::Primitive primitive = input;
    if (primitive.type == v9::PrimitiveType::Circle || primitive.type == v9::PrimitiveType::CircularArc) {
        primitive.radiusX = primitive.radius;
        primitive.radiusY = primitive.radius;
        primitive.rotation = 0.0;
    }
    const double halfPi = 3.1415926535897932384626433832795 * 0.5;
    const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(primitive.sweepAngle) / halfPi)));
    const double delta = primitive.sweepAngle / pieces;
    double angle = primitive.startAngle;
    for (int piece = 0; piece < pieces; ++piece) {
        const double nextAngle = angle + delta;
        const double handle = 4.0 / 3.0 * std::tan(delta * 0.25);
        const Point p0 = ellipsePoint(primitive, angle);
        const Point p3 = ellipsePoint(primitive, nextAngle);
        const Point d0 = ellipseDerivative(primitive, angle);
        const Point d1 = ellipseDerivative(primitive, nextAngle);
        const Point c1Pixel{p0.x + handle * d0.x, p0.y + handle * d0.y};
        const Point c2Pixel{p3.x - handle * d1.x, p3.y - handle * d1.y};
        const Point p0Document = toDocument(p0, imageW, imageH, placement);
        const Point c1Document = toDocument(c1Pixel, imageW, imageH, placement);
        const Point c2Document = toDocument(c2Pixel, imageW, imageH, placement);
        const Point p3Document = toDocument(p3, imageW, imageH, placement);
        appendCubic(subPath, p0Document, c1Document, c2Document, p3Document);
        current = p3Document;
        angle = nextAngle;
    }
}

void appendPrimitive(VGCore::IVGSubPathPtr subPath, const v9::Primitive& primitive,
                     int imageW, int imageH, const Placement& placement, Point& current) {
    if (primitive.type == v9::PrimitiveType::Line) {
        const Point end = toDocument(primitive.end, imageW, imageH, placement);
        subPath->AppendLineSegment(end.x, end.y, VARIANT_FALSE);
        current = end;
        return;
    }
    if (primitive.type == v9::PrimitiveType::CubicBezier ||
        primitive.type == v9::PrimitiveType::CubicBSpline) {
        const Point start = toDocument(primitive.start, imageW, imageH, placement);
        const Point control1 = toDocument(primitive.control1, imageW, imageH, placement);
        const Point control2 = toDocument(primitive.control2, imageW, imageH, placement);
        const Point end = toDocument(primitive.end, imageW, imageH, placement);
        appendCubic(subPath, start, control1, control2, end);
        current = end;
        return;
    }
    appendAnalyticArc(subPath, primitive, imageW, imageH, placement, current);
}

} // namespace

bool runOnCorelSelection(VGCore::IVGApplication* app, const RunOptions& runOptions,
                         RunSummary& summary, std::wstring& error) {
    summary = {};
    try {
        auto bitmapShape = selectedBitmap(app);
        if (!bitmapShape) {
            error = L"请先选择一个位图，或选择包含位图的群组 / PowerClip。";
            return false;
        }
        auto bitmap = bitmapShape->Bitmap;
        if (!bitmap) { error = L"选中对象没有可读取的位图数据。"; return false; }

        TempPng temp;
        if (temp.path().empty()) { error = L"无法创建临时文件。"; return false; }
        auto filter = bitmap->SaveAs(_bstr_t(temp.path().c_str()), VGCore::cdrPNG, VGCore::cdrCompressionNone);
        if (filter) filter->Finish();

        RgbaImage image;
        if (!loadRgbaWithWic(temp.path(), image, error)) return false;
        const Placement placement = getPlacement(bitmapShape);

        auto options = runOptions.contour;
        auto document = app->ActiveDocument;
        const double toleranceDoc = document->ToUnits(runOptions.simplifyMillimeters, VGCore::cdrMillimeter);
        const double mmDoc = document->ToUnits(1.0, VGCore::cdrMillimeter);
        const double scaleX = placement.width / image.width;
        const double scaleY = placement.height / image.height;
        const double maxScale = std::max(scaleX, scaleY);
        options.simplifyTolerance = maxScale > 0 ? toleranceDoc / maxScale : 0.0;
        const double areaScale = std::max(1e-18, scaleX * scaleY);
        options.minArea = runOptions.minimumAreaSquareMillimeters * mmDoc * mmDoc / areaScale;

        v9::ProductionOptions productionOptions;
        productionOptions.contour = options;
        productionOptions.planner = runOptions.planner;
        productionOptions.offsetPixels = maxScale > 0.0
            ? document->ToUnits(runOptions.offsetMillimeters, VGCore::cdrMillimeter) / maxScale : 0.0;
        productionOptions.verificationP95Pixels = runOptions.verificationP95Pixels;
        productionOptions.verificationMaximumPixels = runOptions.verificationMaximumPixels;
        productionOptions.verificationCornerExtensionPixels =
            runOptions.verificationCornerExtensionPixels;
        productionOptions.enableOpenCvMultiscaleRefinement = true;
        productionOptions.enableCeresGlobalOptimization = true;
        productionOptions.rejectOnVerificationFailure = true;
        auto production = v9::buildManufacturingPaths(image, productionOptions);
        if (!production) {
            error = utf8ToWide(production.error);
            return false;
        }
        const std::vector<v9::PlannedPath>& plans = production.paths;

        document->BeginCommandGroup(_bstr_t(L"SCM V9 图元规划自动寻边"));
        try {
            auto curve = document->CreateCurve();
            for (const v9::PlannedPath& plan : plans) {
                const Point first = toDocument(plan.primitives.front().start, image.width, image.height, placement);
                auto subPath = curve->CreateSubPath(first.x, first.y);
                Point current = first;
                for (const v9::Primitive& primitive : plan.primitives)
                    appendPrimitive(subPath, primitive, image.width, image.height, placement, current);
                subPath->Closed = VARIANT_TRUE;
            }
            auto output = app->ActiveLayer->CreateCurve(curve);
            output->Name = _bstr_t(L"SCM_LASER_CONTOUR");
            output->Fill->ApplyNoFill();
            output->Outline->Color = app->CreateRGBColor(255, 0, 0);
            output->Outline->Width = document->ToUnits(0.001, VGCore::cdrMillimeter);
            output->Selected = VARIANT_TRUE;
            document->EndCommandGroup();
        } catch (...) {
            document->EndCommandGroup();
            throw;
        }

        summary.quality = production.contourQuality;
        // The contour engine reports sampled polyline nodes.  The CorelDRAW
        // output now consists of planned analytic primitives, so expose the
        // actual number of emitted nodes instead of adding both counts.
        summary.quality.outputSegments = 0;
        summary.loopCount = plans.size();
        for (const v9::PlannedPath& plan : plans) {
            summary.primitiveCount += plan.quality.outputPrimitives;
            summary.lineCount += plan.quality.lines;
            summary.circleCount += plan.quality.circles;
            summary.arcCount += plan.quality.circularArcs;
            summary.ellipseCount += plan.quality.ellipses + plan.quality.ellipticArcs;
            summary.bezierCount += plan.quality.cubicBeziers + plan.quality.cubicBSplines;
            summary.quality.outputSegments += plan.quality.outputNodes;
            summary.quality.maxDeviationPixels = std::max(summary.quality.maxDeviationPixels,
                                                          plan.quality.maxErrorPixels);
        }
        summary.quality.openPaths = production.quality.openPaths;
        summary.quality.selfIntersections = production.quality.selfIntersections;
        summary.quality.duplicateSegments = production.quality.duplicateSegments;
        summary.quality.maxDeviationPixels = std::max(summary.quality.maxDeviationPixels,
                                                      production.quality.hausdorffPixels);
        summary.maximumDeviationMillimeters = mmDoc > 0
            ? production.quality.hausdorffPixels * maxScale / mmDoc : 0.0;
        return true;
    } catch (const _com_error& e) {
        error = e.Description().length() ? static_cast<const wchar_t*>(e.Description()) : L"CorelDRAW COM 调用失败。";
        return false;
    } catch (const std::exception& e) {
        error = utf8ToWide(e.what());
        return false;
    }
}

} // namespace scm
