#include <engine/shaders/dxc/shader_compiler_dxc.hpp>

#include <engine/core/log.hpp>

#include <d3dcompiler.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace engine::shaders::dxc {

namespace {

class DxcShaderCompiler final : public IShaderCompiler {
public:
    bool compile(const ShaderCompileDesc& desc, ShaderBytecode& out, std::string& error_log) override {
        std::ifstream file(std::string(desc.file_path), std::ios::binary | std::ios::ate);
        if (!file) {
            error_log = "Failed to open shader file";
            return false;
        }

        auto size = file.tellg();
        if (size <= 0) {
            error_log = "Shader file is empty";
            return false;
        }

        std::vector<char> source(static_cast<size_t>(size));
        file.seekg(0);
        file.read(source.data(), size);

        const std::string entry(desc.entry_point);
        const std::string target(desc.target_profile);
        const std::string path(desc.file_path);

        ID3DBlob* bytecode = nullptr;
        ID3DBlob* errors = nullptr;

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
        char debug_env[8] = {};
        if (GetEnvironmentVariableA("ENGINE_GPU_DEBUG", debug_env, sizeof(debug_env)) > 0
            && debug_env[0] == '1') {
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
        }

        HRESULT hr = D3DCompile(
            source.data(),
            source.size(),
            path.c_str(),
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entry.c_str(),
            target.c_str(),
            flags,
            0,
            &bytecode,
            &errors);

        if (FAILED(hr)) {
            if (errors) {
                error_log.assign(
                    static_cast<const char*>(errors->GetBufferPointer()),
                    errors->GetBufferSize());
                errors->Release();
            } else {
                error_log = "Shader compilation failed";
            }
            log(LogLevel::Error, LogChannel::Render, error_log);
            if (bytecode) bytecode->Release();
            return false;
        }

        out.data.assign(
            static_cast<const u8*>(bytecode->GetBufferPointer()),
            static_cast<const u8*>(bytecode->GetBufferPointer()) + bytecode->GetBufferSize());
        bytecode->Release();
        return true;
    }
};

} // namespace

std::unique_ptr<IShaderCompiler> create_compiler() {
    return std::make_unique<DxcShaderCompiler>();
}

} // namespace engine::shaders::dxc
