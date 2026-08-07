#pragma once

#include <engine/core/types.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::shaders {

struct ShaderCompileDesc {
    std::string_view file_path;
    std::string_view entry_point = "main";
    std::string_view target_profile = "vs_5_0";
};

struct ShaderBytecode {
    std::vector<u8> data;
};

class IShaderCompiler {
public:
    virtual ~IShaderCompiler() = default;

    virtual bool compile(const ShaderCompileDesc& desc, ShaderBytecode& out, std::string& error_log) = 0;
};

} // namespace engine::shaders
