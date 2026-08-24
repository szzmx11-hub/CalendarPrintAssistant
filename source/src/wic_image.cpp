#include "wic_image.hpp"

#include <wincodec.h>
#include <wrl/client.h>

#include <limits>

namespace scm {

using Microsoft::WRL::ComPtr;

bool loadRgbaWithWic(const std::wstring& path, RgbaImage& image, std::wstring& error) {
    image = {};
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { error = L"无法创建 Windows 图像解码器。"; return false; }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) { error = L"无法读取 CorelDRAW 导出的临时 PNG。"; return false; }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) { error = L"无法读取图像帧。"; return false; }

    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0 ||
        width > static_cast<UINT>(std::numeric_limits<int>::max()) ||
        height > static_cast<UINT>(std::numeric_limits<int>::max())) {
        error = L"位图尺寸无效。";
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) { error = L"无法创建 RGBA 转换器。"; return false; }
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { error = L"位图无法转换为 RGBA。"; return false; }

    const std::uint64_t byteCount = static_cast<std::uint64_t>(width) * height * 4;
    if (byteCount > std::numeric_limits<UINT>::max()) {
        error = L"位图过大，超过单次解码限制。";
        return false;
    }
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.pixels.resize(static_cast<std::size_t>(byteCount));
    hr = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(byteCount), image.pixels.data());
    if (FAILED(hr)) { image = {}; error = L"RGBA 像素读取失败。"; return false; }
    return true;
}

} // namespace scm
