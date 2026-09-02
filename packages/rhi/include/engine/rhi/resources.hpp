#pragma once

#include <engine/core/types.hpp>
#include <engine/rhi/rhi.hpp>

#include <span>
#include <string_view>

namespace engine::rhi {

// RGBA8_UNORM_SRGB is the same bytes as RGBA8_UNORM; the difference is that the
// hardware applies the sRGB transfer function on sample, *before* filtering, so
// bilinear and trilinear interpolation happen in linear space. Use it for colour
// (albedo, emissive) and plain RGBA8_UNORM for data (metallic-roughness, normal
// maps, masks, LUTs). Alpha is never transformed either way.
//
// Note for a second backend: flip-model swapchains do not accept _SRGB formats,
// so a presented surface stays UNORM and the encode is applied in-shader. See
// packages/sandbox/content/shaders/common.hlsli.
enum class Format : u8 { Unknown, RGBA8_UNORM, RGBA8_UNORM_SRGB, RGBA16_FLOAT, D32_FLOAT };
enum class BufferUsage : u8 { Vertex, Index, Uniform, Storage, Readback };
enum class TextureDimension : u8 { Tex2D, Tex2DArray, Cube };
enum class TextureUsage : u8 {
    RenderTarget,
    DepthStencil,
    ShaderResource,
    DepthShaderResource,
    ColorShaderResource,
    // Written by a compute pass, sampled by a later graphics pass. Named
    // combinations rather than flags, matching the two above - a storage
    // texture nothing ever reads has no use here.
    StorageShaderResource,
};
enum class ResourceState : u8 {
    Common,
    Present,
    RenderTarget,
    DepthWrite,
    CopySrc,
    CopyDst,
    ShaderRead,
    Storage,
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

// Takes the convention rather than hard-coding a direction: a shadow sampler
// comparing the wrong way is a fully-lit or fully-shadowed scene, with nothing
// logged.
inline SamplerDesc shadow_comparison_sampler(DepthConvention convention) {
    SamplerDesc desc{};
    desc.filter = FilterMode::Linear;
    desc.address = AddressMode::Border;
    desc.compare = convention == DepthConvention::Reversed ? CompareOp::Greater : CompareOp::Less;
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
// Greater/GreaterEqual are the reversed-Z half. Which one a pipeline gets is
// derived from IDevice::depth_convention(), never chosen per pipeline - see
// DepthConvention in rhi.hpp.
enum class DepthTest : u8 { Disabled, Less, LessEqual, Equal, Greater, GreaterEqual };

// What a pass means is "the closer fragment wins"; *which* compare implements
// that depends on the convention. Say the intent and derive the mechanism -
// writing DepthTest::Less at a call site hard-codes Standard, and that is how
// five-of-six reversed-Z happens.
inline DepthTest depth_closer(DepthConvention convention) {
    return convention == DepthConvention::Reversed ? DepthTest::Greater : DepthTest::Less;
}

inline DepthTest depth_closer_or_equal(DepthConvention convention) {
    return convention == DepthConvention::Reversed ? DepthTest::GreaterEqual
                                                   : DepthTest::LessEqual;
}

// Shadow-map slope bias pushes samples *away* from the light, and which sign
// that is flips with the depth direction. A positive bias under reversed-Z
// pulls them toward it, which is shadow acne with no error.
inline f32 depth_bias_for(f32 magnitude, DepthConvention convention) {
    return convention == DepthConvention::Reversed ? -magnitude : magnitude;
}
enum class PrimitiveTopology : u8 { TriangleList, LineList };

struct VertexAttribute {
    VertexSemantic semantic = VertexSemantic::Position;
    u8 semantic_index = 0;
    VertexFormat format = VertexFormat::Float3;
    u32 offset = 0;
};

// ── The binding contract ─────────────────────────────────────────────────────
//
// A pipeline declares *how many* of each resource kind it uses, not where they
// sit. Backends allocate the layout. These four counts are the whole contract,
// and this table is what a second backend implements against - it exists so the
// interface can stay in neutral vocabulary while the translation stays written
// down rather than rediscovered.
//
//   count                   | D3D12 today                  | Vulkan would need
//   ------------------------|------------------------------|-------------------------
//   uniform_buffer_count    | root CBVs, b0..bN            | UNIFORM_BUFFER
//                           |                              | descriptors, or push
//                           |                              | constants for small ones
//   sampled_texture_count   | one SRV descriptor table,    | COMBINED_IMAGE_SAMPLER or
//                           | t0..tN, pixel-stage-visible  | SAMPLED_IMAGE in a set,
//                           |                              | stage flags narrowed to
//                           |                              | fragment to match
//   storage_buffer_count    | root SRVs in a second        | STORAGE_BUFFER descriptors
//                           | register space, all stages   | visible to all stages
//   storage_texture_count   | UAV descriptor table         | STORAGE_IMAGE descriptors
//                           | (compute pipelines only)     |
//   sampler_count           | static samplers baked into   | immutable samplers in the
//                           | the layout from SamplerDesc  | descriptor set layout
//
// Two asymmetries a backend has to preserve, because shaders depend on them:
// storage buffers are visible to every stage while sampled textures are not,
// and samplers are immutable - fixed at pipeline creation from SamplerDesc,
// never bound per draw.
//
// Counts rather than an explicit layout is the debt, and it is the D3D12 shape:
// WebGPU/Dawn chose the Vulkan binding model for the opposite reason, that
// Vulkan -> D3D12 is the cheap translation direction and D3D12 -> Vulkan the
// expensive one. Moving to bind groups is the expected answer, and the right
// time is when rhi-vulkan exists to validate it (ENGINE_MAP RHI #12, Far) -
// designing it against the one backend that exists is how abstractions get the
// wrong seams. Until then the debt is bounded by this table and the
// rhi-vocabulary invariant, which fails the build if D3D12 terms creep back in.

struct GraphicsPipelineDesc {
    static constexpr u32 kMaxAttributes = 8;
    static constexpr u32 kMaxSamplers = 8;

    std::span<const u8> vertex_shader;
    std::span<const u8> pixel_shader;
    // **The order of this array is significant.** It must match the
    // declaration order of the shader's vertex input struct.
    //
    // One backend resolves attributes by semantic *name* out of the shader's
    // input signature and does not care about the order; the other resolves
    // them by *position*, because the bytecode carries a slot number assigned
    // in declaration order and nothing else. So a reordering here is free on
    // the first backend and reads the wrong data on the second - measured, by
    // swapping two entries and watching one backend pass and the other return
    // the wrong pixels.
    //
    // For the same reason every `semantic_index` must be 0: a second index on
    // one semantic (TEXCOORD1) is its own slot, so array order stops
    // corresponding to declaration order. A backend that resolves by position
    // rejects a non-zero index by name rather than binding silently.
    VertexAttribute attributes[kMaxAttributes]{};
    u32 attribute_count = 0;
    u32 uniform_buffer_count = 0;
    u32 sampled_texture_count = 0;
    // Visible to every stage, unlike sampled_texture_count. See the binding
    // contract above for why that asymmetry exists and what it costs a backend.
    u32 storage_buffer_count = 0;
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
    // Must equal the sample count of every target this pipeline draws into.
    // The backend rejects a mismatch by name rather than returning null,
    // because a null pipeline reads as "feature off" further up.
    u32 sample_count = 1;
    std::string_view debug_name;
};

struct ComputePipelineDesc {
    std::span<const u8> compute_shader;
    u32 uniform_buffer_count = 0;
    u32 sampled_texture_count = 0;
    u32 storage_texture_count = 0;
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
    //
    // **What `data` must contain depends on the texture.** For a
    // single-layer RGBA8 or RGBA8_SRGB 2D texture, `data` is **mip 0 only**
    // and the backend generates the rest with a box filter - sRGB averaging in
    // light rather than in bytes. For anything else - a cube, an array, or any
    // other format - `data` must be the **whole chain**, slice-major then mip,
    // tightly packed.
    //
    // Written down because it was implicit and is the wrong way round from
    // what a reader expects: supplying a full chain for the generated case
    // silently discards everything past mip 0, because the backend refilters
    // level 0 over the top of it. Found by a gate that supplied one and got
    // mip 0's value back from level 1.
    u32 mip_levels = 1;
    // Cube is 6 faces. Tex2DArray is N slices. Tex2D stays 1.
    u32 array_size = 1;
    TextureDimension dimension = TextureDimension::Tex2D;
    Format format = Format::RGBA8_UNORM;
    TextureUsage usage = TextureUsage::RenderTarget;
    // 1 is no multisampling. A multisampled target cannot be sampled directly -
    // it is resolved into a single-sample texture first, which is what
    // RenderPassInfo::resolve does.
    u32 sample_count = 1;
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
    // 1 when not multisampled. A pipeline's sample count must match the
    // target it draws into, or pipeline creation fails.
    virtual u32 sample_count() const = 0;
};

} // namespace engine::rhi
