#include <engine/debug/stats_overlay.hpp>

#include <engine/core/log.hpp>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <span>
#include <vector>

namespace engine::debug {

namespace {

struct Vertex {
    f32 x = 0.f;
    f32 y = 0.f;
    f32 z = 0.f;
};

struct ScreenConstants {
    f32 screen_width = 0.f;
    f32 screen_height = 0.f;
};

constexpr u8 kGlyphHeight = 8;
constexpr u8 kGlyphWidth  = 8;
constexpr u8 kGlyphAdvance = 9;

constexpr u8 kFontSpace[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr u8 kFontColon[8] = {0x00, 0x00, 0x18, 0x00, 0x00, 0x18, 0x00, 0x00};
constexpr u8 kFontDot[8]   = {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00};
constexpr u8 kFontF[8]     = {0x7E, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x60, 0x00};
constexpr u8 kFontP[8]     = {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00};
constexpr u8 kFontS[8]     = {0x3C, 0x60, 0x30, 0x1C, 0x06, 0x66, 0x3C, 0x00};
constexpr u8 kFontC[8]     = {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00};
constexpr u8 kFontU[8]     = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00};
constexpr u8 kFontM[8]     = {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00};
constexpr u8 kFontE[8]     = {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00};
constexpr u8 kFontG[8]     = {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00};
constexpr u8 kFontR[8]     = {0x7C, 0x66, 0x66, 0x7C, 0x68, 0x64, 0x66, 0x00};
constexpr u8 kFontX[8]     = {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00};
constexpr u8 kFontN[8]     = {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00};
constexpr u8 kFontA[8]     = {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00};
constexpr u8 kFontSlash[8] = {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00};
constexpr u8 kFontPct[8]   = {0xC2, 0xC4, 0x08, 0x10, 0x20, 0x43, 0x83, 0x00};
constexpr u8 kFontO[8]     = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00};
constexpr u8 kFontT[8]     = {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00};
constexpr u8 kFont0[8]     = {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00};
constexpr u8 kFont1[8]     = {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00};
constexpr u8 kFont2[8]     = {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00};
constexpr u8 kFont3[8]     = {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00};
constexpr u8 kFont4[8]     = {0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00};
constexpr u8 kFont5[8]     = {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00};
constexpr u8 kFont6[8]     = {0x3C, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x3C, 0x00};
constexpr u8 kFont7[8]     = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00};
constexpr u8 kFont8[8]     = {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00};
constexpr u8 kFont9[8]     = {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00};

const u8* glyph_rows(char c) {
    switch (c) {
    case ' ': return kFontSpace;
    case ':': return kFontColon;
    case '.': return kFontDot;
    case 'F': return kFontF;
    case 'P': return kFontP;
    case 'S': return kFontS;
    case 'C': return kFontC;
    case 'E': return kFontE;
    case 'U': return kFontU;
    case 'm': return kFontM;
    case 'G': return kFontG;
    case 'R': return kFontR;
    case 'X': return kFontX;
    case 'n': return kFontN;
    case 'a': return kFontA;
    case '/': return kFontSlash;
    case '%': return kFontPct;
    case 'O': return kFontO;
    case 'T': return kFontT;
    case 'M': return kFontM;
    case '0': return kFont0;
    case '1': return kFont1;
    case '2': return kFont2;
    case '3': return kFont3;
    case '4': return kFont4;
    case '5': return kFont5;
    case '6': return kFont6;
    case '7': return kFont7;
    case '8': return kFont8;
    case '9': return kFont9;
    default:  return kFontSpace;
    }
}

void append_quad(std::vector<Vertex>& vertices, f32 x, f32 y, f32 w, f32 h) {
    const Vertex quad[] = {
        {x,     y,     0.f},
        {x + w, y,     0.f},
        {x + w, y + h, 0.f},
        {x,     y,     0.f},
        {x + w, y + h, 0.f},
        {x,     y + h, 0.f},
    };
    vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
}

void append_glyph(std::vector<Vertex>& vertices, char c, f32 origin_x, f32 origin_y) {
    const u8* rows = glyph_rows(c);
    for (u8 row = 0; row < kGlyphHeight; ++row) {
        for (u8 col = 0; col < kGlyphWidth; ++col) {
            if (rows[row] & (1u << (7u - col))) {
                append_quad(vertices, origin_x + static_cast<f32>(col),
                    origin_y + static_cast<f32>(row), 1.f, 1.f);
            }
        }
    }
}

