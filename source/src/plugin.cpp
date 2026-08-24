#include <windows.h>

#include "corel_bridge.hpp"
#include "resource.h"
#include "vgcore.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <sstream>

static_assert(sizeof(void*) == 8, "SCM AutoContour must be compiled as x64.");

namespace {

constexpr wchar_t kDialogCommand[] = L"SCM.AutoContour.Native64";
constexpr wchar_t kOuterCommand[] = L"SCM.AutoContour.Native64.Outer";
constexpr wchar_t kOuterHolesCommand[] = L"SCM.AutoContour.Native64.OuterHoles";
constexpr wchar_t kHolesCommand[] = L"SCM.AutoContour.Native64.Holes";
constexpr wchar_t kRegistryPath[] = L"Software\\SCM\\AutoContourV9";
HINSTANCE g_module = nullptr;

bool isScmCommand(const _bstr_t& command) {
    return command == _bstr_t(kDialogCommand) || command == _bstr_t(kOuterCommand) ||
           command == _bstr_t(kOuterHolesCommand) || command == _bstr_t(kHolesCommand);
}

std::wstring readRegistryText(const wchar_t* name, const wchar_t* fallback) {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return fallback;
    wchar_t value[256]{};
    DWORD type{};
    DWORD bytes = sizeof(value);
    const LONG status = RegQueryValueExW(key, name, nullptr, &type,
                                         reinterpret_cast<BYTE*>(value), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return fallback;
    value[255] = L'\0';
    return value;
}

double readRegistryDouble(const wchar_t* name, double fallback, double minimum, double maximum) {
    const std::wstring text = readRegistryText(name, L"");
    if (text.empty()) return fallback;
    wchar_t* end = nullptr;
    const double value = std::wcstod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value)) return fallback;
    return std::clamp(value, minimum, maximum);
}

void writeRegistryText(const wchar_t* name, const std::wstring& value) {
    HKEY key{};
    DWORD disposition{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, &disposition) != ERROR_SUCCESS) return;
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
}

scm::RunOptions loadPanelOptions(const _bstr_t& command) {
    scm::RunOptions options;
    const std::wstring source = readRegistryText(L"SourceMode", L"auto");
    if (source == L"alpha") options.contour.sourceMode = scm::SourceMode::Alpha;
    else if (source == L"dark") options.contour.sourceMode = scm::SourceMode::DarkPixels;
    else if (source == L"light") options.contour.sourceMode = scm::SourceMode::LightPixels;
    else options.contour.sourceMode = scm::SourceMode::AutoProduct;

    if (command == _bstr_t(kOuterCommand)) options.contour.pathMode = scm::PathMode::OuterOnly;
    else if (command == _bstr_t(kHolesCommand)) options.contour.pathMode = scm::PathMode::HolesOnly;
    else options.contour.pathMode = scm::PathMode::All;

    options.contour.threshold = readRegistryDouble(L"ThresholdPercent", 50.0, 0.1, 99.9) / 100.0;
    options.contour.gaussianSigma = readRegistryDouble(L"GaussianSigma", 0.55, 0.0, 8.0);
    options.simplifyMillimeters = readRegistryDouble(L"SimplifyMillimeters", 0.03, 0.0, 10.0);
    options.minimumAreaSquareMillimeters = readRegistryDouble(L"MinimumAreaSquareMillimeters", 0.01, 0.0, 100000.0);
    options.offsetMillimeters = readRegistryDouble(L"OffsetMillimeters", 0.0, -1000.0, 1000.0);
    return options;
}

double readDouble(HWND dialog, int id, double fallback, double minimum, double maximum) {
    wchar_t text[64]{};
    GetDlgItemTextW(dialog, id, text, 64);
    wchar_t* end = nullptr;
    const double value = std::wcstod(text, &end);
    if (end == text || !std::isfinite(value)) return fallback;
    return std::clamp(value, minimum, maximum);
}

