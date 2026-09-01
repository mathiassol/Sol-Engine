#include "sandbox_common.hpp"

namespace sandbox {

// Gate-only knobs. Nothing outside run_cvar_gate reads these, which is why the
// gate is free to drive them to any value and leave them there.
engine::Cvar cv_gate_bool{"gate.bool", false, "Cvar gate: bool knob"};
engine::Cvar cv_gate_int{"gate.int", 0, "Cvar gate: int knob"};
engine::Cvar cv_gate_float{"gate.float", 0.f, "Cvar gate: float knob"};
engine::Cvar cv_gate_string{"gate.string", "default", "Cvar gate: string knob"};
engine::Cvar cv_gate_prec{"gate.prec", 0, "Cvar gate: precedence knob"};
engine::Cvar cv_gate_args{"gate.args", 0, "Cvar gate: command-line knob"};
engine::Cvar cv_gate_file{"gate.file", 0, "Cvar gate: config-file knob"};
engine::Cvar cv_text_int{"gate.text_int", 0, "Cvar gate: text parser int"};
engine::Cvar cv_text_float{"gate.text_float", 0.f, "Cvar gate: text parser float"};
engine::Cvar cv_text_string{"gate.text_string", "unset", "Cvar gate: text parser string"};
engine::Cvar cv_text_eq{"gate.text_eq", 0, "Cvar gate: text parser no-space '=' knob"};
engine::Cvar cv_text_prec{
    "gate.text_prec", 0, "Cvar gate: text parser precedence-through-text knob"};
engine::Cvar cv_text_comment{
    "gate.text_comment", "unset", "Cvar gate: text parser trailing-comment knob"};

// The AA default is demo state, not engine state, so the knob is read here
// rather than in Engine::init.
engine::Cvar cv_aa{"r.aa", "off", "Anti-aliasing: off | fxaa | smaa | taa"};
// -2.0 EV (a 0.25x multiplier) was picked by screenshot sweep, not derived:
// mean frame luminance runs 203/255 at 0 EV, 168 at -1, 147 at -1.5 and 126 at
// -2.0, and -2.0 is where the sky keeps a gradient, the sun reads as a disc
// rather than a blown band, and the floor holds contrast into the distance.
engine::Cvar cv_exposure{"r.exposure", -2.0f,
    "Exposure in EV stops; the multiplier is 2^ev. Scales sun, sky and IBL together."};

// Quality presets (Build #10). One knob that moves the two the demo actually
// has: anti-aliasing and shadow resolution.
//
// "custom" is the default and means *no preset*, so the per-knob defaults below
// stand and an existing config.cfg behaves exactly as it did. None of the three
// presets reproduces today's pairing (Off + 1024), so making one of them the
// default would have changed behaviour while claiming not to.
//
// Startup only, and the help says so: the shadow map is a render-graph
// transient sized when the graph is built, so changing it mid-session means
// recreating that transient. That is its own row, not this one.
engine::Cvar cv_quality{"r.quality",
    "custom",
    "Quality preset: custom | low | medium | high. Applied at startup to r.aa and "
    "r.shadow_size; an explicit value for either of those wins."};
engine::Cvar cv_shadow_size{"r.shadow_size", 1024,
    "Shadow map edge in texels. A power of two from 256 to 4096."};

// A power of two so the mip chain and the PCF kernel stay well-defined, and
// bounded because the shadow map is a graph transient: 8192 would allocate
// 256 MB of D32 and fail somewhere less obvious than here.
bool valid_shadow_size(engine::u32 texels) {
    return texels >= 256 && texels <= 4096 && (texels & (texels - 1)) == 0;
}

// A preset is a default, not an override, so this reads whether a knob was set
// rather than writing through Cvar::set(). Passing the explicit values in as
// pointers - null meaning "not set" - keeps it a pure function the gate can
// drive without touching global cvar state.
//
// Returns false for a name that is not a preset. "custom" is a name, and means
// no preset was asked for.
bool resolve_quality(std::string_view preset, const engine::renderer::aa::Mode* explicit_aa,
    const engine::u32* explicit_shadow, QualitySettings& out) {
    out = QualitySettings{};
    bool known = preset == "custom";
    for (const QualityPreset& candidate : kQualityPresets) {
        if (preset == candidate.name) {
            out.aa = candidate.aa;
            out.shadow_size = candidate.shadow_size;
            known = true;
            break;
        }
    }
    if (explicit_aa != nullptr) {
        out.aa = *explicit_aa;
    }
    if (explicit_shadow != nullptr) {
        out.shadow_size = *explicit_shadow;
    }
    return known;
}

// What the current knobs ask for, read off the live cvars. Both setup_forward_demo
// and run_aa_gate call this, so the gate cannot drift from the behaviour it
// checks - which it did the moment r.quality became a second way to move the AA
// mode, and the gate still expected the factory default.
//
// `warn` because the setup path should report a bad knob once and the gate must
// not repeat it.
QualitySettings resolve_quality_from_cvars(bool warn) {
    engine::renderer::aa::Mode explicit_aa{};
    const engine::renderer::aa::Mode* explicit_aa_ptr = nullptr;
    if (cv_aa.source() != engine::CvarSource::Default) {
        if (engine::renderer::aa::parse_mode(cv_aa.as_string(), explicit_aa)) {
            explicit_aa_ptr = &explicit_aa;
        } else if (warn) {
            engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
                std::string("Cvar 'r.aa' expects ") + cv_aa.help());
        }
    }