rhi::GraphicsPipelineDesc overlay_pipeline_desc(std::span<const u8> vs, std::span<const u8> ps) {
    rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.attributes[0] = {rhi::VertexSemantic::Position, 0, rhi::VertexFormat::Float3, 0};
    desc.attribute_count = 1;
    desc.constant_buffer_count = 1;
    desc.depth = rhi::DepthTest::Disabled;
    desc.cull = rhi::CullMode::None;
    desc.blend = rhi::BlendMode::Alpha;
    desc.color_format = rhi::Format::RGBA8_UNORM;
    desc.debug_name = "overlay";
    return desc;
}

} // namespace

bool StatsOverlay::init(rhi::IDevice& device, shaders::IShaderCompiler& compiler,
    std::string_view shader_path) {
    device_ = &device;

    shaders::ShaderCompileDesc vs_desc{};
    vs_desc.file_path = shader_path;
    vs_desc.entry_point = "vs_main";
    vs_desc.target_profile = "vs_6_0";

    shaders::ShaderCompileDesc ps_desc = vs_desc;
    ps_desc.entry_point = "ps_main";
    ps_desc.target_profile = "ps_6_0";

    shaders::ShaderBytecode vs_bytecode;
    shaders::ShaderBytecode ps_bytecode;
    std::string error;
    if (!compiler.compile(vs_desc, vs_bytecode, error)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Stats overlay vertex shader compile failed");
        return false;
    }
    if (!compiler.compile(ps_desc, ps_bytecode, error)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Stats overlay pixel shader compile failed");
        return false;
    }

    pipeline_ = device.create_graphics_pipeline(
        overlay_pipeline_desc(vs_bytecode.data, ps_bytecode.data));
    if (!pipeline_) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Stats overlay pipeline creation failed");
        return false;
    }

    rebuild_mesh("FPS:0.0 P:0.0 X:0.0 E:0.0 G:0.0");
    return true;
}

void StatsOverlay::update(const FrameStats& stats) {
    // Widened past the old 80 when R: was added - snprintf truncates in silence,
    // and a clipped overlay reads as a rendering bug rather than a short buffer.
    char text[112];
    if (stats.aa && stats.aa[0] != '\0') {
        std::snprintf(text, sizeof(text), "FPS:%.1f P:%.1f X:%.1f E:%.1f G:%.1f R:%.0f%% %s",
            stats.fps, stats.poll_ms, stats.extract_ms, stats.execute_ms, stats.gpu_ms,
            static_cast<double>(stats.ring_pct), stats.aa);
    } else {
        std::snprintf(text, sizeof(text), "FPS:%.1f P:%.1f X:%.1f E:%.1f G:%.1f R:%.0f%%",
            stats.fps, stats.poll_ms, stats.extract_ms, stats.execute_ms, stats.gpu_ms,
            static_cast<double>(stats.ring_pct));
    }
    if (cached_text_ == text) {
        return;
    }
    cached_text_ = text;
    rebuild_mesh(cached_text_);
}

void StatsOverlay::rebuild_mesh(std::string_view text) {
    std::vector<Vertex> vertices;
    vertices.reserve(text.size() * kGlyphWidth * kGlyphHeight * 6);

    constexpr f32 origin_x = 12.f;
    constexpr f32 origin_y = 12.f;
    f32 pen_x = origin_x;
    for (char c : text) {
        append_glyph(vertices, c, pen_x, origin_y);
        pen_x += kGlyphAdvance;
    }

    vertex_count_ = static_cast<u32>(vertices.size());
    if (vertex_count_ == 0) {
        return;
    }

    rhi::BufferDesc vb_desc{};
    vb_desc.size  = vertices.size() * sizeof(Vertex);
    vb_desc.usage = rhi::BufferUsage::Vertex;
    vertex_buffer_ = device_->create_buffer(vb_desc, vertices.data());
}

void StatsOverlay::draw(rhi::ICommandList& cmd, u32 width, u32 height) {
    if (!visible_ || !pipeline_ || !vertex_buffer_ || vertex_count_ == 0) {
        return;
    }

    ScreenConstants constants{};
    constants.screen_width  = static_cast<f32>(std::max(width, 1u));
    constants.screen_height = static_cast<f32>(std::max(height, 1u));
    const rhi::FrameAllocation slice = device_->alloc_frame_memory(sizeof(constants));
    if (!slice.buffer) {
        return;
    }
    device_->write_buffer(*slice.buffer, slice.offset, &constants, sizeof(constants));

    cmd.set_pipeline(*pipeline_);
    cmd.set_constant_buffer(0, *slice.buffer, slice.offset);
    cmd.set_vertex_buffer(0, *vertex_buffer_, sizeof(Vertex));
    cmd.draw(vertex_count_);
}

} // namespace engine::debug
