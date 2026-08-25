#pragma once

#include <engine/shaders/shader_compiler.hpp>

#include <memory>
#include <string>

namespace engine::shaders {

struct WatchedShaderPair {
    ShaderCompileDesc vertex;
    ShaderCompileDesc pixel;
};

enum class ShaderReloadStatus : u8 { Unchanged, Reloaded, Failed, Busy };

class IShaderHotReloader {
public:
    virtual ~IShaderHotReloader() = default;

    virtual void begin_watch(const WatchedShaderPair& sources) = 0;
    // Queue a compile on the worker. poll() must not wait for DXC.
    virtual void request_compile() = 0;
    virtual ShaderReloadStatus poll(
        ShaderBytecode& out_vertex,
        ShaderBytecode& out_pixel,
        std::string& error_log) = 0;
};

} // namespace engine::shaders