INT_PTR CALLBACK settingsProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* options = reinterpret_cast<scm::RunOptions*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        options = reinterpret_cast<scm::RunOptions*>(lParam);
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        CheckRadioButton(dialog, IDC_SOURCE_AUTO, IDC_SOURCE_LIGHT, IDC_SOURCE_AUTO);
        CheckRadioButton(dialog, IDC_PATH_ALL, IDC_PATH_HOLES, IDC_PATH_OUTER);
        SetDlgItemTextW(dialog, IDC_THRESHOLD, L"50");
        SetDlgItemTextW(dialog, IDC_BLUR, L"0.55");
        SetDlgItemTextW(dialog, IDC_TOLERANCE, L"0.03");
        SetDlgItemTextW(dialog, IDC_MINAREA, L"0.01");
        SetDlgItemTextW(dialog, IDC_OFFSET, L"0.00");
        return TRUE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wParam) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
        if (LOWORD(wParam) == IDC_RUN && options) {
            if (IsDlgButtonChecked(dialog, IDC_SOURCE_AUTO) == BST_CHECKED)
                options->contour.sourceMode = scm::SourceMode::AutoProduct;
            else if (IsDlgButtonChecked(dialog, IDC_SOURCE_DARK) == BST_CHECKED)
                options->contour.sourceMode = scm::SourceMode::DarkPixels;
            else if (IsDlgButtonChecked(dialog, IDC_SOURCE_LIGHT) == BST_CHECKED)
                options->contour.sourceMode = scm::SourceMode::LightPixels;
            else options->contour.sourceMode = scm::SourceMode::Alpha;

            if (IsDlgButtonChecked(dialog, IDC_PATH_OUTER) == BST_CHECKED)
                options->contour.pathMode = scm::PathMode::OuterOnly;
            else if (IsDlgButtonChecked(dialog, IDC_PATH_HOLES) == BST_CHECKED)
                options->contour.pathMode = scm::PathMode::HolesOnly;
            else options->contour.pathMode = scm::PathMode::All;

            options->contour.threshold = readDouble(dialog, IDC_THRESHOLD, 50.0, 0.1, 99.9) / 100.0;
            options->contour.gaussianSigma = readDouble(dialog, IDC_BLUR, 0.55, 0.0, 8.0);
            options->simplifyMillimeters = readDouble(dialog, IDC_TOLERANCE, 0.03, 0.0, 10.0);
            options->minimumAreaSquareMillimeters = readDouble(dialog, IDC_MINAREA, 0.01, 0.0, 100000.0);
            options->offsetMillimeters = readDouble(dialog, IDC_OFFSET, 0.0, -1000.0, 1000.0);
            EndDialog(dialog, IDOK);
            return TRUE;
        }
    }
    return FALSE;
}

class AutoContourPlugin final : public VGCore::IVGAppPlugin {
public:
    AutoContourPlugin() = default;

    STDMETHOD(QueryInterface)(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDispatch) *object = static_cast<IDispatch*>(this);
        else if (iid == __uuidof(VGCore::IVGAppPlugin)) *object = static_cast<VGCore::IVGAppPlugin*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHOD_(ULONG, AddRef)() override { return ++references_; }
    STDMETHOD_(ULONG, Release)() override {
        const ULONG count = --references_;
        if (!count) delete this;
        return count;
    }

    STDMETHOD(GetTypeInfoCount)(UINT*) override { return E_NOTIMPL; }
    STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    STDMETHOD(Invoke)(DISPID id, REFIID, LCID, WORD, DISPPARAMS* parameters,
                      VARIANT*, EXCEPINFO*, UINT*) override {
        if (id == 0x0014 && parameters && parameters->cArgs == 1) {
            const _bstr_t command(parameters->rgvarg[0].bstrVal);
            if (command == _bstr_t(kDialogCommand)) openDialog();
            else if (command == _bstr_t(kOuterCommand) ||
                     command == _bstr_t(kOuterHolesCommand) ||
                     command == _bstr_t(kHolesCommand)) runPreset(command);
        } else if (id == 0x0015 && parameters && parameters->cArgs == 3) {
            const _bstr_t command(parameters->rgvarg[2].bstrVal);
            if (isScmCommand(command) && parameters->rgvarg[1].pboolVal)
                *parameters->rgvarg[1].pboolVal = canRun() ? VARIANT_TRUE : VARIANT_FALSE;
        }
        return S_OK;
    }