    engine::u32 explicit_shadow = 0;
    const engine::u32* explicit_shadow_ptr = nullptr;
    if (cv_shadow_size.source() != engine::CvarSource::Default) {
        const engine::i32 asked = cv_shadow_size.as_int();
        if (asked > 0 && valid_shadow_size(static_cast<engine::u32>(asked))) {
            explicit_shadow = static_cast<engine::u32>(asked);
            explicit_shadow_ptr = &explicit_shadow;
        } else if (warn) {
            engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
                std::string("Cvar 'r.shadow_size' expects ") + cv_shadow_size.help());
        }
    }

    QualitySettings out{};
    if (!resolve_quality(cv_quality.as_string(), explicit_aa_ptr, explicit_shadow_ptr, out)
            && warn) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
            std::string("Cvar 'r.quality' expects ") + cv_quality.help());
    }
    return out;
}

void toggle_walk_mode(SandboxState& state) {
    if (state.walk_mode && state.forward) {
        if (state.player.spawned()) {
            state.game_camera.update(state.player.position());
        }
        state.forward->camera.yaw = state.game_camera.yaw;
        state.forward->camera.pitch = state.game_camera.pitch;
        state.forward->camera.position = state.game_camera.position();
    } else if (state.forward) {
        state.game_camera.yaw = state.forward->camera.yaw;
        state.game_camera.pitch = state.forward->camera.pitch;
    }
    state.walk_mode = !state.walk_mode;
}

sandbox::WorldExtractAssets make_extract_assets(ForwardDemo& demo) {
    sandbox::WorldExtractAssets assets{};
    assets.pipelines = demo.pipelines;
    assets.taa_history = nullptr;
    assets.taa_sample = demo.taa_frames;
    assets.taa_reset = !demo.taa_history_valid;
    assets.aa_mode = demo.aa_mode;
    assets.exposure = demo.exposure;
    assets.meshes = &demo.meshes;
    for (engine::u32 i = 0; i < sandbox::kHuskyVariantCount; ++i) {
        assets.husky_albedos[i] = demo.albedos[i].get();
    }
    assets.floor_albedo = demo.floor_albedo.get();
    assets.default_mr = demo.default_mr.get();
    assets.default_normal = demo.default_normal.get();
    assets.ibl_irradiance = demo.ibl_irradiance.get();
    assets.ibl_prefilter = demo.ibl_prefilter.get();
    assets.ibl_brdf_lut = demo.ibl_brdf_lut.get();
    assets.sky_cubemap = demo.sky_cubemap.get();
    return assets;
}

