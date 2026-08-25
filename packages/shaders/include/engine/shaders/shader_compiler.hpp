#pragma once

#include <engine/core/types.hpp>

#include <string>
#include <vector>

namespace engine::shaders {

enum class ShaderTarget : u8 {
    Dxil,
    Spirv,
};

struct ShaderCompileDesc {
    std::string file_path;
    std::string entry_point = "main";
    std::string target_profile = "vs_6_0";
    ShaderTarget target = ShaderTarget::Dxil;
};

struct ShaderBytecode {
    std::vector<u8> data;
};

class IShaderCompiler {
public:
    virtual ~IShaderCompiler() = default;

    virtual bool compile(const ShaderCompileDesc& desc, ShaderBytecode& out, std::string& error_log) = 0;
    virtual bool last_compile_from_cache() const = 0;
};

} // namespace engine::shaders