    STDMETHOD(raw_OnLoad)(VGCore::IVGApplication* application) override {
        app_ = application;
        if (app_) app_->AddRef();
        return S_OK;
    }
    STDMETHOD(raw_StartSession)() override {
        try {
            app_->AddPluginCommand(_bstr_t(kDialogCommand), _bstr_t(L"SCM V9.1.1 原生自动寻边设置"),
                                   _bstr_t(L"从位图生成经过拓扑校验的激光切割闭合轮廓"));
            app_->AddPluginCommand(_bstr_t(kOuterCommand), _bstr_t(L"SCM V9.1.1 产品外形寻边"),
                                   _bstr_t(L"由 V8 风格泊坞窗直接生成产品最外围轮廓"));
            app_->AddPluginCommand(_bstr_t(kOuterHolesCommand), _bstr_t(L"SCM V9.1.1 外形与圆孔寻边"),
                                   _bstr_t(L"由 V8 风格泊坞窗直接生成外轮廓和孔位"));
            app_->AddPluginCommand(_bstr_t(kHolesCommand), _bstr_t(L"SCM V9.1.1 仅圆孔寻边"),
                                   _bstr_t(L"由 V8 风格泊坞窗直接生成孔位"));
            try {
                auto control = app_->CommandBars->Item[_bstr_t(L"Standard")]->Controls->AddCustomButton(
                    VGCore::cdrCmdCategoryPlugins, _bstr_t(kDialogCommand), 1, VARIANT_FALSE);
                if (control) control->SetIcon2(_bstr_t(L"guid://d2fdc0d9-09f8-4948-944c-4297395c05b7"));
            } catch (const _com_error&) {
                // Some workspaces disallow toolbar mutation; the command still remains available.
            }
            cookie_ = app_->AdviseEvents(this);
        } catch (const _com_error& e) {
            MessageBoxW(nullptr, e.Description(), L"SCM V9.1.1 插件加载错误", MB_OK | MB_ICONERROR);
        }
        return S_OK;
    }
    STDMETHOD(raw_StopSession)() override {
        try {
            if (cookie_) app_->UnadviseEvents(cookie_);
            app_->RemovePluginCommand(_bstr_t(kHolesCommand));
            app_->RemovePluginCommand(_bstr_t(kOuterHolesCommand));
            app_->RemovePluginCommand(_bstr_t(kOuterCommand));
            app_->RemovePluginCommand(_bstr_t(kDialogCommand));
        } catch (const _com_error&) {}
        cookie_ = 0;
        return S_OK;
    }
    STDMETHOD(raw_OnUnload)() override {
        if (app_) { app_->Release(); app_ = nullptr; }
        return S_OK;
    }

private:
    ~AutoContourPlugin() = default;

    bool canRun() const {
        try { return app_ && app_->Documents->Count > 0 && app_->ActiveSelectionRange->Count > 0; }
        catch (const _com_error&) { return false; }
    }

    void openDialog() {
        scm::RunOptions options;
        HWND owner = nullptr;
        try { owner = reinterpret_cast<HWND>(static_cast<INT_PTR>(app_->AppWindow->Handle)); }
        catch (const _com_error&) {}
        if (DialogBoxParamW(g_module, MAKEINTRESOURCEW(IDD_SCM_CONTOUR), owner,
                            settingsProc, reinterpret_cast<LPARAM>(&options)) != IDOK) return;

        scm::RunSummary summary;
        std::wstring error;
        if (!scm::runOnCorelSelection(app_, options, summary, error)) {
            MessageBoxW(owner, error.c_str(), L"SCM V9.1.1 自动寻边", MB_OK | MB_ICONERROR);
            return;
        }
        std::wostringstream text;
        text << L"已生成 " << summary.loopCount << L" 条闭合轮廓\n"
             << L"输出节点：" << summary.quality.outputSegments << L"\n"
             << L"最大几何误差：" << std::fixed << std::setprecision(4)
             << summary.maximumDeviationMillimeters << L" mm\n"
             << L"开口 / 自交 / 重复段：" << summary.quality.openPaths << L" / "
             << summary.quality.selfIntersections << L" / " << summary.quality.duplicateSegments;
        MessageBoxW(owner, text.str().c_str(), L"SCM V9.1.1 自动寻边质量报告", MB_OK | MB_ICONINFORMATION);
    }

    void runPreset(const _bstr_t& command) {
        writeRegistryText(L"LastStatus", L"running");
        writeRegistryText(L"LastMessage", L"V9.1.1 原生引擎正在处理位图");
        const scm::RunOptions options = loadPanelOptions(command);
        scm::RunSummary summary;
        std::wstring error;
        if (!scm::runOnCorelSelection(app_, options, summary, error)) {
            writeRegistryText(L"LastStatus", L"failed");
            writeRegistryText(L"LastMessage", error.empty() ? L"原生引擎没有生成路径" : error);
            return;
        }
        std::wostringstream text;
        text << L"已生成 " << summary.loopCount << L" 条闭合轮廓；节点 "
             << summary.quality.outputSegments << L"；直线 " << summary.lineCount
             << L"，圆/圆弧 " << (summary.circleCount + summary.arcCount)
             << L"，贝塞尔 " << summary.bezierCount << L"；最大几何误差 "
             << std::fixed << std::setprecision(4) << summary.maximumDeviationMillimeters << L" mm";
        writeRegistryText(L"LastMessage", text.str());
        writeRegistryText(L"LastStatus", L"success");
    }

    std::atomic<ULONG> references_{1};
    VGCore::IVGApplication* app_{};
    long cookie_{};
};

} // namespace

BOOL APIENTRY DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) DWORD APIENTRY AttachPlugin(VGCore::IVGAppPlugin** plugin) {
    if (!plugin) return 0;
    *plugin = new AutoContourPlugin();
    return 0x100;
}