bool ensure_taa_history(engine::rhi::IDevice& device, engine::renderer::RenderGraph& graph,
    ForwardDemo& demo, engine::u32 width, engine::u32 height) {
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (demo.taa_history[0] && demo.taa_history[1] && demo.taa_history_w == width
        && demo.taa_history_h == height) {
        return true;
    }
    device.wait_idle();
    engine::rhi::TextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.format = engine::renderer::taa::kFormat;
    desc.usage = engine::rhi::TextureUsage::ColorShaderResource;
    demo.taa_history[0] = device.create_texture(desc);
    demo.taa_history[1] = device.create_texture(desc);
    if (!demo.taa_history[0] || !demo.taa_history[1]) {
        return false;
    }
    device.set_debug_name(*demo.taa_history[0], "taa_history_a");
    device.set_debug_name(*demo.taa_history[1], "taa_history_b");
    graph.bind_persistent(graph.find_resource("taa_history_a"), demo.taa_history[0].get());
    graph.bind_persistent(graph.find_resource("taa_history_b"), demo.taa_history[1].get());
    demo.taa_history_w = width;
    demo.taa_history_h = height;
    demo.taa_history_valid = false;
    return true;
}

engine::rhi::GraphicsPipelineDesc make_forward_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.attributes[0] = {engine::rhi::VertexSemantic::Position, 0,
        engine::rhi::VertexFormat::Float3, 0};
    desc.attributes[1] = {engine::rhi::VertexSemantic::Normal, 0,
        engine::rhi::VertexFormat::Float3, 12};
    desc.attributes[2] = {engine::rhi::VertexSemantic::TexCoord, 0,
        engine::rhi::VertexFormat::Float2, 24};
    desc.attribute_count = 3;
    desc.constant_buffer_count = 1;
    desc.shader_resource_count = 7;
    desc.samplers[0] = engine::rhi::linear_wrap_sampler();
    desc.samplers[1] = engine::rhi::shadow_comparison_sampler();
    desc.samplers[2] = engine::rhi::linear_clamp_sampler();
    desc.sampler_count = 3;
    desc.depth = engine::rhi::DepthTest::Less;
    desc.cull = engine::rhi::CullMode::Back;
    desc.blend = engine::rhi::BlendMode::Opaque;
    desc.color_format = engine::rhi::Format::RGBA16_FLOAT;
    desc.depth_format = engine::rhi::Format::D32_FLOAT;
    // One root SRV (t0, space1) holding the frame's per-instance array.
    desc.structured_buffer_count = 1;
    desc.debug_name = "forward";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_shadow_pipeline_desc(std::span<const engine::u8> vs) {
    engine::rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.attributes[0] = {engine::rhi::VertexSemantic::Position, 0,
        engine::rhi::VertexFormat::Float3, 0};
    desc.attribute_count = 1;
    desc.constant_buffer_count = 1;
    desc.depth = engine::rhi::DepthTest::Less;
    desc.cull = engine::rhi::CullMode::Front;
    desc.blend = engine::rhi::BlendMode::Opaque;
    desc.color_format = engine::rhi::Format::Unknown;
    desc.depth_format = engine::rhi::Format::D32_FLOAT;
    desc.slope_scaled_depth_bias = 1.5f;
    // One root SRV (t0, space1) holding the frame's per-instance array.
    desc.structured_buffer_count = 1;
    desc.debug_name = "shadow";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_tonemap_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.constant_buffer_count = 1;
    desc.shader_resource_count = 2;
    desc.samplers[0] = engine::rhi::linear_clamp_sampler();
    desc.sampler_count = 1;
    desc.depth = engine::rhi::DepthTest::Disabled;
    desc.cull = engine::rhi::CullMode::None;
    desc.blend = engine::rhi::BlendMode::Opaque;
    desc.color_format = engine::rhi::Format::RGBA8_UNORM;
    desc.debug_name = "tonemap";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_sky_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.constant_buffer_count = 1;
    desc.shader_resource_count = 1;
    desc.samplers[0] = engine::rhi::linear_clamp_sampler();
    desc.sampler_count = 1;
    desc.depth = engine::rhi::DepthTest::LessEqual;
    desc.depth_write = false;
    desc.cull = engine::rhi::CullMode::None;
    desc.blend = engine::rhi::BlendMode::Opaque;
    desc.color_format = engine::rhi::Format::RGBA16_FLOAT;
    desc.depth_format = engine::rhi::Format::D32_FLOAT;
    desc.debug_name = "sky";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_bloom_downsample_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.constant_buffer_count = 1;
    desc.shader_resource_count = 1;
    desc.samplers[0] = engine::rhi::linear_clamp_sampler();
    desc.sampler_count = 1;
    desc.depth = engine::rhi::DepthTest::Disabled;
    desc.cull = engine::rhi::CullMode::None;
    desc.blend = engine::rhi::BlendMode::Opaque;
    desc.color_format = engine::rhi::Format::RGBA16_FLOAT;
    desc.debug_name = "bloom_downsample";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_bloom_upsample_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.constant_buffer_count = 1;
    desc.shader_resource_count = 2;
    desc.samplers[0] = engine::rhi::linear_clamp_sampler();
    desc.sampler_count = 1;
    desc.depth = engine::rhi::DepthTest::Disabled;
    desc.cull = engine::rhi::CullMode::None;
    desc.blend = engine::rhi::BlendMode::Opaque;
    desc.color_format = engine::rhi::Format::RGBA16_FLOAT;
    desc.debug_name = "bloom_upsample";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_fxaa_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.constant_buffer_count = 1;
    desc.shader_resource_count = 1;
    desc.samplers[0] = engine::rhi::linear_clamp_sampler();
    desc.sampler_count = 1;
    desc.depth = engine::rhi::DepthTest::Disabled;
    desc.cull = engine::rhi::CullMode::None;
    desc.blend = engine::rhi::BlendMode::Opaque;
    desc.color_format = engine::rhi::Format::RGBA8_UNORM;
    desc.debug_name = "fxaa";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_smaa_edge_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc = make_fxaa_pipeline_desc(vs, ps);
    desc.debug_name = "smaa_edge";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_smaa_weights_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc = make_fxaa_pipeline_desc(vs, ps);
    desc.samplers[0] = engine::rhi::point_clamp_sampler();
    desc.debug_name = "smaa_weights";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_smaa_blend_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc = make_fxaa_pipeline_desc(vs, ps);
    desc.shader_resource_count = 2;
    desc.samplers[0] = engine::rhi::linear_clamp_sampler();
    desc.samplers[1] = engine::rhi::point_clamp_sampler();
    desc.sampler_count = 2;
    desc.debug_name = "smaa_blend";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_motion_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.attributes[0] = {engine::rhi::VertexSemantic::Position, 0,
        engine::rhi::VertexFormat::Float3, 0};
    desc.attributes[1] = {engine::rhi::VertexSemantic::Normal, 0,
        engine::rhi::VertexFormat::Float3, 12};
    desc.attributes[2] = {engine::rhi::VertexSemantic::TexCoord, 0,
        engine::rhi::VertexFormat::Float2, 24};
    desc.attribute_count = 3;
    desc.constant_buffer_count = 1;
    desc.depth = engine::rhi::DepthTest::Equal;
    desc.depth_write = false;
    desc.cull = engine::rhi::CullMode::Back;
    desc.blend = engine::rhi::BlendMode::Opaque;
    desc.color_format = engine::renderer::motion::kFormat;
    desc.depth_format = engine::rhi::Format::D32_FLOAT;
    // One root SRV (t0, space1) holding the frame's per-instance array.
    desc.structured_buffer_count = 1;
    desc.debug_name = "motion_vectors";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_taa_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.constant_buffer_count = 1;
    desc.shader_resource_count = 4;
    desc.samplers[0] = engine::rhi::linear_clamp_sampler();
    desc.samplers[1] = engine::rhi::point_clamp_sampler();
    desc.sampler_count = 2;
    desc.depth = engine::rhi::DepthTest::Disabled;
    desc.cull = engine::rhi::CullMode::None;
    desc.blend = engine::rhi::BlendMode::Opaque;
    desc.color_format = engine::renderer::taa::kFormat;
    desc.debug_name = "taa";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_tonemap_aces_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
    desc.shader_resource_count = 1;
    desc.samplers[0] = engine::rhi::linear_clamp_sampler();
    desc.sampler_count = 1;
    desc.depth = engine::rhi::DepthTest::Disabled;
    desc.cull = engine::rhi::CullMode::None;
    desc.blend = engine::rhi::BlendMode::Opaque;
    desc.color_format = engine::rhi::Format::RGBA8_UNORM;
    desc.debug_name = "tonemap_aces";
    return desc;
}

