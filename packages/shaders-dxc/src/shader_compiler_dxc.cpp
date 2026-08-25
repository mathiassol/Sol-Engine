#include <engine/shaders/dxc/shader_compiler_dxc.hpp>

#include <engine/core/log.hpp>

#include <wrl/client.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxcapi.h>

#include <filesystem>
#include <string>
#include <vector>

#pragma comment(lib, "dxcompiler.lib")

namespace engine::shaders::dxc {

namespace {

using Microsoft::WRL::ComPtr;

std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int wide_count = MultiByteToWideChar(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (wide_count <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(wide_count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        wide.data(), wide_count);
    return wide;
}

bool gpu_debug_enabled() {
#ifdef NDEBUG
    return false;
#else
    char value[8] = {};
    return GetEnvironmentVariableA("ENGINE_GPU_DEBUG", value, sizeof(value)) > 0 && value[0] == '1';
#endif
}

class DxcShaderCompiler final : public IShaderCompiler {
public:
    DxcShaderCompiler() {
        if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils_)))
            || FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_)))
            || FAILED(utils_->CreateDefaultIncludeHandler(&includes_))) {
            log(LogLevel::Error, LogChannel::Render,
                "Failed to create DXC compiler (is dxcompiler.dll next to the exe?)");
            utils_.Reset();
            compiler_.Reset();
            includes_.Reset();
        }
    }

    bool last_compile_from_cache() const override { return false; }

    bool compile(const ShaderCompileDesc& desc, ShaderBytecode& out, std::string& error_log) override {
        if (!utils_ || !compiler_ || !includes_) {
            error_log = "DXC compiler is not available";
            return false;
        }

        if (desc.target != ShaderTarget::Dxil) {
            error_log = "Shader target SPIR-V is not implemented (DXC backend emits DXIL only)";
            log(LogLevel::Error, LogChannel::Render, error_log);
            return false;
        }

        const std::wstring wpath = utf8_to_wide(desc.file_path);
        const std::wstring wentry = utf8_to_wide(desc.entry_point);
        const std::wstring wtarget = utf8_to_wide(desc.target_profile);
        if (wpath.empty() || wentry.empty() || wtarget.empty()) {
            error_log = "Shader compile desc is empty";
            return false;
        }

        ComPtr<IDxcBlobEncoding> source;
        HRESULT hr = utils_->LoadFile(wpath.c_str(), nullptr, &source);
        if (FAILED(hr) || !source) {
            error_log = "Failed to open shader file";
            return false;
        }

        const std::filesystem::path parent =
            std::filesystem::path(std::string(desc.file_path)).parent_path();
        const std::wstring winclude = utf8_to_wide(parent.string());

        std::vector<LPCWSTR> args;
        args.push_back(wpath.c_str());
        args.push_back(L"-E");
        args.push_back(wentry.c_str());
        args.push_back(L"-T");
        args.push_back(wtarget.c_str());
        if (!winclude.empty()) {
            args.push_back(L"-I");
            args.push_back(winclude.c_str());
        }
        if (gpu_debug_enabled()) {
            args.push_back(L"-Zi");
            args.push_back(L"-Od");
            args.push_back(L"-Qembed_debug");
        }

        DxcBuffer buffer{};
        buffer.Ptr = source->GetBufferPointer();
        buffer.Size = source->GetBufferSize();
        buffer.Encoding = DXC_CP_UTF8;

        ComPtr<IDxcResult> result;
        hr = compiler_->Compile(&buffer, args.data(), static_cast<UINT32>(args.size()),
            includes_.Get(), IID_PPV_ARGS(&result));
        if (FAILED(hr) || !result) {
            error_log = "DXC Compile() failed";
            log(LogLevel::Error, LogChannel::Render, error_log);
            return false;
        }

        HRESULT status = S_OK;
        result->GetStatus(&status);

        ComPtr<IDxcBlobUtf8> errors;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors && errors->GetStringLength() > 0) {
            error_log.assign(errors->GetStringPointer(), errors->GetStringLength());
        }

        if (FAILED(status)) {
            if (error_log.empty()) {
                error_log = "Shader compilation failed";
            }
            log(LogLevel::Error, LogChannel::Render, error_log);
            return false;
        }

        ComPtr<IDxcBlob> object;
        result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
        if (!object || object->GetBufferSize() == 0) {
            error_log = "DXC produced no shader object";
            return false;
        }

        const auto* bytes = static_cast<const u8*>(object->GetBufferPointer());
        out.data.assign(bytes, bytes + object->GetBufferSize());
        error_log.clear();
        return true;
    }

private:
    ComPtr<IDxcUtils> utils_;
    ComPtr<IDxcCompiler3> compiler_;
    ComPtr<IDxcIncludeHandler> includes_;
};

} // namespace

std::unique_ptr<IShaderCompiler> create_compiler() {
    return std::make_unique<DxcShaderCompiler>();
}

} // namespace engine::shaders::dxc
