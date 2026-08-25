#pragma once

#include <engine/core/types.hpp>

#include <span>
#include <string_view>

namespace engine::rhi {

enum class Format : u8 { Unknown, RGBA8_UNORM, RGBA16_FLOAT, D32_FLOAT };
enum class BufferUsage : u8 { Vertex, Index, Uniform, Storage, Readback };
enum class TextureDimension : u8 { Tex2D, Tex2DArray, Cube };
enum class TextureUsage : u8 {
    RenderTarget,
    DepthStencil,
    ShaderResource,
    DepthShaderResource,
    ColorShaderResource,
};
enum class ResourceState : u8 {
    Common,
    Present,
    RenderTarget,
    DepthWrite,
    CopySrc,
    CopyDst,
    ShaderRead,
    UnorderedAccess,
};

enum class FilterMode : u8 { Point, Linear };
enum class AddressMode : u8 { Wrap, Clamp, Border };
enum class CompareOp : u8 {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

struct SamplerDesc {
    FilterMode filter = FilterMode::Linear;
    AddressMode address = AddressMode::Wrap;
    CompareOp compare = CompareOp::Never;
};

inline SamplerDesc linear_wrap_sampler() {
    SamplerDesc desc{};
    desc.filter = FilterMode::Linear;
    desc.address = AddressMode::Wrap;
    return desc;
}

inline SamplerDesc linear_clamp_sampler() {
    SamplerDesc desc{};
    desc.filter = FilterMode::Linear;
    desc.address = AddressMode::Clamp;
    return desc;
}

inline SamplerDesc point_clamp_sampler() {
    SamplerDesc desc{};
    desc.filter = FilterMode::Point;
    desc.address = AddressMode::Clamp;
    return desc;
}

inline SamplerDesc shadow_comparison_sampler() {
    SamplerDesc desc{};
    desc.filter = FilterMode::Linear;
    desc.address = AddressMode::Border;
    desc.compare = CompareOp::Less;
    return desc;
}

class ISampler {
public:
    virtual ~ISampler() = default;
};

class IComputePipeline {
public:
    virtual ~IComputePipeline() = default;
};

enum class VertexSemantic : u8 { Position, Normal, Color, TexCoord };
enum class VertexFormat : u8 { Float2, Float3, Float4 };
enum class CullMode : u8 { None, Back, Front };
enum class BlendMode : u8 { Opaque, Alpha };
enum class DepthTest : u8 { Disabled, Less, LessEqual, Equal };
enum class PrimitiveTopology : u8 { TriangleList, LineList };

struct VertexAttribute {
    VertexSemantic semantic = VertexSemantic::Position;
    u8 semantic_index = 0;
    VertexFormat format = VertexFormat::Float3;
    u32 offset = 0;
};

struct GraphicsPipelineDesc {
    static constexpr u32 kMaxAttributes = 8;
    static constexpr u32 kMaxSamplers = 8;

    std::span<const u8> vertex_shader;
    std::span<const u8> pixel_shader;
    VertexAttribute attributes[kMaxAttributes]{};
    u32 attribute_count = 0;
    u32 constant_buffer_count = 0;
    u32 shader_resource_count = 0;
    SamplerDesc samplers[kMaxSamplers]{};
    u32 sampler_count = 0;
    DepthTest depth = DepthTest::Disabled;
    bool depth_write = true;
    CullMode cull = CullMode::None;
    BlendMode blend = BlendMode::Opaque;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    Format color_format = Format::RGBA8_UNORM;
    Format depth_format = Format::Unknown;
    f32 slope_scaled_depth_bias = 0.f;
    std::string_view debug_name;
};

struct ComputePipelineDesc {
    std::span<const u8> compute_shader;
    u32 constant_buffer_count = 0;
    u32 shader_resource_count = 0;
    u32 unordered_access_count = 0;
    u32 sampler_count = 0;
    std::string_view debug_name;
};

struct BufferDesc {
    usize size = 0;
    BufferUsage usage = BufferUsage::Vertex;
};

struct TextureDesc {
    u32 width  = 0;
    u32 height = 0;
    // 0 means a full chain from width/height. 1 means the top mip only.
    u32 mip_levels = 1;
    // Cube is 6 faces. Tex2DArray is N slices. Tex2D stays 1.
    u32 array_size = 1;
    TextureDimension dimension = TextureDimension::Tex2D;
    Format format = Format::RGBA8_UNORM;
    TextureUsage usage = TextureUsage::RenderTarget;
};

inline u32 texture_array_size(const TextureDesc& desc) {
    if (desc.dimension == TextureDimension::Cube) {
        return desc.array_size == 0 ? 6u : desc.array_size;
    }
    if (desc.dimension == TextureDimension::Tex2DArray) {
        return desc.array_size == 0 ? 1u : desc.array_size;
    }
    return 1;
}

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
    virtual u32 mip_levels() const = 0;
    virtual u32 array_size() const = 0;
    virtual TextureDimension dimension() const = 0;
    virtual Format format() const = 0;
};

} // namespace engine::rhi
