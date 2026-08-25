#include <engine/assets/png/image_loader_png.hpp>

#include <engine/core/log.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>

#include <fstream>
#include <iterator>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace engine::assets::png {

namespace {

template <typename T>
void release(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

bool decode_wic(IWICBitmapDecoder* decoder, ImageData& out) {
    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        return false;
    }

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)))) {
        release(frame);
        return false;
    }

    IWICFormatConverter* converter = nullptr;
    const bool ok = SUCCEEDED(factory->CreateFormatConverter(&converter))
        && SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
    if (!ok) {
        release(converter);
        release(factory);
        release(frame);
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0) {
        release(converter);
        release(factory);
        release(frame);
        return false;
    }

    const UINT stride = width * 4;
    const UINT bytes = stride * height;
    out.width = width;
    out.height = height;
    out.rgba.resize(bytes);
    const HRESULT hr = converter->CopyPixels(nullptr, stride, bytes, out.rgba.data());

    release(converter);
    release(factory);
    release(frame);
    return SUCCEEDED(hr);
}

void ensure_com() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)hr;
}

} // namespace

bool load_png_bytes(std::span<const u8> bytes, ImageData& out) {
    out = {};
    if (bytes.empty()) {
        return false;
    }

    ensure_com();

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)))) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets, "WIC factory creation failed");
        return false;
    }

    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    bool ok = SUCCEEDED(factory->CreateStream(&stream))
        && SUCCEEDED(stream->InitializeFromMemory(
            const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())))
        && SUCCEEDED(factory->CreateDecoderFromStream(
            stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder))
        && decode_wic(decoder, out);

    release(decoder);
    release(stream);
    release(factory);

    if (!ok) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets, "PNG decode failed");
        out = {};
    }
    return ok;
}

bool load_png_file(std::string_view path, ImageData& out) {
    std::ifstream file{std::string(path), std::ios::binary};
    if (!file) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets, "Failed to open PNG");
        return false;
    }
    std::vector<u8> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return load_png_bytes(bytes, out);
}

} // namespace engine::assets::png
