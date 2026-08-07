#pragma once

#include <engine/core/types.hpp>

#include <string_view>

namespace engine::rhi {

enum class Format : u8 { Unknown, RGBA8_UNORM, RGBA16_FLOAT, D32_FLOAT };
enum class BufferUsage : u8 { Vertex, Index, Uniform, Storage };
enum class TextureUsage : u8 { RenderTarget, DepthStencil, ShaderResource };

struct BufferDesc {
    usize size = 0;
    BufferUsage usage = BufferUsage::Vertex;
};

struct TextureDesc {
    u32 width  = 0;
    u32 height = 0;
    Format format = Format::RGBA8_UNORM;
    TextureUsage usage = TextureUsage::RenderTarget;
};

class IBuffer {
public:
    virtual ~IBuffer() = default;
    virtual usize size() const = 0;
};

class ITexture {
public:
    virtual ~ITexture() = default;
    virtual u32 width() const = 0;
    virtual u32 height() const = 0;
    virtual Format format() const = 0;
};

} // namespace engine::rhi
