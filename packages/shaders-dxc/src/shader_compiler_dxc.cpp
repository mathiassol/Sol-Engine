#include <engine/shaders/dxc/shader_compiler_dxc.hpp>

#include <engine/core/log.hpp>

#include <wrl/client.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxcapi.h>

#include <cstdio>
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

using DxcCreateInstanceFn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);

// Where a SPIR-V-capable DXC lives.
//
// The copy this package links implicitly - the Windows SDK's - is built
// without -DENABLE_SPIRV_CODEGEN and answers "SPIR-V CodeGen not available"
// for any -spirv request. The Vulkan SDK ships a second build with DirectX
// codegen disabled instead. They are different binaries with the same name, and
// the loader dedups by resolved path rather than by base name, so both live in
// one process without a rename - measured, not assumed.
std::wstring find_spirv_dxc() {
    wchar_t override_path[512] = {};
    if (GetEnvironmentVariableW(
            L"ENGINE_DXC_SPIRV", override_path, static_cast<DWORD>(std::size(override_path)))
        > 0) {
        return override_path;
    }
    wchar_t sdk[480] = {};
    if (GetEnvironmentVariableW(L"VULKAN_SDK", sdk, static_cast<DWORD>(std::size(sdk))) > 0) {
        return std::wstring(sdk) + L"\\Bin\\dxcompiler.dll";
    }
    return {};
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

    bool compile(
        const ShaderCompileDesc& desc, ShaderBytecode& out, std::string& error_log) override {
        if (!utils_ || !compiler_ || !includes_) {
            error_log = "DXC compiler is not available";
            return false;
        }

        const bool spirv = desc.target == ShaderTarget::Spirv;
        if (spirv && !ensure_spirv_compiler()) {
            error_log = "No SPIR-V-capable DXC is available";
            return false;
        }
        // Which of the two compilers this call goes to. Everything below uses
        // these rather than the members, so a target that resolves to the wrong
        // DLL is one line to find instead of five.
        IDxcUtils* dxc_utils = spirv ? spirv_utils_.Get() : utils_.Get();
        IDxcCompiler3* dxc_compiler = spirv ? spirv_compiler_.Get() : compiler_.Get();
        IDxcIncludeHandler* dxc_includes = spirv ? spirv_includes_.Get() : includes_.Get();

        const std::wstring wpath = utf8_to_wide(desc.file_path);
        const std::wstring wentry = utf8_to_wide(desc.entry_point);
        const std::wstring wtarget = utf8_to_wide(desc.target_profile);
        if (wpath.empty() || wentry.empty() || wtarget.empty()) {
            error_log = "Shader compile desc is empty";
            return false;
        }

        ComPtr<IDxcBlobEncoding> source;
        HRESULT hr = dxc_utils->LoadFile(wpath.c_str(), nullptr, &source);
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
        if (spirv) {
            args.push_back(L"-spirv");
            // The default HLSL->SPIR-V mapping sends register(xN, spaceM) to
            // set M binding N and *ignores the register type*, so b0, t0, u0
            // and s0 would all collide at set 0 binding 0. Disjoint ranges per
            // type fix it: b at 0, t at 16, u at 32, s at 48.
            //
            // These numbers are also written down in rhi-vulkan's
            // pipeline_vulkan.cpp, which builds the descriptor-set layout from
            // them. A change here without a change there is a shader reading
            // the wrong descriptor, with nothing logged.
            //
            // Two spaces because the tree's whole surface is b0, t0-t6, s0-s2,
            // u0-u1 and one t0 in space 1.
            for (const wchar_t* space : {L"0", L"1"}) {
                args.push_back(L"-fvk-t-shift");
                args.push_back(L"16");
                args.push_back(space);
                args.push_back(L"-fvk-u-shift");
                args.push_back(L"32");
                args.push_back(space);
                args.push_back(L"-fvk-s-shift");
                args.push_back(L"48");
                args.push_back(space);
            }
            // Keeps cbuffer packing matching the C++ structs the constants are
            // memcpy'd from.
            args.push_back(L"-fvk-use-dx-layout");
            // Deliberately NOT -fvk-invert-y. The Y flip is a negative viewport
            // height in the backend, so one HLSL source serves both APIs -
            // which is the property worth protecting.
        }

        DxcBuffer buffer{};
        buffer.Ptr = source->GetBufferPointer();
        buffer.Size = source->GetBufferSize();
        buffer.Encoding = DXC_CP_UTF8;

        ComPtr<IDxcResult> result;
        hr = dxc_compiler->Compile(&buffer, args.data(), static_cast<UINT32>(args.size()),
            dxc_includes, IID_PPV_ARGS(&result));
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
    // Resolved lazily and once. A DXIL-only run must not pay for a 20 MB DLL it
    // never uses, and a machine without the Vulkan SDK must not be told about
    // it on every startup - only when something actually asks for SPIR-V.
    bool ensure_spirv_compiler() {
        if (spirv_probed_) {
            return spirv_compiler_ != nullptr;
        }
        spirv_probed_ = true;

        const std::wstring path = find_spirv_dxc();
        if (path.empty()) {
            // Warn, not Error: an absent capability is the caller's decision to
            // escalate. A gate that only wants to know whether SPIR-V is
            // available should not print two red lines on a machine that is
            // configured exactly as its owner intended.
            log(LogLevel::Warn, LogChannel::Render,
                "No SPIR-V-capable DXC was found, so SPIR-V compilation is unavailable. "
                "Install the Vulkan SDK (it sets VULKAN_SDK), or point ENGINE_DXC_SPIRV at "
                "a dxcompiler.dll built with -DENABLE_SPIRV_CODEGEN=ON.");
            return false;
        }

        const HMODULE module =
            LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) {
            // Say the path and the error. "could not be loaded" sent me looking
            // at escaping when the answer was in GetLastError.
            const DWORD error = GetLastError();
            char narrow[320] = {};
            WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, narrow,
                static_cast<int>(sizeof(narrow)) - 1, nullptr, nullptr);
            char message[448];
            std::snprintf(message, sizeof(message),
                "SPIR-V DXC could not be loaded from '%s' (error %lu)", narrow,
                static_cast<unsigned long>(error));
            log(LogLevel::Error, LogChannel::Render, message);
            return false;
        }
        auto create = reinterpret_cast<DxcCreateInstanceFn>(
            reinterpret_cast<void*>(GetProcAddress(module, "DxcCreateInstance")));
        if (!create) {
            log(LogLevel::Error, LogChannel::Render,
                "SPIR-V DXC has no DxcCreateInstance export");
            return false;
        }
        if (FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&spirv_utils_)))
            || FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&spirv_compiler_)))
            || FAILED(spirv_utils_->CreateDefaultIncludeHandler(&spirv_includes_))) {
            log(LogLevel::Error, LogChannel::Render, "SPIR-V DXC would not instantiate");
            spirv_compiler_.Reset();
            spirv_utils_.Reset();
            spirv_includes_.Reset();
            return false;
        }
        log(LogLevel::Info, LogChannel::Render, "SPIR-V DXC loaded");
        return true;
    }

    ComPtr<IDxcUtils> utils_;
    ComPtr<IDxcCompiler3> compiler_;
    ComPtr<IDxcIncludeHandler> includes_;
    bool spirv_probed_ = false;
    ComPtr<IDxcUtils> spirv_utils_;
    ComPtr<IDxcCompiler3> spirv_compiler_;
    ComPtr<IDxcIncludeHandler> spirv_includes_;
};

} // namespace

std::unique_ptr<IShaderCompiler> create_compiler() {
    return std::make_unique<DxcShaderCompiler>();
}

} // namespace engine::shaders::dxc