bool mount_app_content(engine::assets::IAssetLoader& loader,
    const engine::ContentMountPaths& mounts) {
    if (mounts.layout == engine::ContentLayout::Unknown) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            "Failed to configure content mounts (unknown layout)");
        return false;
    }
    const bool ok = loader.mount("content", mounts.content)
        && loader.mount("shaders", mounts.shaders)
        && loader.mount("debug", mounts.debug);
    engine::log(ok ? engine::LogLevel::Info : engine::LogLevel::Error, engine::LogChannel::Assets,
        ok ? "Content mounts ready (/content, /shaders, /debug)"
           : "Failed to configure content mounts");
    return ok;
}

bool resolve_content(engine::assets::IAssetLoader& loader, std::string_view virtual_path,
    std::string& out_physical) {
    if (!loader.resolve_path(virtual_path, out_physical)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            std::string("Failed to resolve ") + std::string(virtual_path));
        return false;
    }
    return true;
}

bool compile_fullscreen_hlsl(engine::shaders::IShaderCompiler& compiler, const std::string& path,
    const char* fail_label, engine::shaders::ShaderBytecode& vs_out,
    engine::shaders::ShaderBytecode& ps_out) {
    engine::shaders::ShaderCompileDesc vs{};
    vs.file_path = path;
    vs.entry_point = "vs_main";
    vs.target_profile = "vs_6_0";
    engine::shaders::ShaderCompileDesc ps = vs;
    ps.entry_point = "ps_main";
    ps.target_profile = "ps_6_0";
    std::string error;
    if (!compiler.compile(vs, vs_out, error) || !compiler.compile(ps, ps_out, error)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, fail_label);
        return false;
    }
    return true;
}

bool build_fullscreen_pipeline(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& path, const char* name,
    MakePipelineDesc make_desc, std::unique_ptr<engine::rhi::IGraphicsPipeline>& out) {
    char label[96];
    std::snprintf(label, sizeof(label), "%s shader compile failed", name);

    engine::shaders::ShaderBytecode vs_bytecode;
    engine::shaders::ShaderBytecode ps_bytecode;
    if (!compile_fullscreen_hlsl(compiler, path, label, vs_bytecode, ps_bytecode)) {
        return false;
    }

    out = device.create_graphics_pipeline(make_desc(vs_bytecode.data, ps_bytecode.data));
    if (!out) {
        std::snprintf(label, sizeof(label), "%s pipeline creation failed", name);
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, label);
        return false;
    }
    return true;
}

// Reads a whole file. The gate asserts on what landed on disk, not on what it
// asked for, so it has to read it back.
std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

} // namespace sandbox
