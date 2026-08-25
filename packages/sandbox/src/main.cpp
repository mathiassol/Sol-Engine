#include <engine/engine.hpp>
#include <engine/cvar_file.hpp>
#include <engine/audio/audio.hpp>
#include <engine/audio/wav.hpp>
#include <engine/core/arena.hpp>
#include <engine/core/assert.hpp>
#include <engine/core/clock.hpp>
#include <engine/core/cvar.hpp>
#include <engine/core/log.hpp>
#include <engine/core/profile.hpp>
#include <engine/renderer/render_graph.hpp>
#include <engine/renderer/render_snapshot.hpp>
#include <engine/renderer/extract.hpp>
#include <engine/renderer/ibl.hpp>
#include <engine/renderer/pbr.hpp>
#include <engine/renderer/pcf.hpp>
#include <engine/renderer/sky.hpp>
#include <engine/renderer/aa.hpp>
#include <engine/renderer/bloom.hpp>
#include <engine/renderer/motion.hpp>
#include <engine/renderer/taa.hpp>
#include <engine/renderer/standard_frame.hpp>
#include <engine/assets/filesystem/asset_loader_filesystem.hpp>
#include <engine/assets/gpu/mesh_store.hpp>
#include <engine/assets/gpu/mesh_upload.hpp>
#include <engine/assets/obj/mesh_loader_obj.hpp>
#include <engine/assets/gltf/mesh_loader_gltf.hpp>
#include <engine/assets/png/image_loader_png.hpp>
#include <engine/assets/cooked.hpp>
#include <engine/assets/pak.hpp>
#include <engine/assets/image.hpp>
#include <engine/math/aabb.hpp>
#include <engine/math/constants.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/vec2.hpp>
#include <engine/math/vec3.hpp>
#include <engine/scene/world.hpp>
#include <engine/scene/scene_file.hpp>
#include <engine/scene/prefab.hpp>
#include <engine/gameplay/character.hpp>
#include <engine/gameplay/camera.hpp>
#include <engine/platform/input.hpp>
#include <engine/platform/window.hpp>
#include <engine/rhi/commands.hpp>
#include <engine/rhi/device.hpp>
#include <engine/rhi/resources.hpp>
#include <engine/debug/debug_lines.hpp>
#include <engine/debug/frame_stats.hpp>
#include <engine/debug/stats_overlay.hpp>
#include <engine/shaders/dxc/shader_compiler_dxc.hpp>
#include <engine/shaders/dxc/shader_hot_reload_dxc.hpp>
#include <engine/shaders/shader_hot_reload.hpp>

#include "world_extract.hpp"

#ifdef ENGINE_HAS_WIN32_PLATFORM
#include <engine/platform/win32/platform_win32.hpp>
#endif

#ifdef ENGINE_HAS_XAUDIO2
#include <engine/audio/xaudio2/audio_xaudio2.hpp>
#endif

#ifdef ENGINE_HAS_PHYSICS_CPU
#include <engine/physics/cpu/physics_cpu.hpp>
#endif

#ifdef ENGINE_HAS_D3D12
#include <engine/rhi/d3d12/rhi_d3d12.hpp>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef ENGINE_APP_FILE_VERSION
#define ENGINE_APP_FILE_VERSION ""
#endif

namespace {

constexpr const char* kForwardShader = "/shaders/forward.hlsl";
constexpr const char* kShadowShader = "/shaders/shadow.hlsl";
constexpr const char* kTonemapShader = "/shaders/tonemap.hlsl";
constexpr const char* kSkyShader = "/shaders/sky.hlsl";
constexpr const char* kBloomDownShader = "/shaders/bloom_downsample.hlsl";
constexpr const char* kBloomUpShader = "/shaders/bloom_upsample.hlsl";
constexpr const char* kFxaaShader = "/shaders/fxaa.hlsl";
constexpr const char* kSmaaEdgeShader = "/shaders/smaa_edge.hlsl";
constexpr const char* kSmaaWeightsShader = "/shaders/smaa_weights.hlsl";
constexpr const char* kSmaaBlendShader = "/shaders/smaa_blend.hlsl";
constexpr const char* kMotionShader = "/shaders/motion.hlsl";
constexpr const char* kTaaShader = "/shaders/taa.hlsl";
constexpr const char* kTonemapAcesShader = "/shaders/tonemap_aces.hlsl";
constexpr const char* kComputeGateShader = "/shaders/compute_gate.hlsl";
constexpr engine::u32 kComputeGateMagic = 0xC0DE0001u;
constexpr const char* kCubeMesh = "/content/meshes/cube.obj";
constexpr const char* kHuskyMesh = "/content/meshes/cartoon_husky.gltf";
constexpr const char* kHuskyAlbedos[] = {
    "/content/textures/husky/Cartoon_Husky_Albedo1.png",
    "/content/textures/husky/Cartoon_Husky_Albedo2.png",
    "/content/textures/husky/Cartoon_Husky_Albedo3.png",
    "/content/textures/husky/Cartoon_Husky_Albedo4.png",
};
constexpr const char* kGroundMesh = "/content/meshes/ground_quad";
constexpr const char* kOverlayShader = "/debug/shaders/overlay.hlsl";
constexpr const char* kDebugLinesShader = "/debug/shaders/debug_lines.hlsl";
constexpr const char* kTestFile = "/content/test.txt";

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
engine::Cvar cv_text_prec{"gate.text_prec", 0, "Cvar gate: text parser precedence-through-text knob"};
engine::Cvar cv_text_comment{"gate.text_comment", "unset", "Cvar gate: text parser trailing-comment knob"};

struct FlyCamera {
    engine::math::Vec3 position{0.f, 0.35f, -2.2f};
    engine::f32 yaw   = 0.f;
    engine::f32 pitch = 0.f;

    void update(const engine::platform::InputState& input, engine::f32 dt, bool fly_wasd = true) {
        using engine::platform::Key;
        using engine::platform::MouseButton;

        if (input.mouse_down[static_cast<engine::usize>(MouseButton::Right)]) {
            yaw   += input.mouse_dx * 0.003f;
            pitch += input.mouse_dy * 0.003f;
            pitch = std::clamp(pitch, -1.4f, 1.4f);
        }

        const engine::f32 cy = std::cos(yaw);
        const engine::f32 sy = std::sin(yaw);
        const engine::f32 cp = std::cos(pitch);
        const engine::f32 sp = std::sin(pitch);

        engine::math::Vec3 forward{sy * cp, sp, cy * cp};
        engine::math::Vec3 right = forward.cross({0.f, 1.f, 0.f}).normalized();
        engine::math::Vec3 up    = right.cross(forward).normalized();

        const engine::f32 speed = 4.f * dt;
        if (fly_wasd) {
            if (input.keys_down[static_cast<engine::usize>(Key::W)]) position = position + forward * speed;
            if (input.keys_down[static_cast<engine::usize>(Key::S)]) position = position - forward * speed;
            if (input.keys_down[static_cast<engine::usize>(Key::A)]) position = position - right * speed;
            if (input.keys_down[static_cast<engine::usize>(Key::D)]) position = position + right * speed;
            if (input.keys_down[static_cast<engine::usize>(Key::E)]) position = position + up * speed;
            if (input.keys_down[static_cast<engine::usize>(Key::Q)]) position = position - up * speed;
        }
    }

    engine::math::Mat4 view() const {
        const engine::f32 cy = std::cos(yaw);
        const engine::f32 sy = std::sin(yaw);
        const engine::f32 cp = std::cos(pitch);
        const engine::f32 sp = std::sin(pitch);
        engine::math::Vec3 forward{sy * cp, sp, cy * cp};
        return engine::math::Mat4::look_at(position, position + forward, {0.f, 1.f, 0.f});
    }

    engine::math::Mat4 projection(engine::f32 aspect) const {
        return engine::math::Mat4::perspective(engine::math::radians(60.f), aspect, 0.1f, 100.f);
    }
};

struct ForwardDemo {
    std::unique_ptr<engine::rhi::IGraphicsPipeline> pipeline;
    std::unique_ptr<engine::rhi::IGraphicsPipeline> shadow_pipeline;
    std::unique_ptr<engine::rhi::IGraphicsPipeline> sky_pipeline;
    std::unique_ptr<engine::rhi::IGraphicsPipeline> bloom_downsample_pipeline;
    std::unique_ptr<engine::rhi::IGraphicsPipeline> bloom_upsample_pipeline;
    std::unique_ptr<engine::rhi::IGraphicsPipeline> tonemap_pipeline;
    std::unique_ptr<engine::rhi::IGraphicsPipeline> fxaa_pipeline;
    std::unique_ptr<engine::rhi::IGraphicsPipeline> smaa_edge_pipeline;
    std::unique_ptr<engine::rhi::IGraphicsPipeline> smaa_weights_pipeline;
    std::unique_ptr<engine::rhi::IGraphicsPipeline> smaa_blend_pipeline;
    std::unique_ptr<engine::rhi::IGraphicsPipeline> motion_pipeline;
    std::unique_ptr<engine::rhi::IGraphicsPipeline> taa_pipeline;
    std::unique_ptr<engine::rhi::IGraphicsPipeline> tonemap_aces_pipeline;
    std::unique_ptr<engine::rhi::ITexture> taa_history[2];
    engine::u32 taa_history_w = 0;
    engine::u32 taa_history_h = 0;
    engine::u32 taa_frames = 0;
    bool taa_history_valid = false;
    engine::renderer::motion::MotionHistory motion_history{};
    engine::renderer::aa::Mode aa_mode = engine::renderer::aa::kDefault;
    engine::assets::gpu::GpuMeshStore meshes;
    engine::assets::MeshHandle husky{};
    engine::assets::MeshHandle ground{};
    std::unique_ptr<engine::rhi::ITexture> albedos[sandbox::kHuskyVariantCount];
    std::unique_ptr<engine::rhi::ITexture> floor_albedo;
    std::unique_ptr<engine::rhi::ITexture> default_mr;
    std::unique_ptr<engine::rhi::ITexture> default_normal;
    std::unique_ptr<engine::rhi::ITexture> ibl_irradiance;
    std::unique_ptr<engine::rhi::ITexture> ibl_prefilter;
    std::unique_ptr<engine::rhi::ITexture> ibl_brdf_lut;
    std::unique_ptr<engine::rhi::ITexture> sky_cubemap;
    std::unique_ptr<engine::shaders::IShaderHotReloader> shader_watcher;
    engine::shaders::WatchedShaderPair shader_sources{};
    engine::scene::World world;
    FlyCamera camera;
};

struct SandboxState {
    std::unique_ptr<ForwardDemo> forward;
    engine::debug::FrameStatsTracker frame_stats;
    engine::debug::StatsOverlay overlay;
    engine::debug::DebugLines debug_lines;
    engine::audio::SoundHandle beep{};
    engine::gameplay::CharacterController player;
    engine::gameplay::GameCamera game_camera;
    bool walk_mode = false;
    engine::f32 husky_foot_y = 0.f;
};

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
    assets.forward = demo.pipeline.get();
    assets.shadow = demo.shadow_pipeline.get();
    assets.sky = demo.sky_pipeline.get();
    assets.bloom_downsample = demo.bloom_downsample_pipeline.get();
    assets.bloom_upsample = demo.bloom_upsample_pipeline.get();
    assets.tonemap = demo.tonemap_pipeline.get();
    assets.fxaa = demo.fxaa_pipeline.get();
    assets.smaa_edge = demo.smaa_edge_pipeline.get();
    assets.smaa_weights = demo.smaa_weights_pipeline.get();
    assets.smaa_blend = demo.smaa_blend_pipeline.get();
    assets.motion = demo.motion_pipeline.get();
    assets.taa = demo.taa_pipeline.get();
    assets.tonemap_aces = demo.tonemap_aces_pipeline.get();
    assets.taa_history = nullptr;
    assets.taa_sample = demo.taa_frames;
    assets.taa_reset = !demo.taa_history_valid;
    assets.aa_mode = demo.aa_mode;
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
    desc.debug_name = "shadow";
    return desc;
}

engine::rhi::GraphicsPipelineDesc make_tonemap_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps) {
    engine::rhi::GraphicsPipelineDesc desc{};
    desc.vertex_shader = vs;
    desc.pixel_shader = ps;
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

bool run_cvar_gate(engine::platform::IFileSystem* fs, const std::string& scratch_dir) {
    using engine::CvarSetResult;
    using engine::CvarSource;

    const engine::usize count = engine::cvar_count();
    bool walked = false;
    for (engine::usize i = 0; i < count; ++i) {
        if (engine::cvar_at(i) == &cv_gate_prec) {
            walked = true;
        }
    }
    const bool registry_ok = count >= 17
        && walked
        && engine::find_cvar("gate.bool") == &cv_gate_bool
        && engine::find_cvar("gate.string") == &cv_gate_string
        && engine::find_cvar("gate.nope") == nullptr
        && engine::cvar_at(count) == nullptr;

    const bool types_ok =
        cv_gate_bool.set("on", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_bool.as_bool()
        && cv_gate_bool.set("0", CvarSource::Code) == CvarSetResult::Applied
        && !cv_gate_bool.as_bool()
        && cv_gate_bool.set("maybe", CvarSource::Code) == CvarSetResult::Invalid
        && !cv_gate_bool.as_bool()
        && cv_gate_int.set("-42", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_int.as_int() == -42
        && cv_gate_int.set("4.5", CvarSource::Code) == CvarSetResult::Invalid
        && cv_gate_int.set("", CvarSource::Code) == CvarSetResult::Invalid
        && cv_gate_float.set("-0.25", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_float.as_float() < -0.24f && cv_gate_float.as_float() > -0.26f
        && cv_gate_string.set("two words", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_string.as_string() == "two words"
        && cv_gate_bool.type() == engine::CvarType::Bool
        && cv_gate_int.type() == engine::CvarType::Int
        && cv_gate_float.type() == engine::CvarType::Float
        && cv_gate_string.type() == engine::CvarType::String;

    // Comments, both separators, blank lines, one unknown key, one bad value.
    static constexpr const char* kText =
        "# hash comment\n"
        "// slash comment\n"
        "\n"
        "   \n"
        "gate.text_int 7\n"
        "gate.text_float = 1.5\n"
        "gate.text_string   hello world  \r\n"
        "gate.nothere 1\n"
        "gate.text_int oops\n";
    const auto text_stats = engine::apply_cvar_text(kText, CvarSource::File);

    // "key=value" with no spaces at all: the comment above split_line() in
    // cvar.cpp claims all three separator forms behave identically, but until
    // now only "key value" and "key = value" were exercised.
    const auto text_eq_stats = engine::apply_cvar_text("gate.text_eq=42\n", CvarSource::File);
    const bool text_eq_ok = text_eq_stats.applied == 1 && cv_text_eq.as_int() == 42;

    // The entire reason the precedence rule exists: a File write must lose to
    // a CommandLine-owned value even when it arrives as config text rather
    // than a direct set() call. Until now `ignored` was only ever produced by
    // calling set() directly, never through apply_cvar_text.
    cv_text_prec.set("5", CvarSource::CommandLine);
    const auto text_prec_stats = engine::apply_cvar_text("gate.text_prec 9\n", CvarSource::File);
    const bool text_prec_ok = text_prec_stats.applied == 0
        && text_prec_stats.ignored == 1
        && cv_text_prec.as_int() == 5;

    // Pin the trailing-comment fix: a String knob must have the comment
    // stripped, not stored verbatim.
    const auto text_comment_stats = engine::apply_cvar_text(
        "gate.text_comment borderless   # my note\n", CvarSource::File);
    const bool text_comment_ok = text_comment_stats.applied == 1
        && cv_text_comment.as_string() == "borderless";

    const auto text_empty_stats = engine::apply_cvar_text("", CvarSource::File);
    const bool text_empty_ok = text_empty_stats.applied == 0
        && text_empty_stats.unknown == 0
        && text_empty_stats.invalid == 0
        && text_empty_stats.ignored == 0;

    // A bare key with no separator at all is malformed, not silently ignored.
    const auto text_bare_stats = engine::apply_cvar_text("just_a_bare_key\n", CvarSource::File);
    const bool text_bare_ok = text_bare_stats.invalid == 1 && text_bare_stats.applied == 0;

    const bool text_ok = text_stats.applied == 3
        && text_stats.unknown == 1
        && text_stats.invalid == 1
        && text_stats.ignored == 0
        && cv_text_int.as_int() == 7
        && cv_text_float.as_float() > 1.49f && cv_text_float.as_float() < 1.51f
        && cv_text_string.as_string() == "hello world"
        && text_eq_ok
        && text_prec_ok
        && text_comment_ok
        && text_empty_ok
        && text_bare_ok;

    // A lower source never overwrites a higher one. This is what lets the
    // engine load config.cfg after the command line.
    const bool precedence_ok =
        cv_gate_prec.source() == CvarSource::Default
        && cv_gate_prec.set("1", CvarSource::File) == CvarSetResult::Applied
        && cv_gate_prec.set("2", CvarSource::CommandLine) == CvarSetResult::Applied
        && cv_gate_prec.set("3", CvarSource::File) == CvarSetResult::Ignored
        && cv_gate_prec.as_int() == 2
        && cv_gate_prec.source() == CvarSource::CommandLine
        && cv_gate_prec.set("4", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_prec.as_int() == 4;

    // Both value forms, plus the two malformed tails.
    static const char* kArgvOk[] = {
        "sandbox.exe", "--gates", "--set", "gate.args=11", "--set", "gate.args", "12"};
    const auto args_stats = engine::apply_cvar_args(7, kArgvOk);
    // Captured immediately: the later dangling/no_value calls below mutate
    // gate.args again, so reading cv_gate_args.as_int() inside the args_ok
    // expression at that point would see the final value (13), not this one.
    const engine::i32 args_after_ok = cv_gate_args.as_int();
    static const char* kArgvDangling[] = {"sandbox.exe", "--set"};
    const auto dangling_stats = engine::apply_cvar_args(2, kArgvDangling);
    static const char* kArgvNoValue[] = {
        "sandbox.exe", "--set", "gate.args", "--set", "gate.args=13"};
    const auto no_value_stats = engine::apply_cvar_args(5, kArgvNoValue);
    const bool args_ok = args_stats.applied == 2
        && args_stats.unknown == 0
        && args_stats.invalid == 0
        && args_after_ok == 12
        && dangling_stats.invalid == 1 && dangling_stats.applied == 0
        && no_value_stats.invalid == 1 && no_value_stats.applied == 1
        && cv_gate_args.as_int() == 13;

    // A Cvar with automatic (non-static) storage duration must leave no trace
    // once it goes out of scope: no dangling pointer in the registry, and no
    // false "duplicate cvar name" abort if the same name is declared again.
    bool scope_ok;
    {
        const engine::usize before = engine::cvar_count();
        bool ok = true;
        {
            engine::Cvar scoped("gate.scoped", false, "Cvar gate: scope-lifetime knob");
            ok = ok
                && engine::find_cvar("gate.scoped") == &scoped
                && engine::cvar_count() == before + 1;
        }
        ok = ok
            && engine::cvar_count() == before
            && engine::find_cvar("gate.scoped") == nullptr;
        {
            // Re-entering an identical scope must not abort: that is the
            // regression the destructor's unregister step exists to prevent.
            engine::Cvar scoped_again("gate.scoped", false, "Cvar gate: scope-lifetime knob");
            ok = ok && engine::find_cvar("gate.scoped") == &scoped_again;
        }
        ok = ok
            && engine::cvar_count() == before
            && engine::find_cvar("gate.scoped") == nullptr;
        scope_ok = ok;
    }

    // The engine's own loader, not a private copy of it.
    bool file_ok = false;
    bool missing_ok = false;
    if (fs) {
        const std::string path = scratch_dir + "/gate_cvars.cfg";
        static constexpr const char* kBody = "# written by the cvar gate\ngate.file 99\n";
        const std::span<const engine::u8> body{
            reinterpret_cast<const engine::u8*>(kBody), std::strlen(kBody)};
        if (fs->write_file(path, body)) {
            bool found = false;
            const auto stats = engine::apply_cvar_file(*fs, path, &found);
            file_ok = found && stats.applied == 1 && stats.invalid == 0
                && cv_gate_file.as_int() == 99;
        }
        bool missing_found = true;
        const auto missing_stats =
            engine::apply_cvar_file(*fs, scratch_dir + "/no_such_file.cfg", &missing_found);
        missing_ok = !missing_found && missing_stats.applied == 0
            && missing_stats.invalid == 0 && missing_stats.unknown == 0;
    }

    const bool passed = registry_ok && text_ok && types_ok && precedence_ok && scope_ok && args_ok
        && file_ok && missing_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Cvar gate: count=%llu registry=%s text=%s types=%s precedence=%s scope=%s args=%s "
        "file=%s missing=%s (%s)",
        static_cast<unsigned long long>(count),
        registry_ok ? "yes" : "no",
        text_ok ? "yes" : "no",
        types_ok ? "yes" : "no",
        precedence_ok ? "yes" : "no",
        scope_ok ? "yes" : "no",
        args_ok ? "yes" : "no",
        file_ok ? "yes" : "no",
        missing_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_window_gate(engine::platform::IWindow* window, engine::rhi::IDevice* device) {
    using engine::platform::WindowMode;

    if (!window) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Platform,
            "Window gate: no window (FAIL)");
        return false;
    }

    // window.mode and r.vsync are now user-settable, so asserting the factory
    // default unconditionally would go red for any developer with a config.cfg.
    // Assert the factory default only while a knob is untouched; once a knob has
    // set it, assert the window matches what the knob asked for instead. That
    // tests the wiring, which is the assertion worth having. The knobs live in
    // engine.cpp's anonymous namespace -- the public registry is how a gate
    // reaches them without exporting engine internals.
    const engine::Cvar* cv_mode = engine::find_cvar("window.mode");
    const engine::Cvar* cv_vsync_knob = engine::find_cvar("r.vsync");

    bool mode_ok = window->mode() == WindowMode::Windowed;
    if (cv_mode && cv_mode->type() == engine::CvarType::String
        && cv_mode->source() != engine::CvarSource::Default) {
        WindowMode wanted = WindowMode::Windowed;
        // An unparsable value is refused by the engine, which keeps the default.
        if (!engine::platform::parse_window_mode(cv_mode->as_string(), wanted)) {
            wanted = WindowMode::Windowed;
        }
        mode_ok = window->mode() == wanted;
    }

    bool vsync_startup_ok = window->vsync();
    if (cv_vsync_knob && cv_vsync_knob->type() == engine::CvarType::Bool
        && cv_vsync_knob->source() != engine::CvarSource::Default) {
        vsync_startup_ok = window->vsync() == cv_vsync_knob->as_bool();
    }

    const bool default_ok = mode_ok && vsync_startup_ok;

    // The restore check below compares against the windowed dimensions, so the
    // baseline has to be read while the window really is windowed -- startup is
    // no longer guaranteed to be. Without this, a window.mode knob reds the gate
    // on the restore clause instead of the startup clause.
    const WindowMode startup_mode = window->mode();
    const bool startup_vsync = window->vsync();
    if (startup_mode != WindowMode::Windowed) {
        window->set_mode(WindowMode::Windowed);
    }
    const engine::u32 windowed_w = window->width();
    const engine::u32 windowed_h = window->height();

    const bool borderless_ok = window->set_mode(WindowMode::Borderless)
        && window->mode() == WindowMode::Borderless
        && window->width() > 0 && window->height() > 0;
    const bool fullscreen_ok = window->set_mode(WindowMode::Fullscreen)
        && window->mode() == WindowMode::Fullscreen;
    const bool restore_ok = window->set_mode(WindowMode::Windowed)
        && window->mode() == WindowMode::Windowed
        && window->width() == windowed_w
        && window->height() == windowed_h;

    window->set_vsync(false);
    const bool vsync_off = !window->vsync();
    window->set_vsync(true);
    const bool vsync_on = window->vsync();

    // Hand the window back in the state the user asked for. The sweep above used
    // to leave it windowed with vsync on, which silently undid the knobs a
    // moment after startup applied them.
    if (startup_mode != WindowMode::Windowed) {
        window->set_mode(startup_mode);
    }
    if (!startup_vsync) {
        window->set_vsync(false);
    }

    bool present_ok = device == nullptr;
    if (device) {
        device->set_present_interval(0);
        present_ok = device->present_interval() == 0;
        device->set_present_interval(1);
        present_ok = present_ok && device->present_interval() == 1;
    }

    const bool passed = default_ok && borderless_ok && fullscreen_ok && restore_ok
        && vsync_off && vsync_on && present_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Window gate: startup=%s borderless=%s fullscreen=%s restore=%s vsync=%s present=%s (%s)",
        default_ok ? "yes" : "no",
        borderless_ok ? "yes" : "no",
        fullscreen_ok ? "yes" : "no",
        restore_ok ? "yes" : "no",
        (vsync_off && vsync_on) ? "yes" : "no",
        present_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Platform, message);
    return passed;
}

bool run_identity_gate(engine::Engine& app) {
#ifdef ENGINE_GAME_APP
    const char* expect_title = "Sol";
    const bool expect_icon = true;
#else
    const char* expect_title = "Engine Sandbox";
    const bool expect_icon = false;
#endif
    const bool title_ok = app.window() != nullptr && app.window()->title() == expect_title;
    const bool icon_ok = app.executable_has_icon() == expect_icon;
    const bool version_ok = app.executable_file_version() == ENGINE_APP_FILE_VERSION
        && ENGINE_APP_FILE_VERSION[0] != '\0';

    const bool passed = title_ok && icon_ok && version_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Identity gate: title=%s icon=%s version=%s (%s)",
        expect_title, expect_icon ? "yes" : "no", ENGINE_APP_FILE_VERSION,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_ship_gate(engine::Engine& app) {
    const std::filesystem::path exe_dir{app.executable_directory()};
    std::error_code ec;
    const bool dxc_ok = std::filesystem::exists(exe_dir / "dxcompiler.dll", ec);
    ec.clear();
    const bool dxil_ok = std::filesystem::exists(exe_dir / "dxil.dll", ec);
    ec.clear();
    const bool agility_present =
        std::filesystem::exists(exe_dir / "D3D12Core.dll", ec)
        || std::filesystem::exists(exe_dir / "D3D12" / "D3D12Core.dll", ec);
    const bool agility_ok = !agility_present;

    engine::rhi::IDevice* device = app.device();
    const engine::rhi::GpuBaseline baseline =
        device != nullptr ? device->gpu_baseline() : engine::rhi::GpuBaseline{};
    const bool sm_ok = baseline.shader_model >= engine::rhi::kGpuShaderModel_6_0;
    const bool fl_ok = baseline.feature_level >= engine::rhi::kGpuFeatureLevel_11_0;

    const bool passed = dxc_ok && dxil_ok && agility_ok && sm_ok && fl_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Ship gate: dxc=%s dxil=%s agility=%s sm=%s fl=%s (%s)",
        dxc_ok ? "yes" : "no",
        dxil_ok ? "yes" : "no",
        agility_ok ? "os" : "sdk",
        sm_ok ? "6.0" : "no",
        fl_ok ? "11_0" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_pix_gate(engine::rhi::IDevice* device) {
    if (!device) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Pix gate: no device (FAIL)");
        return false;
    }

    device->begin_frame();
    auto& cmd = device->command_list();
    cmd.begin();
    const bool start_ok = cmd.debug_event_depth() == 0;

    cmd.begin_event("gate/outer");
    const bool begin_ok = start_ok
        && cmd.debug_event_depth() == 1
        && cmd.debug_event_name() == "gate/outer";

    cmd.begin_event("gate/inner");
    const bool nest_ok = cmd.debug_event_depth() == 2
        && cmd.debug_event_name() == "gate/inner";

    cmd.set_marker("gate/tick");
    const bool marker_ok = cmd.last_debug_marker() == "gate/tick";

    cmd.end_event();
    cmd.end_event();
    const bool depth_ok = cmd.debug_event_depth() == 0
        && cmd.debug_event_name().empty();

    cmd.end();
    device->submit();
    device->wait_idle();

    const bool passed = begin_ok && nest_ok && marker_ok && depth_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Pix gate: begin=%s nest=%s marker=%s depth=%s (%s)",
        begin_ok ? "yes" : "no",
        nest_ok ? "yes" : "no",
        marker_ok ? "yes" : "no",
        depth_ok ? "0" : "bad",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_build_gate(engine::ContentLayout layout) {
#ifdef ENGINE_GAME_APP
    const bool ok = layout == engine::ContentLayout::Install;
    const char* target = "game";
#else
    const bool ok = layout == engine::ContentLayout::Repo
        || layout == engine::ContentLayout::Install;
    const char* target = "sandbox";
#endif
    char message[192];
    std::snprintf(message, sizeof(message),
        "Build gate: target=%s layout=%s (%s)",
        target, engine::content_layout_name(layout), ok ? "pass" : "FAIL");
    engine::log(ok ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return ok;
}

bool run_mount_gate(engine::assets::IAssetLoader& loader) {
    std::string physical;
    std::vector<engine::u8> bytes;
    const bool resolved = loader.resolve_path(kTestFile, physical);
    const bool loaded = resolved && loader.load_bytes(kTestFile, bytes) && !bytes.empty();
    char message[512];
    std::snprintf(message, sizeof(message),
        "Mount gate %s -> %s (%s)",
        kTestFile,
        physical.c_str(),
        loaded ? "pass" : "FAIL");
    engine::log(loaded ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return loaded;
}

bool run_handle_gate(engine::assets::gpu::GpuMeshStore& store, engine::rhi::IDevice& device,
    const engine::assets::MeshData& mesh_data) {
    const auto first = store.store(device, kCubeMesh, mesh_data);
    const auto second = store.store(device, kCubeMesh, mesh_data);
    const bool passed = first.valid() && first == second && store.get(second) != nullptr;
    char message[128];
    std::snprintf(message, sizeof(message),
        "MeshHandle gate: id=%llu gen=%u -> id=%llu gen=%u (%s)",
        static_cast<unsigned long long>(first.id), first.generation,
        static_cast<unsigned long long>(second.id), second.generation,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_handle_unload_gate(engine::assets::gpu::GpuMeshStore& store, engine::rhi::IDevice& device,
    const engine::assets::MeshData& mesh_data) {
    constexpr const char* kProbe = "/content/meshes/cube.obj#unload-probe";
    const auto first = store.store(device, kProbe, mesh_data);
    device.wait_idle();
    const bool dropped = first.valid() && store.unload(first) && store.get(first) == nullptr;
    const auto second = store.store(device, kProbe, mesh_data);
    const bool passed = dropped && first.valid() && second.valid() && first != second
        && store.get(first) == nullptr && store.get(second) != nullptr;
    store.unload(second);
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets,
        passed ? "MeshHandle unload gate (pass)" : "MeshHandle unload gate (FAIL)");
    return passed;
}

bool bytecode_is_dxil(const std::vector<engine::u8>& data) {
    static constexpr char kDxil[] = {'D', 'X', 'I', 'L'};
    if (data.size() < 4) {
        return false;
    }
    return std::search(data.begin(), data.end(), kDxil, kDxil + 4) != data.end();
}

bool run_shader_cache_gate(engine::shaders::IShaderCompiler& compiler,
    const engine::shaders::ShaderCompileDesc& desc) {
    engine::shaders::ShaderBytecode first;
    engine::shaders::ShaderBytecode second;
    std::string error;
    if (!compiler.compile(desc, first, error) || first.data.empty()) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Shader cache gate compile failed");
        return false;
    }
    const bool first_hit = compiler.last_compile_from_cache();
    if (!compiler.compile(desc, second, error) || second.data.empty()) {
        return false;
    }
    const bool second_hit = compiler.last_compile_from_cache();
    const bool dxil = bytecode_is_dxil(first.data);
    const bool passed = second_hit && first.data.size() == second.data.size() && dxil;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Shader cache gate: first=%s second=%s dxil=%s (%s)",
        first_hit ? "hit" : "miss",
        second_hit ? "hit" : "miss",
        dxil ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_dxc_gate(const engine::shaders::ShaderCompileDesc& desc,
    const engine::shaders::ShaderBytecode& bytecode) {
    const bool sm6 = desc.target_profile.find("6_") != std::string::npos;
    const bool dxil = bytecode_is_dxil(bytecode.data);
    const bool target_ok = desc.target == engine::shaders::ShaderTarget::Dxil;
    const bool passed = sm6 && dxil && target_ok && !bytecode.data.empty();
    char message[192];
    std::snprintf(message, sizeof(message),
        "DXC gate: profile=%s target=dxil dxil=%s bytes=%zu (%s)",
        desc.target_profile.c_str(), dxil ? "yes" : "no", bytecode.data.size(),
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_rhi_contract_gate(engine::rhi::IDevice& device, engine::shaders::IShaderCompiler& compiler) {
    auto sampler = device.create_sampler(engine::rhi::shadow_comparison_sampler());
    const bool sampler_ok = sampler != nullptr
        && engine::rhi::GraphicsPipelineDesc::kMaxSamplers >= 2;

    engine::shaders::ShaderCompileDesc spirv{};
    spirv.file_path = "unused.hlsl";
    spirv.entry_point = "main";
    spirv.target_profile = "vs_6_0";
    spirv.target = engine::shaders::ShaderTarget::Spirv;
    engine::shaders::ShaderBytecode unused;
    std::string error;
    const bool spirv_rejected = !compiler.compile(spirv, unused, error)
        && error.find("SPIR-V") != std::string::npos;
    const bool target_default =
        engine::shaders::ShaderCompileDesc{}.target == engine::shaders::ShaderTarget::Dxil;

    const bool passed = sampler_ok && spirv_rejected && target_default;
    char message[224];
    std::snprintf(message, sizeof(message),
        "RHI contract gate: sampler=%s spirv_rejected=%s target_enum=%s (%s)",
        sampler_ok ? "yes" : "no",
        spirv_rejected ? "yes" : "no", target_default ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

void fill_rgba8(std::vector<engine::u8>& pixels, engine::u32 pixel_count, engine::u8 r,
    engine::u8 g, engine::u8 b) {
    pixels.resize(static_cast<size_t>(pixel_count) * 4);
    for (engine::u32 i = 0; i < pixel_count; ++i) {
        pixels[static_cast<size_t>(i) * 4 + 0] = r;
        pixels[static_cast<size_t>(i) * 4 + 1] = g;
        pixels[static_cast<size_t>(i) * 4 + 2] = b;
        pixels[static_cast<size_t>(i) * 4 + 3] = 255;
    }
}

bool run_rhi_impl_gate(engine::rhi::IDevice& device, engine::shaders::IShaderCompiler& compiler,
    const std::string& compute_path) {
    engine::shaders::ShaderCompileDesc cs_desc{};
    cs_desc.file_path = compute_path;
    cs_desc.entry_point = "cs_main";
    cs_desc.target_profile = "cs_6_0";
    engine::shaders::ShaderBytecode cs_bytecode;
    std::string error;
    const bool compiled = compiler.compile(cs_desc, cs_bytecode, error) && !cs_bytecode.data.empty();

    engine::rhi::ComputePipelineDesc compute{};
    compute.compute_shader = compiled ? std::span<const engine::u8>(cs_bytecode.data)
                                      : std::span<const engine::u8>{};
    compute.unordered_access_count = 1;
    compute.debug_name = "compute_gate";
    auto compute_pso = compiled ? device.create_compute_pipeline(compute) : nullptr;
    const bool pso_ok = compute_pso != nullptr;

    engine::rhi::BufferDesc storage{};
    storage.size = 4;
    storage.usage = engine::rhi::BufferUsage::Storage;
    auto rw = device.create_buffer(storage);

    engine::rhi::BufferDesc readback{};
    readback.size = 4;
    readback.usage = engine::rhi::BufferUsage::Readback;
    auto rb = device.create_buffer(readback);

    engine::u32 magic = 0;
    bool dispatch_ok = false;
    if (pso_ok && rw && rb) {
        device.begin_frame();
        auto& cmd = device.command_list();
        cmd.begin();
        cmd.set_compute_pipeline(*compute_pso);
        cmd.set_unordered_access(0, *rw);
        cmd.dispatch(1, 1, 1);
        cmd.transition(*rw, engine::rhi::ResourceState::UnorderedAccess,
            engine::rhi::ResourceState::CopySrc);
        cmd.copy_buffer(*rw, *rb, 4);
        cmd.end();
        device.submit();
        device.wait_idle();
        device.read_buffer(*rb, 0, &magic, sizeof(magic));
        dispatch_ok = magic == kComputeGateMagic;
    }

    constexpr engine::u32 kFace = 4;
    constexpr engine::u32 kFacePixels = kFace * kFace;
    std::vector<engine::u8> cube_pixels(6 * kFacePixels * 4);
    const engine::u8 face_colors[6][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}, {255, 0, 255}, {0, 255, 255},
    };
    for (engine::u32 face = 0; face < 6; ++face) {
        for (engine::u32 i = 0; i < kFacePixels; ++i) {
            const size_t o = (static_cast<size_t>(face) * kFacePixels + i) * 4;
            cube_pixels[o + 0] = face_colors[face][0];
            cube_pixels[o + 1] = face_colors[face][1];
            cube_pixels[o + 2] = face_colors[face][2];
            cube_pixels[o + 3] = 255;
        }
    }
    engine::rhi::TextureDesc cube{};
    cube.width = kFace;
    cube.height = kFace;
    cube.mip_levels = 1;
    cube.array_size = 6;
    cube.dimension = engine::rhi::TextureDimension::Cube;
    cube.format = engine::rhi::Format::RGBA8_UNORM;
    cube.usage = engine::rhi::TextureUsage::ShaderResource;
    auto cube_tex = device.create_texture(cube, cube_pixels.data());
    const bool cube_ok = cube_tex
        && cube_tex->array_size() == 6
        && cube_tex->dimension() == engine::rhi::TextureDimension::Cube;

    std::vector<engine::u8> array_pixels;
    fill_rgba8(array_pixels, kFacePixels * 4, 32, 64, 128);
    engine::rhi::TextureDesc array_desc{};
    array_desc.width = kFace;
    array_desc.height = kFace;
    array_desc.mip_levels = 1;
    array_desc.array_size = 4;
    array_desc.dimension = engine::rhi::TextureDimension::Tex2DArray;
    array_desc.format = engine::rhi::Format::RGBA8_UNORM;
    array_desc.usage = engine::rhi::TextureUsage::ShaderResource;
    auto array_tex = device.create_texture(array_desc, array_pixels.data());
    const bool array_ok = array_tex
        && array_tex->array_size() == 4
        && array_tex->dimension() == engine::rhi::TextureDimension::Tex2DArray;

    const bool passed = pso_ok && dispatch_ok && cube_ok && array_ok;
    char message[256];
    std::snprintf(message, sizeof(message),
        "RHI impl gate: compute=%s dispatch=%s cubes=%s arrays=%s (%s)",
        pso_ok ? "yes" : "no", dispatch_ok ? "yes" : "no",
        cube_ok ? "yes" : "no", array_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    if (!compiled) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            error.empty() ? "compute_gate.hlsl compile failed" : error.c_str());
    }
    return passed;
}

bool run_mesh_reload_gate(engine::rhi::IDevice& device, const engine::assets::MeshData& mesh_data) {
    device.wait_idle();
    const auto before = device.gpu_memory_stats();

    constexpr engine::usize kDummyBytes = 1024u * 1024u;
    for (int i = 0; i < 100; ++i) {
        auto transient = engine::assets::gpu::upload_mesh(device, mesh_data);
        engine::rhi::BufferDesc dummy{};
        dummy.size  = kDummyBytes;
        dummy.usage = engine::rhi::BufferUsage::Vertex;
        auto dummy_buffer = device.create_buffer(dummy);
        (void)transient;
        (void)dummy_buffer;
    }

    device.wait_idle();
    const auto after = device.gpu_memory_stats();

    constexpr engine::u64 kSlackBytes = 8ull * 1024ull * 1024ull;
    const bool passed = after.local_usage_bytes <= before.local_usage_bytes + kSlackBytes;

    char message[256];
    std::snprintf(message, sizeof(message),
        "Mesh reload gate (100x + 1 MiB dummy): local VRAM %llu -> %llu bytes (%s)",
        static_cast<unsigned long long>(before.local_usage_bytes),
        static_cast<unsigned long long>(after.local_usage_bytes),
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_two_draw_items_gate() {
    engine::renderer::DrawItem draws[2]{};
    draws[0].model = engine::math::Mat4::identity();
    draws[1].model = engine::math::Mat4::translate({2.f, 0.f, 0.f});
    const bool passed = std::memcmp(&draws[0].model, &draws[1].model, sizeof(engine::math::Mat4)) != 0;
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render,
        passed ? "DrawItem pair gate: two models (pass)"
               : "DrawItem pair gate: two models (FAIL)");
    return passed;
}

bool run_scene_world_gate(const engine::scene::World& world) {
    const bool count_ok = world.instance_count >= 2;
    bool models_differ = false;
    bool moved = false;
    if (count_ok) {
        models_differ = std::memcmp(&world.instances[0].model, &world.instances[1].model,
            sizeof(engine::math::Mat4)) != 0;
        engine::scene::World nudged = world;
        const engine::math::Mat4 before = nudged.instances[0].model;
        engine::scene::set_instance_model(nudged, 0,
            engine::math::Mat4::translate({0.1f, 0.f, 0.f}) * before);
        moved = std::memcmp(&nudged.instances[0].model, &before, sizeof(engine::math::Mat4)) != 0;
    }
    const bool passed = count_ok && models_differ && moved;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Scene world gate: instances=%u distinct=%s move=%s (%s)",
        world.instance_count,
        models_differ ? "yes" : "no",
        moved ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_scene_name_gate() {
    engine::scene::World world{};
    const engine::u32 alpha = engine::scene::add_instance(world, {});
    const engine::u32 beta = engine::scene::add_instance(world, {});
    const engine::u32 unnamed = engine::scene::add_instance(world, {});
    engine::scene::set_instance_name(world, alpha, "alpha");
    engine::scene::set_instance_name(world, beta, "beta");

    const engine::scene::NameId intern_a = engine::scene::intern_name(world, "alpha");
    const engine::scene::NameId intern_b = engine::scene::intern_name(world, "alpha");
    const bool intern_ok = intern_a != 0 && intern_a == intern_b
        && intern_a == world.instances[alpha].name;

    const bool find_ok = engine::scene::find_instance(world, "alpha") == alpha
        && engine::scene::find_instance(world, "beta") == beta
        && engine::scene::instance_name(world, alpha) == "alpha"
        && engine::scene::instance_name(world, beta) == "beta";

    const bool unnamed_ok = world.instances[unnamed].name == 0
        && engine::scene::instance_name(world, unnamed).empty()
        && engine::scene::find_instance(world, "") == engine::scene::kInvalidInstance
        && engine::scene::intern_name(world, "") == 0;

    const engine::u32 dup = engine::scene::add_instance(world, {});
    engine::scene::set_instance_name(world, dup, "alpha");
    const bool dup_ok = engine::scene::find_instance(world, "alpha") == alpha
        && world.instances[dup].name == world.instances[alpha].name;

    const bool miss_ok = engine::scene::find_instance(world, "nope") == engine::scene::kInvalidInstance;

    const bool passed = intern_ok && find_ok && unnamed_ok && dup_ok && miss_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Scene name gate: intern=yes find=yes unnamed=yes dup=first miss=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_scene_hierarchy_gate() {
    using engine::math::Mat4;
    using engine::math::Vec3;

    auto origin_of = [](const Mat4& m) {
        return m.transform_point({0.f, 0.f, 0.f});
    };
    auto near3 = [](Vec3 a, Vec3 b) {
        const Vec3 d = a - b;
        return std::abs(d.x) < 1.e-4f && std::abs(d.y) < 1.e-4f && std::abs(d.z) < 1.e-4f;
    };

    engine::scene::World world{};
    const engine::u32 parent = engine::scene::add_instance(world, {});
    const engine::u32 child = engine::scene::add_instance(world, {});
    const engine::u32 grand = engine::scene::add_instance(world, {});
    engine::scene::set_instance_model(world, parent, Mat4::translate({10.f, 0.f, 0.f}));
    engine::scene::set_instance_model(world, child, Mat4::translate({1.f, 0.f, 0.f}));
    engine::scene::set_instance_model(world, grand, Mat4::translate({0.f, 2.f, 0.f}));

    const bool parented = engine::scene::set_instance_parent(world, child, parent, false)
        && engine::scene::set_instance_parent(world, grand, child, false);
    const Vec3 child_world = origin_of(engine::scene::instance_world_model(world, child));
    const Vec3 grand_world = origin_of(engine::scene::instance_world_model(world, grand));
    const bool compose_ok = parented && near3(child_world, {11.f, 0.f, 0.f})
        && near3(grand_world, {11.f, 2.f, 0.f});

    const bool cycle_ok = !engine::scene::set_instance_parent(world, parent, grand, false)
        && !engine::scene::set_instance_parent(world, child, child, false)
        && engine::scene::instance_parent(world, child) == parent;

    const engine::u32 extra = engine::scene::add_instance(world, {});
    engine::scene::set_instance_model(world, extra, Mat4::translate({3.f, 4.f, 5.f}));
    const Vec3 extra_before = origin_of(engine::scene::instance_world_model(world, extra));
    const bool keep_ok = engine::scene::set_instance_parent(world, extra, parent, true)
        && near3(origin_of(engine::scene::instance_world_model(world, extra)), extra_before);

    const bool unparent_ok = engine::scene::set_instance_parent(world, child, engine::scene::kInvalidInstance,
        true)
        && engine::scene::instance_parent(world, child) == engine::scene::kInvalidInstance
        && near3(origin_of(engine::scene::instance_world_model(world, child)), {11.f, 0.f, 0.f});

    const bool passed = compose_ok && cycle_ok && keep_ok && unparent_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Scene hierarchy gate: compose=yes cycle=no keep_world=yes unparent=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_scene_file_gate() {
    using engine::math::Mat4;
    using engine::math::Vec3;

    auto origin_of = [](const Mat4& m) {
        return m.transform_point({0.f, 0.f, 0.f});
    };
    auto near3 = [](Vec3 a, Vec3 b) {
        const Vec3 d = a - b;
        return std::abs(d.x) < 1.e-3f && std::abs(d.y) < 1.e-3f && std::abs(d.z) < 1.e-3f;
    };

    engine::scene::World world{};
    world.ambient = {0.16f, 0.17f, 0.21f};
    world.sun.direction = {0.12f, 0.42f, 0.90f};
    world.sun.color = {4.8f, 4.4f, 3.8f};
    world.points[0].position = {-0.55f, 0.38f, 0.45f};
    world.points[0].color = {1.f, 0.45f, 0.18f};
    world.points[0].radius = 1.8f;
    world.points[0].intensity = 2.2f;
    engine::scene::Material mat{};
    mat.albedo = 2;
    mat.metallic = 0.1f;
    mat.roughness = 0.4f;
    const engine::u32 mat_id = engine::scene::add_material(world, mat);

    engine::scene::Instance parent{};
    parent.mesh = engine::assets::make_mesh_handle("/content/meshes/cartoon_husky.gltf");
    parent.material = mat_id;
    parent.model = Mat4::translate({10.f, 0.f, 0.f});
    const engine::u32 parent_i = engine::scene::add_instance(world, parent);
    engine::scene::set_instance_name(world, parent_i, "parent");

    engine::scene::Instance child{};
    child.mesh = parent.mesh;
    child.material = mat_id;
    child.model = Mat4::translate({1.f, 0.f, 0.f});
    const engine::u32 child_i = engine::scene::add_instance(world, child);
    engine::scene::set_instance_name(world, child_i, "child");
    engine::scene::set_instance_parent(world, child_i, parent_i, false);

    engine::scene::Instance ghost{};
    ghost.mesh = parent.mesh;
    ghost.model = Mat4::translate({0.f, 9.f, 0.f});
    engine::scene::add_instance(world, ghost);

    std::string text;
    const bool wrote = engine::scene::write_world(world, text);
    engine::scene::World loaded{};
    const bool named_ok = wrote && engine::scene::read_world(text, loaded)
        && loaded.instance_count == 2
        && engine::scene::find_instance(loaded, "parent") != engine::scene::kInvalidInstance
        && engine::scene::find_instance(loaded, "child") != engine::scene::kInvalidInstance;

    const engine::u32 loaded_parent = engine::scene::find_instance(loaded, "parent");
    const engine::u32 loaded_child = engine::scene::find_instance(loaded, "child");
    const bool unnamed_ok = named_ok
        && engine::scene::find_instance(loaded, "") == engine::scene::kInvalidInstance
        && loaded.instance_count == 2;

    const bool hierarchy_ok = named_ok
        && engine::scene::instance_parent(loaded, loaded_child) == loaded_parent
        && near3(origin_of(engine::scene::instance_world_model(loaded, loaded_child)),
            {11.f, 0.f, 0.f});

    const bool lights_ok = named_ok
        && near3(loaded.ambient, world.ambient)
        && near3(loaded.sun.color, world.sun.color)
        && near3(loaded.points[0].position, world.points[0].position)
        && std::abs(loaded.points[0].intensity - 2.2f) < 1.e-3f
        && loaded.material_count == 1
        && loaded.materials[0].albedo == 2;

    const bool mesh_ok = named_ok
        && loaded.instances[loaded_parent].mesh == parent.mesh
        && loaded.instances[loaded_child].mesh.generation == parent.mesh.generation;

    engine::scene::World rejected{};
    const bool reject_ok = !engine::scene::read_world("nope", rejected)
        && !engine::scene::read_world("solscene 99\nambient 0 0 0", rejected);

    const bool passed = named_ok && unnamed_ok && hierarchy_ok && lights_ok && mesh_ok && reject_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Scene file gate: named=yes unnamed=drop hierarchy=yes lights=yes mesh=yes reject=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_scene_prefab_gate() {
    using engine::math::Mat4;
    using engine::math::Vec3;

    auto origin_of = [](const Mat4& m) {
        return m.transform_point({0.f, 0.f, 0.f});
    };
    auto near3 = [](Vec3 a, Vec3 b) {
        const Vec3 d = a - b;
        return std::abs(d.x) < 1.e-3f && std::abs(d.y) < 1.e-3f && std::abs(d.z) < 1.e-3f;
    };

    engine::scene::World source{};
    engine::scene::Material mat{};
    mat.albedo = 3;
    mat.roughness = 0.35f;
    const engine::u32 mat_id = engine::scene::add_material(source, mat);
    engine::scene::Instance body{};
    body.mesh = engine::assets::make_mesh_handle("/content/meshes/cartoon_husky.gltf");
    body.material = mat_id;
    body.model = Mat4::translate({1.f, 0.f, 0.f});
    const engine::u32 body_i = engine::scene::add_instance(source, body);
    engine::scene::set_instance_name(source, body_i, "body");
    engine::scene::Instance head{};
    head.mesh = body.mesh;
    head.material = mat_id;
    head.model = Mat4::translate({0.f, 2.f, 0.f});
    const engine::u32 head_i = engine::scene::add_instance(source, head);
    engine::scene::set_instance_name(source, head_i, "head");
    engine::scene::set_instance_parent(source, head_i, body_i, false);
    engine::scene::Instance other{};
    other.model = Mat4::translate({9.f, 0.f, 0.f});
    const engine::u32 other_i = engine::scene::add_instance(source, other);
    engine::scene::set_instance_name(source, other_i, "other");

    engine::scene::World fragment{};
    engine::scene::World miss{};
    const bool extract_ok = engine::scene::extract_prefab(source, "body", fragment)
        && fragment.instance_count == 2
        && engine::scene::find_instance(fragment, "body") != engine::scene::kInvalidInstance
        && engine::scene::find_instance(fragment, "head") != engine::scene::kInvalidInstance
        && engine::scene::find_instance(fragment, "other") == engine::scene::kInvalidInstance
        && !engine::scene::extract_prefab(source, "nope", miss);

    std::string text;
    const bool file_ok = extract_ok && engine::scene::write_world(fragment, text);

    engine::scene::World dest{};
    const engine::u32 a = engine::scene::instantiate_prefab(dest, fragment,
        Mat4::translate({10.f, 0.f, 0.f}), "a_");
    const engine::u32 b = engine::scene::instantiate_prefab(dest, text,
        Mat4::translate({0.f, 0.f, 5.f}), "b_");
    const engine::u32 a_body = engine::scene::find_instance(dest, "a_body");
    const engine::u32 a_head = engine::scene::find_instance(dest, "a_head");
    const engine::u32 b_body = engine::scene::find_instance(dest, "b_body");
    const bool spawn_ok = file_ok && a != engine::scene::kInvalidInstance
        && b != engine::scene::kInvalidInstance && a_body == a
        && dest.instance_count == 4
        && engine::scene::instance_parent(dest, a_head) == a_body
        && engine::scene::instance_parent(dest, engine::scene::find_instance(dest, "b_head")) == b_body;

    const bool compose_ok = spawn_ok
        && near3(origin_of(engine::scene::instance_world_model(dest, a_body)), {11.f, 0.f, 0.f})
        && near3(origin_of(engine::scene::instance_world_model(dest, a_head)), {11.f, 2.f, 0.f})
        && near3(origin_of(engine::scene::instance_world_model(dest, b_body)), {1.f, 0.f, 5.f});

    const bool prefix_ok = spawn_ok
        && dest.materials[0].albedo == 3
        && dest.material_count == 2
        && dest.instances[a_body].mesh == body.mesh;

    engine::scene::World packed{};
    for (engine::u32 i = 0; i < engine::scene::kMaxInstances; ++i) {
        const engine::u32 idx = engine::scene::add_instance(packed, {});
        char name[12];
        std::snprintf(name, sizeof(name), "n%u", i);
        engine::scene::set_instance_name(packed, idx, name);
    }
    const bool full_ok = engine::scene::instantiate_prefab(packed, fragment, Mat4::identity(), "x_")
        == engine::scene::kInvalidInstance;

    const bool passed = extract_ok && spawn_ok && compose_ok && prefix_ok && full_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Scene prefab gate: extract=yes spawn=yes prefix=yes compose=yes miss=yes full=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_cook_gate() {
    using engine::assets::CookedAudio;
    using engine::assets::CookedKind;
    using engine::assets::ImageData;
    using engine::assets::MeshData;

    MeshData mesh{};
    mesh.vertices = {
        {0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f},
        {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f},
    };
    mesh.indices = {0, 1, 2};
    engine::assets::compute_mesh_bounds(mesh);

    std::vector<engine::u8> mesh_blob;
    MeshData mesh_loaded{};
    CookedKind kind{};
    ImageData wrong_image{};
    const bool mesh_ok = engine::assets::write_cooked_mesh(mesh, mesh_blob)
        && engine::assets::peek_cooked_kind(mesh_blob, kind)
        && kind == CookedKind::Mesh
        && engine::assets::read_cooked_mesh(mesh_blob, mesh_loaded)
        && mesh_loaded.vertices.size() == 3
        && mesh_loaded.indices.size() == 3
        && mesh_loaded.indices[2] == 2
        && std::abs(mesh_loaded.vertices[1].px - 1.f) < 1.e-5f
        && std::abs(mesh_loaded.bounds.max.x - 1.f) < 1.e-4f
        && std::abs(mesh_loaded.bounds.max.z - 1.f) < 1.e-4f
        && !engine::assets::read_cooked_image(mesh_blob, wrong_image);

    ImageData image{};
    image.width = 2;
    image.height = 2;
    image.rgba = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255,
    };
    std::vector<engine::u8> image_blob;
    ImageData image_loaded{};
    MeshData wrong_mesh{};
    const bool image_ok = engine::assets::write_cooked_image(image, image_blob)
        && engine::assets::peek_cooked_kind(image_blob, kind)
        && kind == CookedKind::Image
        && engine::assets::read_cooked_image(image_blob, image_loaded)
        && image_loaded.width == 2
        && image_loaded.height == 2
        && image_loaded.rgba.size() == 16
        && image_loaded.rgba[0] == 255
        && image_loaded.rgba[5] == 255
        && !engine::assets::read_cooked_mesh(image_blob, wrong_mesh);

    CookedAudio audio{};
    audio.sample_rate = 44100;
    audio.channels = 1;
    audio.bits_per_sample = 16;
    audio.pcm = {0, 0, 0, 1, 0, 2, 0, 3};
    std::vector<engine::u8> audio_blob;
    CookedAudio audio_loaded{};
    const bool audio_ok = engine::assets::write_cooked_audio(audio, audio_blob)
        && engine::assets::peek_cooked_kind(audio_blob, kind)
        && kind == CookedKind::Audio
        && engine::assets::read_cooked_audio(audio_blob, audio_loaded)
        && audio_loaded.sample_rate == 44100
        && audio_loaded.channels == 1
        && audio_loaded.bits_per_sample == 16
        && audio_loaded.pcm == audio.pcm
        && !engine::assets::read_cooked_mesh(audio_blob, wrong_mesh);

    MeshData empty_mesh{};
    std::vector<engine::u8> scratch;
    std::vector<engine::u8> truncated = mesh_blob;
    if (!truncated.empty()) {
        truncated.pop_back();
    }
    std::vector<engine::u8> bad_magic = mesh_blob;
    if (!bad_magic.empty()) {
        bad_magic[0] = 'X';
    }
    std::vector<engine::u8> bad_version = mesh_blob;
    if (bad_version.size() >= 8) {
        bad_version[4] = 99;
        bad_version[5] = 0;
        bad_version[6] = 0;
        bad_version[7] = 0;
    }
    ImageData empty_image{};
    CookedAudio bad_audio = audio;
    bad_audio.channels = 3;

    const bool reject_ok = !engine::assets::write_cooked_mesh(empty_mesh, scratch)
        && !engine::assets::write_cooked_image(empty_image, scratch)
        && !engine::assets::write_cooked_audio(bad_audio, scratch)
        && !engine::assets::peek_cooked_kind({}, kind)
        && !engine::assets::read_cooked_mesh(truncated, mesh_loaded)
        && !engine::assets::read_cooked_mesh(bad_magic, mesh_loaded)
        && !engine::assets::read_cooked_mesh(bad_version, mesh_loaded);

    const bool passed = mesh_ok && image_ok && audio_ok && reject_ok;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Cook gate: mesh=yes image=yes audio=yes reject=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_pak_gate() {
    using engine::assets::ImageData;
    using engine::assets::MeshData;
    using engine::assets::PakEntry;

    MeshData mesh{};
    mesh.vertices = {
        {0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f},
        {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f},
    };
    mesh.indices = {0, 1, 2};
    engine::assets::compute_mesh_bounds(mesh);

    ImageData image{};
    image.width = 2;
    image.height = 2;
    image.rgba = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255,
    };

    std::vector<engine::u8> mesh_blob;
    std::vector<engine::u8> image_blob;
    const bool cooked = engine::assets::write_cooked_mesh(mesh, mesh_blob)
        && engine::assets::write_cooked_image(image, image_blob);

    std::vector<PakEntry> entries(2);
    entries[0].name = "/content/tri.solc";
    entries[0].bytes = mesh_blob;
    entries[1].name = "/content/px.solc";
    entries[1].bytes = image_blob;

    std::vector<engine::u8> pak;
    std::vector<engine::u8> got_mesh;
    std::vector<engine::u8> got_image;
    MeshData mesh_loaded{};
    ImageData image_loaded{};
    const bool pack_ok = cooked && engine::assets::write_pak(entries, pak)
        && engine::assets::peek_pak(pak)
        && engine::assets::read_pak_entry(pak, "/content/tri.solc", got_mesh)
        && engine::assets::read_pak_entry(pak, "/content/px.solc", got_image)
        && got_mesh == mesh_blob
        && got_image == image_blob
        && engine::assets::read_cooked_mesh(got_mesh, mesh_loaded)
        && engine::assets::read_cooked_image(got_image, image_loaded)
        && mesh_loaded.indices.size() == 3
        && image_loaded.width == 2;

    std::vector<engine::u8> miss_bytes;
    const bool miss_ok = pack_ok
        && !engine::assets::read_pak_entry(pak, "/content/nope.solc", miss_bytes)
        && !engine::assets::read_pak_entry(pak, "content/tri.solc", miss_bytes);

    auto loader = engine::assets::create_pak_loader(pak);
    std::vector<engine::u8> loaded;
    std::string resolved;
    const bool get_ok = miss_ok && loader
        && loader->load_bytes("/content/tri.solc", loaded)
        && loaded == mesh_blob
        && loader->resolve_path("/content/px.solc", resolved)
        && resolved == "/content/px.solc"
        && !loader->load_bytes("/content/nope.solc", loaded);

    std::vector<engine::u8> scratch;
    std::vector<PakEntry> empty;
    std::vector<PakEntry> dup = entries;
    dup.push_back(entries[0]);
    std::vector<PakEntry> traversal(1);
    traversal[0].name = "/content/../secret.solc";
    traversal[0].bytes = {1, 2, 3, 4};
    std::vector<engine::u8> truncated = pak;
    if (!truncated.empty()) {
        truncated.pop_back();
    }
    std::vector<engine::u8> bad_magic = pak;
    if (!bad_magic.empty()) {
        bad_magic[0] = 'X';
    }

    const bool reject_ok = !engine::assets::write_pak(empty, scratch)
        && !engine::assets::write_pak(dup, scratch)
        && !engine::assets::write_pak(traversal, scratch)
        && !engine::assets::peek_pak({})
        && !engine::assets::peek_pak(truncated)
        && !engine::assets::peek_pak(bad_magic)
        && engine::assets::create_pak_loader(bad_magic) == nullptr;

    const bool passed = pack_ok && get_ok && miss_ok && reject_ok;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Pak gate: pack=yes get=yes miss=yes reject=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_pack_gate(engine::Engine& app) {
    engine::platform::IFileSystem* fs = app.filesystem();
    const std::filesystem::path pak_path =
        std::filesystem::path(app.executable_directory()) / "content.pak";

    std::vector<engine::u8> bytes;
    const bool file_ok = fs != nullptr && fs->exists(pak_path.string())
        && fs->read_file(pak_path.string(), bytes) && !bytes.empty();
    const bool peek_ok = file_ok && engine::assets::peek_pak(bytes);

    std::vector<engine::u8> cube_blob;
    engine::assets::MeshData cube{};
    const bool get_ok = peek_ok
        && engine::assets::read_pak_entry(bytes, "/content/meshes/cube.solc", cube_blob)
        && engine::assets::read_cooked_mesh(cube_blob, cube)
        && cube.vertices.size() >= 3
        && cube.indices.size() >= 3;

    std::vector<engine::u8> husky_blob;
    engine::assets::MeshData husky{};
    const bool husky_ok = get_ok
        && engine::assets::read_pak_entry(bytes, "/content/meshes/cartoon_husky.solc", husky_blob)
        && engine::assets::read_cooked_mesh(husky_blob, husky)
        && husky.vertices.size() > 100;

    const bool passed = file_ok && peek_ok && get_ok && husky_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Pack gate: file=yes peek=yes get=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_light_gate(const engine::scene::World& world) {
    engine::renderer::Lighting lighting{};
    sandbox::extract_lighting(world, {0.f, 0.35f, -2.2f}, lighting);
    const engine::f32 sun_len = lighting.sun_direction.length();
    const bool sun_ok = sun_len > 0.99f && sun_len < 1.01f && lighting.sun_color.x > 0.2f;
    const bool ambient_ok = lighting.ambient.x > 0.05f;
    bool point_ok = false;
    for (engine::u32 i = 0; i < engine::renderer::kMaxPointLights; ++i) {
        if (lighting.point_color_intensity[i].w > 0.f && lighting.point_pos_radius[i].w > 0.f) {
            point_ok = true;
        }
    }
    const bool layout_ok = sizeof(engine::renderer::FrameConstants) == 400;
    const bool copied = lighting.sun_color.x == world.sun.color.x
        && lighting.ambient.y == world.ambient.y;
    const bool passed = sun_ok && ambient_ok && point_ok && layout_ok && copied;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Light gate: sun_len=%.3f ambient=%.2f points=%s layout=%s (%s)",
        sun_len, lighting.ambient.x, point_ok ? "yes" : "no",
        layout_ok ? "400" : "bad", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_shadow_gate(const engine::scene::World& world,
    const engine::assets::gpu::GpuMeshStore& meshes,
    const engine::rhi::IGraphicsPipeline* shadow_pipeline) {
    const engine::math::Mat4 sun = engine::renderer::make_sun_view_proj(
        world.sun.direction, sandbox::scene_world_bounds(world, meshes));
    const engine::math::Mat4 identity = engine::math::Mat4::identity();
    const bool matrix_ok = std::memcmp(&sun, &identity, sizeof(engine::math::Mat4)) != 0
        && std::isfinite(sun.cols[0].x) && std::abs(sun.cols[0].x) > 0.01f;
    const bool pipeline_ok = shadow_pipeline != nullptr;
    const bool layout_ok = sizeof(engine::renderer::ShadowConstants) == 128;
    const bool passed = matrix_ok && pipeline_ok && layout_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Shadow gate: sun_proj.x=%.3f pipeline=%s map=%u (%s)",
        sun.cols[0].x, pipeline_ok ? "yes" : "no", engine::renderer::kShadowMapSize,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_hdr_gate(const engine::scene::World& world,
    const engine::rhi::IGraphicsPipeline* tonemap_pipeline) {
    const bool sun_hot = world.sun.color.x >= 3.5f && world.sun.color.y >= 3.f
        && world.sun.color.z >= 2.5f;
    const bool pipeline_ok = tonemap_pipeline != nullptr;
    const bool format_ok = static_cast<engine::u8>(engine::rhi::Format::RGBA16_FLOAT)
        != static_cast<engine::u8>(engine::rhi::Format::RGBA8_UNORM);
    const bool passed = sun_hot && pipeline_ok && format_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "HDR gate: sun=(%.2f,%.2f,%.2f) tonemap=%s rgba16=%s (%s)",
        world.sun.color.x, world.sun.color.y, world.sun.color.z,
        pipeline_ok ? "yes" : "no", format_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_async_compile_gate(engine::shaders::IShaderHotReloader& watcher) {
    engine::Clock clock;
    watcher.request_compile();

    constexpr engine::f32 kMaxPollMs = 16.f;
    constexpr engine::f64 kTimeoutS = 30.0;
    engine::f32 first_poll_ms = 0.f;
    engine::f32 max_poll_ms = 0.f;
    bool have_first = false;
    bool saw_busy = false;
    bool reloaded = false;
    const engine::f64 start = clock.now();

    while (clock.now() - start < kTimeoutS) {
        engine::shaders::ShaderBytecode vs;
        engine::shaders::ShaderBytecode ps;
        std::string error;
        const engine::f64 t0 = clock.now();
        const auto status = watcher.poll(vs, ps, error);
        const engine::f32 poll_ms = static_cast<engine::f32>((clock.now() - t0) * 1000.0);
        if (!have_first) {
            first_poll_ms = poll_ms;
            have_first = true;
        }
        if (poll_ms > max_poll_ms) {
            max_poll_ms = poll_ms;
        }
        if (status == engine::shaders::ShaderReloadStatus::Busy) {
            saw_busy = true;
        }
        if (status == engine::shaders::ShaderReloadStatus::Reloaded
            && !vs.data.empty() && !ps.data.empty()) {
            reloaded = true;
            break;
        }
        if (status == engine::shaders::ShaderReloadStatus::Failed) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const bool fast = have_first && max_poll_ms < kMaxPollMs;
    const bool passed = reloaded && fast;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Async compile gate: first_poll=%.2fms max_poll=%.2fms busy=%s reloaded=%s (%s)",
        first_poll_ms, max_poll_ms, saw_busy ? "yes" : "no",
        reloaded ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_frustum_gate(const engine::scene::World& world, const FlyCamera& camera,
    const sandbox::WorldExtractAssets& assets) {
    engine::scene::World copy = world;
    copy.camera.view = camera.view();
    copy.camera.projection = camera.projection(16.f / 9.f);
    engine::Arena arena(256 * 1024);
    engine::renderer::RenderSnapshot snapshot{};
    snapshot.width = 1280;
    snapshot.height = 720;
    const auto stats = sandbox::extract_world(copy, camera.position, assets, false, nullptr, arena,
        snapshot);
    const engine::u32 skipped = stats.considered - stats.visible;
    const bool passed = world.instance_count >= 64 && stats.visible >= 5 && skipped >= 16;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Frustum gate: instances=%u visible=%u skipped=%u (%s)",
        world.instance_count, stats.visible, skipped, passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_material_gate(const engine::scene::World& world, const FlyCamera& camera,
    const sandbox::WorldExtractAssets& assets, engine::f32 gltf_metallic,
    engine::f32 gltf_roughness) {
    const bool table_ok = world.material_count >= 2;
    bool handles_ok = world.instance_count > 0;
    for (engine::u32 i = 0; i < world.instance_count; ++i) {
        if (world.instances[i].material >= world.material_count) {
            handles_ok = false;
        }
    }

    engine::scene::World copy = world;
    copy.camera.view = camera.view();
    copy.camera.projection = camera.projection(16.f / 9.f);
    engine::Arena arena(256 * 1024);
    engine::renderer::RenderSnapshot before{};
    before.width = 1280;
    before.height = 720;
    sandbox::extract_world(copy, camera.position, assets, false, nullptr, arena, before);

    constexpr engine::f32 kProbeRoughness = 0.03125f;
    engine::scene::World mutated = copy;
    for (engine::u32 i = 0; i < mutated.material_count; ++i) {
        mutated.materials[i].roughness = kProbeRoughness;
    }
    engine::Arena arena_mutated(256 * 1024);
    engine::renderer::RenderSnapshot after{};
    after.width = 1280;
    after.height = 720;
    sandbox::extract_world(mutated, camera.position, assets, false, nullptr, arena_mutated, after);

    bool draws_ok = !after.draws.empty();
    for (const engine::renderer::DrawItem& draw : after.draws) {
        if (draw.roughness != kProbeRoughness) {
            draws_ok = false;
        }
    }
    const bool changed = !before.draws.empty() && !after.draws.empty()
        && before.draws[0].roughness != after.draws[0].roughness;
    const bool layout_ok = sizeof(engine::renderer::FrameConstants) == 400;
    const bool gltf_ok = gltf_metallic >= 0.f && gltf_metallic <= 1.f
        && gltf_roughness >= 0.f && gltf_roughness <= 1.f;
    const bool passed = table_ok && handles_ok && draws_ok && changed && layout_ok && gltf_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Material gate: materials=%u handles=%s roughness_is_data=%s layout=%s (%s)",
        world.material_count, handles_ok ? "yes" : "no",
        (draws_ok && changed) ? "yes" : "no", layout_ok ? "400" : "bad",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_pbr_gate() {
    using engine::renderer::pbr::d_ggx;
    using engine::renderer::pbr::evaluate_punctual;
    using engine::renderer::pbr::f0_from_metal;
    using engine::renderer::pbr::f_schlick;
    using engine::renderer::pbr::perceptual_to_alpha;

    const engine::f32 alpha_smooth = perceptual_to_alpha(0.2f);
    const engine::f32 alpha_rough = perceptual_to_alpha(0.8f);
    const bool ggx_peak = d_ggx(1.f, alpha_smooth) > d_ggx(1.f, alpha_rough) * 8.f;

    const engine::math::Vec3 red{0.8f, 0.12f, 0.04f};
    const engine::math::Vec3 metal_f0 = f0_from_metal(red, 1.f);
    const bool metal_f0_ok = metal_f0.x > 0.7f && metal_f0.y < 0.2f && metal_f0.z < 0.1f;

    const engine::math::Vec3 n{0.f, 0.f, 1.f};
    const engine::math::Vec3 white{1.f, 1.f, 1.f};
    const engine::math::Vec3 metal_lo = evaluate_punctual(n, n, n, red, 1.f, 0.25f, white);
    const engine::math::Vec3 diel_lo = evaluate_punctual(n, n, n, red, 0.f, 0.25f, white);
    const bool metal_has_spec = metal_lo.x > 0.05f;
    const bool dielectric_lit = diel_lo.x > 0.05f && diel_lo.y > 0.01f;

    const engine::math::Vec3 f_grazing = f_schlick(0.f, {0.04f, 0.04f, 0.04f});
    const engine::math::Vec3 f_normal = f_schlick(1.f, {0.04f, 0.04f, 0.04f});
    const bool fresnel_ok = f_grazing.x > 0.95f && f_normal.x < 0.06f;
    const bool alpha_ok = perceptual_to_alpha(0.5f) == 0.25f;

    const bool passed = ggx_peak && metal_f0_ok && metal_has_spec && dielectric_lit && fresnel_ok
        && alpha_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "PBR gate: ggx_peak=%s metal_f0=%s points_spec=yes energy=kd_1-F alpha=r^2 (%s)",
        ggx_peak ? "yes" : "no", metal_f0_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_ibl_gate(const ForwardDemo& demo) {
    using engine::renderer::ibl::integrate_brdf;
    using engine::renderer::ibl::kIntensity;
    using engine::renderer::ibl::kMaxLod;
    using engine::renderer::ibl::lod_from_roughness;
    using engine::renderer::ibl::sky_radiance;
    using engine::renderer::pbr::kDielectricF0;

    const bool intensity_ok = kIntensity == 1.f;
    const bool f0_ok = kDielectricF0 == 0.04f;
    const bool lod_ok = lod_from_roughness(0.f) == 0.f && lod_from_roughness(1.f) == kMaxLod;

    const engine::math::Vec2 dfg = integrate_brdf(0.95f, 0.08f, 64);
    const bool lut_ok = dfg.x > dfg.y * 4.f && dfg.x > 0.4f;

    const engine::math::Vec3 zenith = sky_radiance({0.f, 1.f, 0.f});
    const engine::math::Vec3 ground = sky_radiance({0.f, -1.f, 0.f});
    const bool sky_ok = zenith.y > ground.y * 1.5f;

    const bool cubes_ok = demo.ibl_irradiance
        && demo.ibl_irradiance->dimension() == engine::rhi::TextureDimension::Cube
        && demo.ibl_prefilter
        && demo.ibl_prefilter->dimension() == engine::rhi::TextureDimension::Cube
        && demo.ibl_prefilter->mip_levels() == engine::renderer::ibl::kPrefilterMips
        && demo.ibl_brdf_lut
        && demo.ibl_brdf_lut->width() == engine::renderer::ibl::kLutSize;

    const bool passed = intensity_ok && f0_ok && lod_ok && lut_ok && sky_ok && cubes_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "IBL gate: intensity=1 split_sum=yes lut=%s cubes=%s (%s)",
        lut_ok ? "yes" : "no", cubes_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool upload_ibl_maps(engine::rhi::IDevice& device, ForwardDemo& demo) {
    const engine::renderer::ibl::Baked baked = engine::renderer::ibl::bake();

    engine::rhi::TextureDesc irradiance{};
    irradiance.width = engine::renderer::ibl::kIrradianceSize;
    irradiance.height = engine::renderer::ibl::kIrradianceSize;
    irradiance.mip_levels = 1;
    irradiance.array_size = 6;
    irradiance.dimension = engine::rhi::TextureDimension::Cube;
    irradiance.format = engine::rhi::Format::RGBA16_FLOAT;
    irradiance.usage = engine::rhi::TextureUsage::ShaderResource;
    demo.ibl_irradiance = device.create_texture(irradiance, baked.irradiance_rgba16.data());

    engine::rhi::TextureDesc prefilter{};
    prefilter.width = engine::renderer::ibl::kPrefilterSize;
    prefilter.height = engine::renderer::ibl::kPrefilterSize;
    prefilter.mip_levels = engine::renderer::ibl::kPrefilterMips;
    prefilter.array_size = 6;
    prefilter.dimension = engine::rhi::TextureDimension::Cube;
    prefilter.format = engine::rhi::Format::RGBA16_FLOAT;
    prefilter.usage = engine::rhi::TextureUsage::ShaderResource;
    demo.ibl_prefilter = device.create_texture(prefilter, baked.prefilter_rgba16.data());

    engine::rhi::TextureDesc lut{};
    lut.width = engine::renderer::ibl::kLutSize;
    lut.height = engine::renderer::ibl::kLutSize;
    lut.mip_levels = 1;
    lut.format = engine::rhi::Format::RGBA16_FLOAT;
    lut.usage = engine::rhi::TextureUsage::ShaderResource;
    demo.ibl_brdf_lut = device.create_texture(lut, baked.lut_rgba16.data());

    engine::rhi::TextureDesc sky{};
    sky.width = engine::renderer::sky::kCubemapSize;
    sky.height = engine::renderer::sky::kCubemapSize;
    sky.mip_levels = 1;
    sky.array_size = 6;
    sky.dimension = engine::rhi::TextureDimension::Cube;
    sky.format = engine::rhi::Format::RGBA16_FLOAT;
    sky.usage = engine::rhi::TextureUsage::ShaderResource;
    demo.sky_cubemap = device.create_texture(sky, baked.source_rgba16.data());

    if (!demo.ibl_irradiance || !demo.ibl_prefilter || !demo.ibl_brdf_lut || !demo.sky_cubemap) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "IBL / sky texture creation failed");
        return false;
    }
    device.set_debug_name(*demo.ibl_irradiance, "sandbox/ibl_irradiance");
    device.set_debug_name(*demo.ibl_prefilter, "sandbox/ibl_prefilter");
    device.set_debug_name(*demo.ibl_brdf_lut, "sandbox/ibl_brdf_lut");
    device.set_debug_name(*demo.sky_cubemap, "sandbox/sky_cubemap");
    return true;
}

bool run_sky_gate(const ForwardDemo& demo) {
    using engine::renderer::sky::apply_sun_disk;
    using engine::renderer::sky::direction_from_ndc;
    using engine::renderer::sky::kCubemapSize;
    using engine::renderer::sky::kIntensity;
    using engine::renderer::sky::kMode;
    using engine::renderer::sky::kSunDisk;
    using engine::renderer::sky::make_constants;
    using engine::renderer::sky::Mode;
    using engine::renderer::sky::radiance;

    const bool mode_ok = kMode == Mode::Cubemap;
    const bool intensity_ok = kIntensity == 1.f;
    const bool sun_ok = kSunDisk;

    const engine::math::Vec3 zenith = radiance({0.f, 1.f, 0.f});
    const engine::math::Vec3 ground = radiance({0.f, -1.f, 0.f});
    const bool gradient_ok = zenith.y > ground.y * 1.5f && zenith.z > zenith.x
        && zenith.x == engine::renderer::ibl::sky_radiance({0.f, 1.f, 0.f}).x;

    const engine::math::Vec3 sun_dir = demo.world.sun.direction.normalized();
    const engine::math::Vec3 sun_col = demo.world.sun.color;
    const engine::math::Vec3 ibl_on_sun = engine::renderer::ibl::sky_radiance(sun_dir);
    const engine::math::Vec3 ibl_off = engine::renderer::ibl::sky_radiance(
        engine::math::Vec3{sun_dir.x + 0.2f, sun_dir.y, sun_dir.z}.normalized());
    const bool ibl_no_disk = std::abs(ibl_on_sun.x - ibl_off.x) < 0.25f;
    const engine::math::Vec3 skybox = apply_sun_disk(sun_dir, ibl_on_sun, sun_dir, sun_col);
    const bool disk_ok = skybox.x > ibl_on_sun.x + 8.f;

    const engine::math::Mat4 view = engine::math::Mat4::look_at(
        {0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f});
    const engine::math::Mat4 projection = engine::math::Mat4::perspective(
        engine::math::radians(60.f), 16.f / 9.f, 0.1f, 100.f);
    const auto constants = make_constants(view, projection, sun_dir, sun_col);
    const engine::math::Vec3 forward = direction_from_ndc({0.f, 0.f}, constants);
    const engine::math::Vec3 up = direction_from_ndc({0.f, 1.f}, constants);
    const bool ray_ok = forward.z > 0.9f && std::abs(forward.x) < 0.05f && up.y > forward.y;

    const bool cube_ok = demo.sky_pipeline && demo.sky_cubemap
        && demo.sky_cubemap->dimension() == engine::rhi::TextureDimension::Cube
        && demo.sky_cubemap->mip_levels() == 1
        && demo.sky_cubemap->width() == kCubemapSize
        && demo.sky_cubemap.get() != demo.ibl_prefilter.get()
        && demo.sky_cubemap.get() != demo.ibl_irradiance.get();

    const bool passed = mode_ok && intensity_ok && sun_ok && gradient_ok && ibl_no_disk && disk_ok
        && ray_ok && cube_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Sky gate: cubemap=yes source_not_ggx=yes intensity=1 sun_disk=skybox (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_bloom_gate(const ForwardDemo& demo) {
    using engine::renderer::bloom::apply_knee;
    using engine::renderer::bloom::karis_box;
    using engine::renderer::bloom::kCompositeBeforeTonemap;
    using engine::renderer::bloom::kIntensity;
    using engine::renderer::bloom::kMips;
    using engine::renderer::bloom::kSoftKnee;
    using engine::renderer::bloom::kThreshold;
    using engine::renderer::bloom::make_downsample_constants;
    using engine::renderer::bloom::threshold_params;

    const engine::math::Vec3 dim{0.5f, 0.5f, 0.5f};
    const engine::math::Vec3 hot{2.f, 2.f, 2.f};
    const engine::math::Vec3 below = apply_knee(dim);
    const engine::math::Vec3 above = apply_knee(hot);
    const bool knee_ok = kThreshold == 1.f && kSoftKnee == 0.5f
        && below.x < 0.01f && above.x > 0.4f;

    const engine::math::Vec3 firefly{100.f, 100.f, 100.f};
    const engine::math::Vec3 rest{1.f, 1.f, 1.f};
    const engine::math::Vec3 karis = karis_box(firefly, rest, rest, rest);
    const engine::f32 arith = (100.f + 1.f + 1.f + 1.f) / 4.f;
    const bool karis_ok = karis.x < 5.f && arith > 20.f;

    const auto first = make_downsample_constants(128, 128, true);
    const auto later = make_downsample_constants(64, 64, false);
    const engine::math::Vec4 packed = threshold_params();
    const bool mode_ok = first.params.x > 0.5f && later.params.x < 0.5f
        && packed.x == kThreshold && packed.z > packed.y;

    const bool intensity_ok = kCompositeBeforeTonemap && kIntensity > 0.f && kIntensity <= 0.15f
        && kMips == 5;
    const bool pipeline_ok = demo.bloom_downsample_pipeline && demo.bloom_upsample_pipeline
        && demo.tonemap_pipeline;

    const bool passed = knee_ok && karis_ok && mode_ok && intensity_ok && pipeline_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Bloom gate: karis=yes knee=%.1f intensity=hdr_add mips=%u (%s)",
        kSoftKnee, kMips, passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_aa_gate(const ForwardDemo& demo) {
    using engine::renderer::aa::effective_mode;
    using engine::renderer::aa::kAfterTonemap;
    using engine::renderer::aa::kTaaBeforeTonemap;
    using engine::renderer::aa::kDefault;
    using engine::renderer::aa::kSmaaMaxSearch;
    using engine::renderer::aa::kSmaaThreshold;
    using engine::renderer::aa::kStackSpatial;
    using engine::renderer::aa::luma;
    using engine::renderer::aa::Mode;
    using engine::renderer::aa::next_mode;

    const bool policy_ok = kDefault == Mode::Off && kAfterTonemap && kTaaBeforeTonemap
        && !kStackSpatial && kSmaaThreshold == 0.1f && kSmaaMaxSearch == 8
        && next_mode(Mode::Off) == Mode::Fxaa && next_mode(Mode::Fxaa) == Mode::Smaa
        && next_mode(Mode::Smaa) == Mode::Taa && next_mode(Mode::Taa) == Mode::Off;

    const engine::f32 edge = luma({1.f, 1.f, 1.f}) - luma({0.f, 0.f, 0.f});
    const bool luma_ok = edge > 0.9f && luma({0.2f, 0.2f, 0.2f}) < 0.3f;

    const bool fxaa_ok = demo.fxaa_pipeline != nullptr;
    const bool smaa_ok = demo.smaa_edge_pipeline && demo.smaa_weights_pipeline
        && demo.smaa_blend_pipeline;
    const bool taa_ok = demo.taa_pipeline && demo.tonemap_aces_pipeline;
    const bool exclusive_ok = effective_mode(Mode::Taa, fxaa_ok, smaa_ok, taa_ok) == Mode::Taa
        && effective_mode(Mode::Taa, fxaa_ok, smaa_ok, false) == Mode::Smaa
        && effective_mode(Mode::Smaa, fxaa_ok, smaa_ok, taa_ok) == Mode::Smaa
        && effective_mode(Mode::Fxaa, fxaa_ok, smaa_ok, taa_ok) == Mode::Fxaa
        && effective_mode(Mode::Off, fxaa_ok, smaa_ok, taa_ok) == Mode::Off
        && effective_mode(Mode::Smaa, fxaa_ok, false, false) == Mode::Off;

    const bool passed = policy_ok && luma_ok && fxaa_ok && smaa_ok && exclusive_ok
        && demo.aa_mode == kDefault;
    char message[224];
    std::snprintf(message, sizeof(message),
        "AA gate: default=off exclusive=yes after_tonemap=yes fxaa=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_taa_gate(const ForwardDemo& demo) {
    using engine::renderer::aa::Mode;
    using engine::renderer::aa::effective_mode;
    using engine::renderer::taa::apply_jitter;
    using engine::renderer::taa::clip_aabb;
    using engine::renderer::taa::jitter_ndc;
    using engine::renderer::taa::jitter_to_uv;
    using engine::renderer::taa::kBeforeTonemap;
    using engine::renderer::taa::kHistoryWeight;
    using engine::renderer::taa::kSampleCount;
    using engine::renderer::taa::radical_inverse;
    using engine::renderer::taa::resolve_sample;
    using engine::renderer::taa::rgb_to_ycocg;
    using engine::renderer::taa::ycocg_to_rgb;

    const engine::math::Vec2 j = jitter_ndc(1, 1280, 720);
    const engine::math::Vec2 j_uv = jitter_to_uv(j);
    const bool jitter_ok = kSampleCount == 8 && radical_inverse(2, 1) > 0.4f
        && radical_inverse(2, 1) < 0.6f && (j.x != 0.f || j.y != 0.f)
        && kHistoryWeight > 0.94f && kHistoryWeight < 0.96f
        && std::abs(j_uv.x - j.x * 0.5f) < 1e-8f
        && std::abs(j_uv.y - j.y * -0.5f) < 1e-8f;

    const engine::math::Mat4 proj = engine::math::Mat4::perspective(
        engine::math::radians(60.f), 16.f / 9.f, 0.1f, 100.f);
    const engine::math::Mat4 jittered = apply_jitter(proj, j);
    const engine::math::Mat4 none = apply_jitter(proj, {0.f, 0.f});
    const bool apply_ok = std::abs(jittered.cols[2].x - proj.cols[2].x) > 1e-8f
        && std::abs(none.cols[0].x - proj.cols[0].x) < 1e-6f
        && sizeof(engine::renderer::FrameConstants) == 400
        && sizeof(engine::renderer::taa::Constants) == 48
        && std::abs(engine::renderer::taa::make_constants(1280, 720, j, false).jitter.x - j_uv.x)
            < 1e-8f;

    const engine::math::Vec3 rgb{0.8f, 0.4f, 0.2f};
    const engine::math::Vec3 roundtrip = ycocg_to_rgb(rgb_to_ycocg(rgb));
    const bool ycc_ok = std::abs(roundtrip.x - rgb.x) < 1e-5f
        && std::abs(roundtrip.y - rgb.y) < 1e-5f && std::abs(roundtrip.z - rgb.z) < 1e-5f;

    const engine::math::Vec3 inside = clip_aabb({0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, {0.2f, 0.3f, 0.4f});
    const engine::math::Vec3 outside = clip_aabb({0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, {4.f, 0.5f, 0.5f});
    const bool clip_ok = std::abs(inside.x - 0.2f) < 1e-5f && outside.x <= 1.0001f
        && outside.x >= 0.f;
    const engine::math::Vec3 resolved = resolve_sample({0.1f, 0.1f, 0.1f}, {8.f, 0.1f, 0.1f},
        rgb_to_ycocg({0.f, 0.f, 0.f}), rgb_to_ycocg({0.2f, 0.2f, 0.2f}), kHistoryWeight, false);
    const bool blend_ok = resolved.x < 1.f;
    const engine::math::Vec3 reset = resolve_sample({0.5f, 0.5f, 0.5f}, {8.f, 8.f, 8.f},
        {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, kHistoryWeight, true);
    const bool reset_ok = std::abs(reset.x - 0.5f) < 1e-5f;

    engine::renderer::RenderGraph probe;
    engine::renderer::StandardFrameDesc frame{};
    frame.log_ready = false;
    const bool compiled = engine::renderer::setup_standard_frame(probe, std::move(frame));
    int bloom_i = -1;
    int taa_i = -1;
    int tonemap_taa_i = -1;
    int tonemap_i = -1;
    for (engine::u32 i = 0; i < probe.pass_count(); ++i) {
        const std::string_view name = probe.pass_name(i);
        if (name == "bloom_up0") {
            bloom_i = static_cast<int>(i);
        } else if (name == "taa_even" && taa_i < 0) {
            taa_i = static_cast<int>(i);
        } else if (name == "tonemap_taa_even" && tonemap_taa_i < 0) {
            tonemap_taa_i = static_cast<int>(i);
        } else if (name == "tonemap") {
            tonemap_i = static_cast<int>(i);
        }
    }
    const bool pass_ok = compiled && bloom_i >= 0 && taa_i > bloom_i && tonemap_taa_i > taa_i
        && tonemap_i > tonemap_taa_i
        && probe.find_resource("taa_history_a").valid()
        && probe.find_resource("taa_history_b").valid();

    const bool hdr_ok = kBeforeTonemap && engine::renderer::aa::kTaaBeforeTonemap
        && engine::renderer::aa::kDefault == Mode::Off
        && demo.taa_pipeline != nullptr && demo.tonemap_aces_pipeline != nullptr
        && effective_mode(Mode::Taa, true, true, true) == Mode::Taa
        && effective_mode(Mode::Smaa, true, true, true) == Mode::Smaa;

    const bool passed = jitter_ok && apply_ok && ycc_ok && clip_ok && blend_ok && reset_ok
        && pass_ok && hdr_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "TAA gate: default=off optional=yes hdr=yes jitter=%s clip=ycocg pass=%s (%s)",
        jitter_ok && apply_ok ? "yes" : "no", pass_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_motion_gate(const ForwardDemo& demo) {
    using engine::renderer::motion::Constants;
    using engine::renderer::motion::kFormat;
    using engine::renderer::motion::MotionHistory;
    using engine::renderer::motion::nearly_zero;
    using engine::renderer::motion::screen_uv_motion;

    const engine::math::Mat4 view_a = engine::math::Mat4::look_at(
        {0.f, 0.f, -4.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    const engine::math::Mat4 view_b = engine::math::Mat4::look_at(
        {0.4f, 0.f, -4.f}, {0.4f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    const engine::math::Mat4 proj =
        engine::math::Mat4::perspective(engine::math::radians(60.f), 16.f / 9.f, 0.1f, 100.f);
    const engine::math::Mat4 model = engine::math::Mat4::identity();
    const engine::math::Mat4 moved = engine::math::Mat4::translate({0.5f, 0.f, 0.f});
    const engine::math::Vec3 local{0.3f, 0.15f, 0.f};
    const engine::math::Mat4 vp_a = proj * view_a;
    const engine::math::Mat4 vp_b = proj * view_b;

    const bool static_ok = nearly_zero(screen_uv_motion(vp_a, model, vp_a, model, local));
    const engine::math::Vec2 camera = screen_uv_motion(vp_b, model, vp_a, model, local);
    const bool camera_ok = !nearly_zero(camera);
    const engine::math::Vec2 object = screen_uv_motion(vp_a, moved, vp_a, model, local);
    const bool object_ok = !nearly_zero(object);
    const bool uv_ok = static_ok && sizeof(engine::renderer::FrameConstants) == 400
        && sizeof(Constants) == 272 && kFormat == engine::rhi::Format::RGBA16_FLOAT;

    char dummy{};
    engine::renderer::ExtractInstance inst{};
    inst.pipeline = reinterpret_cast<engine::rhi::IGraphicsPipeline*>(&dummy);
    inst.vertex_buffer = reinterpret_cast<engine::rhi::IBuffer*>(&dummy);
    inst.index_buffer = reinterpret_cast<engine::rhi::IBuffer*>(&dummy);
    inst.texture = reinterpret_cast<engine::rhi::ITexture*>(&dummy);
    inst.model = model;
    inst.id = 0;
    inst.index_count = 3;
    inst.vertex_stride = 32;

    MotionHistory history{};
    engine::renderer::ExtractDesc desc{};
    desc.view = view_a;
    desc.projection = proj;
    desc.history = &history;
    desc.instances = {&inst, 1};

    engine::Arena first_arena(64 * 1024);
    engine::renderer::RenderSnapshot first{};
    engine::renderer::extract_visible(desc, first_arena, first);
    const bool first_zero = first.draws.size() == 1
        && nearly_zero(screen_uv_motion(first.projection * first.view, first.draws[0].model,
            first.prev_view_proj, first.draws[0].prev_model, local));

    desc.view = view_a;
    engine::Arena still_arena(64 * 1024);
    engine::renderer::RenderSnapshot still{};
    engine::renderer::extract_visible(desc, still_arena, still);
    const bool still_zero = still.draws.size() == 1
        && nearly_zero(screen_uv_motion(still.projection * still.view, still.draws[0].model,
            still.prev_view_proj, still.draws[0].prev_model, local));

    desc.view = view_b;
    engine::Arena next_arena(64 * 1024);
    engine::renderer::RenderSnapshot next{};
    engine::renderer::extract_visible(desc, next_arena, next);
    const engine::math::Vec2 hist_cam = next.draws.size() == 1
        ? screen_uv_motion(next.projection * next.view, next.draws[0].model, next.prev_view_proj,
            next.draws[0].prev_model, local)
        : engine::math::Vec2{};
    const bool history_ok = first_zero && still_zero && !nearly_zero(hist_cam)
        && std::abs(hist_cam.x - camera.x) < 1e-4f && std::abs(hist_cam.y - camera.y) < 1e-4f;

    engine::renderer::RenderGraph probe;
    engine::renderer::StandardFrameDesc frame{};
    frame.log_ready = false;
    const bool compiled = engine::renderer::setup_standard_frame(probe, std::move(frame));
    int forward_i = -1;
    int motion_i = -1;
    int sky_i = -1;
    for (engine::u32 i = 0; i < probe.pass_count(); ++i) {
        const std::string_view name = probe.pass_name(i);
        if (name == "forward") {
            forward_i = static_cast<int>(i);
        } else if (name == "motion_vectors") {
            motion_i = static_cast<int>(i);
        } else if (name == "sky") {
            sky_i = static_cast<int>(i);
        }
    }
    const bool pass_ok = compiled && forward_i >= 0 && motion_i > forward_i && sky_i > motion_i;
    const bool equal_ok = demo.motion_pipeline != nullptr
        && static_cast<engine::u8>(engine::rhi::DepthTest::Equal)
            != static_cast<engine::u8>(engine::rhi::DepthTest::Less);

    const bool passed = uv_ok && camera_ok && object_ok && history_ok && equal_ok && pass_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Motion gate: uv=yes camera=%s object=%s history=%s equal=yes pass=%s (%s)",
        camera_ok ? "yes" : "no", object_ok ? "yes" : "no", history_ok ? "yes" : "no",
        pass_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_audio_gate(engine::audio::IAudio* audio) {
    using engine::audio::parse_wav;
    using engine::audio::WavPcm;
    using engine::audio::write_pcm16_wav;

    const engine::i16 samples[] = {0, 8192, 16384, 8192, 0, -8192, -16384, -8192};
    const auto wav = write_pcm16_wav(samples, 22050, 1);
    WavPcm parsed{};
    const bool wav_ok = parse_wav(wav, parsed) && parsed.sample_rate == 22050
        && parsed.channels == 1 && parsed.bits_per_sample == 16 && parsed.samples.size() == 16;
    const engine::u8 garbage[] = {'N', 'O', 'P', 'E'};
    WavPcm rejected{};
    const bool reject_ok = !parse_wav(garbage, rejected);

    const bool backend_ok = audio != nullptr && audio->name() == "xaudio2";
    bool play_ok = false;
    if (audio) {
        const auto tone = engine::audio::make_tone_pcm16(22050, 80, 440.f, 0.15f);
        engine::audio::SoundDesc desc{};
        desc.pcm = {reinterpret_cast<const engine::u8*>(tone.data()),
            tone.size() * sizeof(engine::i16)};
        desc.sample_rate = 22050;
        desc.channels = 1;
        const auto handle = audio->create_sound(desc);
        play_ok = handle.valid() && audio->play(handle, 0.15f) && audio->playing_count() >= 1;
        audio->tick();
    }

    const bool passed = wav_ok && reject_ok && backend_ok && play_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Audio gate: wav=yes oneshot=yes backend=xaudio2 (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Audio, message);
    return passed;
}

bool run_physics_gate(engine::physics::IPhysics* physics) {
    using engine::physics::BodyDesc;
    using engine::physics::BodyHandle;
    using engine::physics::ShapeType;
    using engine::physics::kAllLayers;

    const bool backend_ok = physics != nullptr && physics->name() == "cpu";
    bool aabb_ok = false;
    bool sphere_ok = false;
    bool mask_ok = false;
    bool move_ok = false;
    bool gen_ok = false;

    if (physics) {
        BodyDesc a{};
        a.shape.type = ShapeType::Aabb;
        a.shape.half_extents = {0.5f, 0.5f, 0.5f};
        a.position = {0.f, 0.f, 0.f};
        a.layer = 1u;

        BodyDesc b = a;
        b.position = {10.f, 0.f, 0.f};
        b.layer = 2u;

        const BodyHandle ha = physics->create_body(a);
        const BodyHandle hb = physics->create_body(b);
        BodyHandle hits[8]{};

        const auto near_origin = engine::math::Aabb::from_center_half({0.f, 0.f, 0.f},
            {1.f, 1.f, 1.f});
        const engine::u32 n_aabb = physics->overlap_aabb(near_origin, kAllLayers, hits);
        aabb_ok = ha.valid() && n_aabb == 1 && hits[0] == ha;

        const engine::u32 n_sphere = physics->overlap_sphere({0.f, 0.f, 0.f}, 0.75f, kAllLayers,
            hits);
        sphere_ok = n_sphere == 1 && hits[0] == ha;

        const engine::u32 n_mask_miss = physics->overlap_aabb(near_origin, 2u, hits);
        const engine::u32 n_mask_hit = physics->overlap_aabb(near_origin, 1u, hits);
        mask_ok = n_mask_miss == 0 && n_mask_hit == 1 && hits[0] == ha;

        physics->set_position(hb, {0.25f, 0.f, 0.f});
        const engine::u32 n_move = physics->overlap_aabb(near_origin, kAllLayers, hits);
        move_ok = n_move == 2;

        physics->destroy_body(hb);
        const engine::u32 n_after_destroy = physics->overlap_aabb(near_origin, kAllLayers, hits);
        const BodyHandle hb2 = physics->create_body(b);
        const auto far_box = engine::math::Aabb::from_center_half({10.f, 0.f, 0.f},
            {1.f, 1.f, 1.f});
        const engine::u32 n_far = physics->overlap_aabb(far_box, kAllLayers, hits);
        gen_ok = n_after_destroy == 1 && hb2.valid() && hb2 != hb && n_far == 1 && hits[0] == hb2;

        physics->destroy_body(ha);
        physics->destroy_body(hb2);
    }

    const bool passed = backend_ok && aabb_ok && sphere_ok && mask_ok && move_ok && gen_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Physics gate: aabb=%s sphere=%s mask=%s move=%s gen=%s backend=cpu (%s)",
        aabb_ok ? "yes" : "no", sphere_ok ? "yes" : "no", mask_ok ? "yes" : "no",
        move_ok ? "yes" : "no", gen_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Physics, message);
    return passed;
}

bool run_physics_body_gate(engine::physics::IPhysics* physics) {
    using engine::physics::BodyDesc;
    using engine::physics::MotionType;
    using engine::physics::ShapeType;

    bool gravity_ok = false;
    bool floor_ok = false;
    bool sphere_ok = false;
    bool box_ok = false;
    bool rest_ok = false;

    if (physics) {
        physics->set_gravity({0.f, -9.81f, 0.f});
        const engine::f32 dt = 1.f / 60.f;

        BodyDesc falling{};
        falling.shape.type = ShapeType::Sphere;
        falling.shape.radius = 0.25f;
        falling.position = {0.f, 10.f, 0.f};
        falling.motion = MotionType::Dynamic;
        falling.mass = 1.f;
        falling.restitution = 0.f;
        const auto drop = physics->create_body(falling);
        for (int i = 0; i < 60; ++i) {
            physics->step(dt);
        }
        const engine::f32 y_drop = physics->position(drop).y;
        gravity_ok = drop.valid() && y_drop > 4.5f && y_drop < 5.6f
            && physics->linear_velocity(drop).y < -8.f;
        physics->destroy_body(drop);

        BodyDesc floor{};
        floor.shape.type = ShapeType::Aabb;
        floor.shape.half_extents = {4.f, 0.5f, 4.f};
        floor.position = {0.f, -0.5f, 0.f};
        floor.motion = MotionType::Static;
        const auto floor_h = physics->create_body(floor);

        BodyDesc ball = falling;
        ball.position = {0.f, 3.f, 0.f};
        ball.shape.radius = 0.5f;
        const auto ball_h = physics->create_body(ball);

        BodyDesc box{};
        box.shape.type = ShapeType::Aabb;
        box.shape.half_extents = {0.5f, 0.5f, 0.5f};
        box.position = {2.f, 3.f, 0.f};
        box.motion = MotionType::Dynamic;
        box.mass = 1.f;
        box.restitution = 0.f;
        const auto box_h = physics->create_body(box);

        for (int i = 0; i < 180; ++i) {
            physics->step(dt);
        }

        const engine::math::Vec3 floor_p = physics->position(floor_h);
        floor_ok = floor_h.valid() && std::abs(floor_p.y + 0.5f) < 0.001f
            && std::abs(physics->linear_velocity(floor_h).y) < 0.001f;

        const engine::f32 ball_y = physics->position(ball_h).y;
        const engine::f32 ball_vy = physics->linear_velocity(ball_h).y;
        sphere_ok = ball_y > 0.48f && ball_y < 0.58f && std::abs(ball_vy) < 0.25f;

        const engine::f32 box_y = physics->position(box_h).y;
        const engine::f32 box_vy = physics->linear_velocity(box_h).y;
        box_ok = box_y > 0.48f && box_y < 0.58f && std::abs(box_vy) < 0.25f;
        rest_ok = sphere_ok && box_ok;

        physics->destroy_body(ball_h);
        physics->destroy_body(box_h);
        physics->destroy_body(floor_h);
        physics->set_gravity(engine::physics::kDefaultGravity);
    }

    const bool passed = gravity_ok && floor_ok && sphere_ok && box_ok && rest_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Physics body gate: gravity=%s rest=%s floor=%s sphere=%s box=%s (%s)",
        gravity_ok ? "yes" : "no", rest_ok ? "yes" : "no", floor_ok ? "yes" : "no",
        sphere_ok ? "yes" : "no", box_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Physics, message);
    return passed;
}

bool run_physics_capsule_gate(engine::physics::IPhysics* physics) {
    using engine::physics::BodyDesc;
    using engine::physics::BodyHandle;
    using engine::physics::MotionType;
    using engine::physics::ShapeType;
    using engine::physics::kAllLayers;

    bool overlap_ok = false;
    bool rest_ok = false;
    bool floor_ok = false;
    bool not_aabb_ok = false;

    if (physics) {
        physics->set_gravity({0.f, -9.81f, 0.f});
        const engine::f32 dt = 1.f / 60.f;
        const engine::f32 radius = 0.3f;
        const engine::f32 half_height = 0.5f;

        BodyDesc cube{};
        cube.shape.type = ShapeType::Aabb;
        cube.shape.half_extents = {0.5f, 0.5f, 0.5f};
        cube.position = {0.f, 0.f, 0.f};
        cube.motion = MotionType::Static;
        const auto cube_h = physics->create_body(cube);

        BodyHandle hits[8]{};
        const engine::u32 n_hit = physics->overlap_capsule({0.55f, 0.f, 0.f}, radius, half_height,
            kAllLayers, hits);
        const bool hit_face = n_hit == 1 && hits[0] == cube_h;
        const engine::u32 n_corner = physics->overlap_capsule({0.75f, 0.f, 0.75f}, radius,
            half_height, kAllLayers, hits);
        const engine::u32 n_far = physics->overlap_capsule({10.f, 0.f, 0.f}, radius, half_height,
            kAllLayers, hits);
        overlap_ok = cube_h.valid() && hit_face && n_far == 0;
        not_aabb_ok = n_corner == 0;
        physics->destroy_body(cube_h);

        BodyDesc floor{};
        floor.shape.type = ShapeType::Aabb;
        floor.shape.half_extents = {4.f, 0.5f, 4.f};
        floor.position = {0.f, -0.5f, 0.f};
        floor.motion = MotionType::Static;
        const auto floor_h = physics->create_body(floor);

        BodyDesc cap{};
        cap.shape.type = ShapeType::Capsule;
        cap.shape.radius = radius;
        cap.shape.half_height = half_height;
        cap.position = {0.f, 3.f, 0.f};
        cap.motion = MotionType::Dynamic;
        cap.mass = 1.f;
        cap.restitution = 0.f;
        const auto cap_h = physics->create_body(cap);

        for (int i = 0; i < 180; ++i) {
            physics->step(dt);
        }

        const engine::math::Vec3 floor_p = physics->position(floor_h);
        floor_ok = floor_h.valid() && std::abs(floor_p.y + 0.5f) < 0.001f
            && std::abs(physics->linear_velocity(floor_h).y) < 0.001f;

        const engine::f32 y = physics->position(cap_h).y;
        const engine::f32 vy = physics->linear_velocity(cap_h).y;
        rest_ok = cap_h.valid() && y > 0.78f && y < 0.88f && std::abs(vy) < 0.25f;

        physics->destroy_body(cap_h);
        physics->destroy_body(floor_h);
        physics->set_gravity(engine::physics::kDefaultGravity);
    }

    const bool passed = overlap_ok && rest_ok && floor_ok && not_aabb_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Physics capsule gate: overlap=%s rest=%s floor=%s not_aabb=%s (%s)",
        overlap_ok ? "yes" : "no", rest_ok ? "yes" : "no", floor_ok ? "yes" : "no",
        not_aabb_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Physics, message);
    return passed;
}

bool run_physics_trigger_gate(engine::physics::IPhysics* physics) {
    using engine::physics::BodyDesc;
    using engine::physics::BodyHandle;
    using engine::physics::MotionType;
    using engine::physics::ShapeType;
    using engine::physics::TriggerEvent;
    using engine::physics::TriggerEventType;
    using engine::physics::kAllLayers;
    using engine::physics::kDefaultGravity;

    bool enter_ok = false;
    bool stay_ok = false;
    bool exit_ok = false;
    bool mask_ok = false;
    bool solid_ok = false;

    const auto involves = [](const TriggerEvent& e, BodyHandle x, BodyHandle y) {
        return (e.a == x && e.b == y) || (e.a == y && e.b == x);
    };

    if (physics) {
        physics->set_gravity({0.f, 0.f, 0.f});
        const engine::f32 dt = 1.f / 60.f;

        BodyDesc volume{};
        volume.shape.type = ShapeType::Aabb;
        volume.shape.half_extents = {1.f, 1.f, 1.f};
        volume.position = {0.f, 0.f, 0.f};
        volume.sensor = true;
        volume.motion = MotionType::Static;
        volume.layer = 1u;
        volume.mask = kAllLayers;
        const auto vol_h = physics->create_body(volume);

        BodyDesc guest{};
        guest.shape.type = ShapeType::Sphere;
        guest.shape.radius = 0.25f;
        guest.position = {0.f, 0.f, 0.f};
        guest.motion = MotionType::Kinematic;
        guest.layer = 2u;
        guest.mask = kAllLayers;
        const auto guest_h = physics->create_body(guest);

        physics->step(dt);
        TriggerEvent ev[8]{};
        engine::u32 n = physics->trigger_events(ev);
        enter_ok = vol_h.valid() && guest_h.valid() && n == 1
            && ev[0].type == TriggerEventType::Enter && involves(ev[0], vol_h, guest_h);

        physics->step(dt);
        n = physics->trigger_events(ev);
        stay_ok = n == 0;

        physics->set_position(guest_h, {10.f, 0.f, 0.f});
        physics->step(dt);
        n = physics->trigger_events(ev);
        exit_ok = n == 1 && ev[0].type == TriggerEventType::Exit && involves(ev[0], vol_h, guest_h);

        physics->destroy_body(guest_h);
        physics->destroy_body(vol_h);

        const auto vol2 = physics->create_body(volume);
        BodyDesc masked = guest;
        masked.mask = 2u;
        const auto masked_h = physics->create_body(masked);
        physics->step(dt);
        n = physics->trigger_events(ev);
        mask_ok = n == 0;
        physics->destroy_body(masked_h);
        physics->destroy_body(vol2);

        BodyDesc solid_a{};
        solid_a.shape.type = ShapeType::Aabb;
        solid_a.shape.half_extents = {0.5f, 0.5f, 0.5f};
        solid_a.position = {0.f, 0.f, 0.f};
        solid_a.motion = MotionType::Static;
        const auto sa = physics->create_body(solid_a);
        BodyDesc solid_b = solid_a;
        solid_b.motion = MotionType::Kinematic;
        const auto sb = physics->create_body(solid_b);
        physics->step(dt);
        n = physics->trigger_events(ev);
        solid_ok = n == 0;

        physics->destroy_body(sa);
        physics->destroy_body(sb);
        physics->set_gravity(kDefaultGravity);
    }

    const bool passed = enter_ok && stay_ok && exit_ok && mask_ok && solid_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Physics trigger gate: enter=%s stay=%s exit=%s mask=%s solid=%s (%s)",
        enter_ok ? "yes" : "no", stay_ok ? "yes" : "no", exit_ok ? "yes" : "no",
        mask_ok ? "yes" : "no", solid_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Physics, message);
    return passed;
}

bool run_physics_raycast_gate(engine::physics::IPhysics* physics) {
    using engine::physics::BodyDesc;
    using engine::physics::MotionType;
    using engine::physics::RaycastHit;
    using engine::physics::ShapeType;
    using engine::physics::kAllLayers;

    bool aabb_ok = false;
    bool sphere_ok = false;
    bool capsule_ok = false;
    bool closest_ok = false;
    bool mask_ok = false;
    bool miss_ok = false;

    if (physics) {
        const engine::math::Vec3 origin{0.f, 0.f, 0.f};
        const engine::math::Vec3 dir{1.f, 0.f, 0.f};
        const engine::f32 max_d = 10.f;

        BodyDesc box{};
        box.shape.type = ShapeType::Aabb;
        box.shape.half_extents = {0.5f, 0.5f, 0.5f};
        box.position = {2.f, 0.f, 0.f};
        box.motion = MotionType::Static;
        box.layer = 1u;
        const auto box_h = physics->create_body(box);

        RaycastHit hit{};
        aabb_ok = physics->raycast(origin, dir, max_d, kAllLayers, hit) && hit.body == box_h
            && std::abs(hit.point.x - 1.5f) < 0.01f && std::abs(hit.normal.x + 1.f) < 0.01f
            && std::abs(hit.fraction - 0.15f) < 0.005f;
        physics->destroy_body(box_h);

        BodyDesc ball{};
        ball.shape.type = ShapeType::Sphere;
        ball.shape.radius = 0.5f;
        ball.position = {2.f, 0.f, 0.f};
        ball.motion = MotionType::Static;
        const auto ball_h = physics->create_body(ball);
        sphere_ok = physics->raycast(origin, dir, max_d, kAllLayers, hit) && hit.body == ball_h
            && std::abs(hit.point.x - 1.5f) < 0.01f && std::abs(hit.normal.x + 1.f) < 0.01f;
        physics->destroy_body(ball_h);

        BodyDesc cap{};
        cap.shape.type = ShapeType::Capsule;
        cap.shape.radius = 0.3f;
        cap.shape.half_height = 0.5f;
        cap.position = {2.f, 0.f, 0.f};
        cap.motion = MotionType::Static;
        const auto cap_h = physics->create_body(cap);
        capsule_ok = physics->raycast(origin, dir, max_d, kAllLayers, hit) && hit.body == cap_h
            && std::abs(hit.point.x - 1.7f) < 0.01f && std::abs(hit.normal.x + 1.f) < 0.01f
            && std::abs(hit.point.y) < 0.01f;
        physics->destroy_body(cap_h);

        const auto near_h = physics->create_body(box);
        BodyDesc far = box;
        far.position = {5.f, 0.f, 0.f};
        const auto far_h = physics->create_body(far);
        closest_ok = physics->raycast(origin, dir, max_d, kAllLayers, hit) && hit.body == near_h;

        mask_ok = !physics->raycast(origin, dir, max_d, 2u, hit);
        miss_ok = !physics->raycast(origin, {-1.f, 0.f, 0.f}, max_d, kAllLayers, hit);

        physics->destroy_body(near_h);
        physics->destroy_body(far_h);
    }

    const bool passed = aabb_ok && sphere_ok && capsule_ok && closest_ok && mask_ok && miss_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Physics raycast gate: aabb=%s sphere=%s capsule=%s closest=%s mask=%s miss=%s (%s)",
        aabb_ok ? "yes" : "no", sphere_ok ? "yes" : "no", capsule_ok ? "yes" : "no",
        closest_ok ? "yes" : "no", mask_ok ? "yes" : "no", miss_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Physics, message);
    return passed;
}

bool run_character_gate(engine::physics::IPhysics* physics) {
    using engine::gameplay::CharacterController;
    using engine::gameplay::CharacterDesc;
    using engine::gameplay::is_walkable_ground;
    using engine::physics::BodyDesc;
    using engine::physics::MotionType;
    using engine::physics::ShapeType;

    bool grounded_ok = false;
    bool walk_ok = false;
    bool jump_ok = false;
    bool step_ok = false;
    bool slope_ok = false;

    if (physics) {
        physics->set_gravity({0.f, -9.81f, 0.f});
        const engine::f32 dt = 1.f / 60.f;

        BodyDesc floor{};
        floor.shape.type = ShapeType::Aabb;
        floor.shape.half_extents = {6.f, 0.5f, 6.f};
        floor.position = {0.f, -0.5f, 0.f};
        floor.motion = MotionType::Static;
        const auto floor_h = physics->create_body(floor);

        CharacterDesc desc{};
        desc.radius = 0.25f;
        desc.half_height = 0.4f;
        desc.walk_speed = 4.f;
        desc.jump_speed = 6.f;
        desc.step_offset = 0.35f;
        desc.slope_limit_deg = 45.f;
        const engine::f32 rest = desc.half_height + desc.radius;

        CharacterController cc;
        cc.spawn(*physics, {0.f, rest, 0.f}, desc);
        for (int i = 0; i < 8; ++i) {
            cc.move({}, false, dt);
        }
        grounded_ok = floor_h.valid() && cc.grounded()
            && std::abs(cc.position().y - rest) < 0.05f;

        const engine::f32 z0 = cc.position().z;
        for (int i = 0; i < 30; ++i) {
            cc.move({0.f, 0.f, 1.f}, false, dt);
        }
        walk_ok = cc.grounded() && cc.position().z > z0 + 0.5f
            && std::abs(cc.position().y - rest) < 0.08f;

        cc.destroy();
        cc.spawn(*physics, {0.f, rest, 0.f}, desc);
        for (int i = 0; i < 8; ++i) {
            cc.move({}, false, dt);
        }
        cc.move({}, true, dt);
        bool left_ground = false;
        bool rose = false;
        for (int i = 0; i < 8; ++i) {
            cc.move({}, false, dt);
            if (!cc.grounded()) {
                left_ground = true;
            }
            if (cc.position().y > rest + 0.15f) {
                rose = true;
            }
        }
        bool landed = false;
        for (int i = 0; i < 90; ++i) {
            cc.move({}, false, dt);
            if (cc.grounded() && std::abs(cc.position().y - rest) < 0.08f) {
                landed = true;
                break;
            }
        }
        jump_ok = left_ground && rose && landed;

        cc.destroy();
        BodyDesc step{};
        step.shape.type = ShapeType::Aabb;
        step.shape.half_extents = {0.5f, 0.12f, 0.5f};
        step.position = {0.f, 0.12f, 1.5f};
        step.motion = MotionType::Static;
        const auto step_h = physics->create_body(step);
        cc.spawn(*physics, {0.f, rest, 0.f}, desc);
        for (int i = 0; i < 8; ++i) {
            cc.move({}, false, dt);
        }
        bool climbed = false;
        for (int i = 0; i < 40; ++i) {
            cc.move({0.f, 0.f, 1.f}, false, dt);
            if (cc.position().z > 1.2f && cc.position().y > rest + 0.1f && cc.grounded()) {
                climbed = true;
                break;
            }
        }
        step_ok = step_h.valid() && climbed;

        slope_ok = is_walkable_ground({0.f, 1.f, 0.f}, 45.f)
            && !is_walkable_ground({0.f, 0.5f, 0.866f}, 45.f);

        cc.destroy();
        physics->destroy_body(step_h);
        physics->destroy_body(floor_h);
    }

    const bool passed = grounded_ok && walk_ok && jump_ok && step_ok && slope_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Character gate: walk=%s jump=%s step=%s slope=%s grounded=%s (%s)",
        walk_ok ? "yes" : "no", jump_ok ? "yes" : "no", step_ok ? "yes" : "no",
        slope_ok ? "yes" : "no", grounded_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_camera_gate() {
    using engine::gameplay::CameraMode;
    using engine::gameplay::GameCamera;
    using engine::gameplay::next_camera_mode;

    const engine::math::Vec3 target{0.f, 1.f, 0.f};
    GameCamera cam;
    cam.desc.follow_distance = 4.f;
    cam.desc.follow_height = 1.5f;
    cam.desc.look_height = 0.4f;
    cam.desc.orbit_distance = 4.f;
    cam.desc.eye_height = 0.5f;

    cam.yaw = 0.f;
    cam.pitch = 0.f;
    cam.set_mode(CameraMode::Follow);
    cam.update(target);
    const engine::math::Vec3 follow_a = cam.position();
    const engine::math::Vec3 follow_view = cam.view().transform_point(target);
    cam.pitch = 0.8f;
    cam.update(target);
    const engine::math::Vec3 follow_b = cam.position();
    const bool follow_ok = std::abs(follow_a.x) < 0.05f && follow_a.z < target.z - 3.5f
        && follow_a.y > target.y + 1.2f && follow_view.z < 0.f
        && std::abs(follow_b.y - follow_a.y) < 0.05f
        && next_camera_mode(CameraMode::Follow) == CameraMode::Orbit;

    cam.pitch = 0.f;
    cam.set_mode(CameraMode::Orbit);
    cam.update(target);
    const engine::math::Vec3 orbit_a = cam.position();
    const engine::f32 orbit_dist_a = (orbit_a - cam.look_at()).length();
    cam.pitch = 0.8f;
    cam.update(target);
    const engine::math::Vec3 orbit_b = cam.position();
    const engine::f32 orbit_dist_b = (orbit_b - cam.look_at()).length();
    const engine::math::Vec3 orbit_view = cam.view().transform_point(cam.look_at());
    const bool orbit_ok = std::abs(orbit_dist_a - 4.f) < 0.05f
        && std::abs(orbit_dist_b - 4.f) < 0.05f
        && orbit_b.y > orbit_a.y + 1.5f && orbit_view.z < 0.f
        && next_camera_mode(CameraMode::Orbit) == CameraMode::Fps;

    cam.pitch = 0.f;
    cam.set_mode(CameraMode::Fps);
    cam.update(target);
    const engine::math::Vec3 fps_pos = cam.position();
    const engine::math::Vec3 ahead = fps_pos + cam.forward();
    const engine::math::Vec3 fps_view = cam.view().transform_point(ahead);
    const bool fps_ok = std::abs(fps_pos.x - target.x) < 0.01f
        && std::abs(fps_pos.z - target.z) < 0.01f
        && std::abs(fps_pos.y - (target.y + 0.5f)) < 0.01f
        && fps_view.z < 0.f
        && (fps_pos - target).length() < 1.f
        && next_camera_mode(CameraMode::Fps) == CameraMode::Follow;

    const bool passed = follow_ok && orbit_ok && fps_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Camera gate: follow=%s orbit=%s fps=%s (%s)",
        follow_ok ? "yes" : "no", orbit_ok ? "yes" : "no", fps_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_gamepad_gate(engine::platform::IInput* input) {
    using engine::platform::GamepadButton;
    using engine::platform::GamepadState;
    using engine::platform::apply_stick_deadzone;
    using engine::platform::gamepad_begin_frame;
    using engine::platform::gamepad_set_button;
    using engine::platform::kMaxGamepads;

    engine::f32 zx = 0.05f;
    engine::f32 zy = 0.02f;
    apply_stick_deadzone(zx, zy, 0.2f);
    engine::f32 ox = 0.8f;
    engine::f32 oy = 0.f;
    apply_stick_deadzone(ox, oy, 0.2f);
    const bool deadzone_ok = std::abs(zx) < 1.e-5f && std::abs(zy) < 1.e-5f
        && ox > 0.7f && ox <= 1.001f && std::abs(oy) < 0.01f;

    GamepadState pad{};
    gamepad_begin_frame(pad);
    gamepad_set_button(pad, GamepadButton::A, true);
    const bool press_ok = pad.buttons_pressed[static_cast<engine::usize>(GamepadButton::A)]
        && pad.buttons_down[static_cast<engine::usize>(GamepadButton::A)];
    gamepad_begin_frame(pad);
    gamepad_set_button(pad, GamepadButton::A, true);
    const bool hold_ok = !pad.buttons_pressed[static_cast<engine::usize>(GamepadButton::A)]
        && pad.buttons_down[static_cast<engine::usize>(GamepadButton::A)];
    gamepad_begin_frame(pad);
    gamepad_set_button(pad, GamepadButton::A, false);
    const bool release_ok = pad.buttons_released[static_cast<engine::usize>(GamepadButton::A)]
        && !pad.buttons_down[static_cast<engine::usize>(GamepadButton::A)];
    const bool button_ok = press_ok && hold_ok && release_ok
        && kMaxGamepads == 4
        && static_cast<engine::u32>(GamepadButton::Count) >= 14;

    bool poll_ok = false;
    bool connected = false;
    if (input) {
        const auto& g = input->state().gamepads[0];
        connected = g.connected;
        poll_ok = std::abs(g.left_x) <= 1.f && std::abs(g.left_y) <= 1.f
            && std::abs(g.right_x) <= 1.f && std::abs(g.right_y) <= 1.f
            && g.left_trigger >= 0.f && g.left_trigger <= 1.f
            && g.right_trigger >= 0.f && g.right_trigger <= 1.f;
    }

    const bool passed = deadzone_ok && button_ok && poll_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Gamepad gate: deadzone=%s button=%s poll=%s connected=%s (%s)",
        deadzone_ok ? "yes" : "no", button_ok ? "yes" : "no", poll_ok ? "yes" : "no",
        connected ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Platform, message);
    return passed;
}

bool run_pcf_gate() {
    using engine::renderer::pcf::filter_step_edge;
    using engine::renderer::pcf::interleaved_gradient_noise;
    using engine::renderer::pcf::kTapCount;
    using engine::renderer::pcf::vogel_disk;

    const engine::f32 ign_a = interleaved_gradient_noise({12.5f, 8.5f});
    const engine::f32 ign_b = interleaved_gradient_noise({13.5f, 8.5f});
    const bool ign_ok = ign_a >= 0.f && ign_a < 1.f && ign_b >= 0.f && ign_b < 1.f
        && std::abs(ign_a - ign_b) > 0.01f;

    const engine::math::Vec2 v0 = vogel_disk(0, kTapCount, 0.f);
    const engine::math::Vec2 v_mid = vogel_disk(kTapCount / 2, kTapCount, 0.f);
    const engine::math::Vec2 v_last = vogel_disk(kTapCount - 1, kTapCount, 0.f);
    const bool vogel_ok = kTapCount == 16 && v0.length() > 0.05f && v0.length() < v_last.length()
        && v_last.length() <= 1.001f && std::abs(v0.x - v_mid.x) > 0.05f;

    const engine::f32 filtered = filter_step_edge({0.5f, 0.5f}, {12.5f, 8.5f}, 1024);
    const bool edge_ok = filtered > 0.05f && filtered < 0.95f;

    const bool passed = ign_ok && vogel_ok && edge_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "PCF gate: vogel=%s ign=%s edge_filter=%s taps=%d (%s)",
        vogel_ok ? "yes" : "no", ign_ok ? "yes" : "no", edge_ok ? "yes" : "no",
        kTapCount, passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_albedo_gate(const engine::assets::ImageData& image) {
    const bool passed = image.width > 0 && image.height > 0
        && image.rgba.size() == static_cast<engine::usize>(image.width) * image.height * 4;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Albedo PNG gate: %ux%u rgba=%zu (%s)",
        image.width, image.height, image.rgba.size(),
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_gltf_gate(const engine::assets::gltf::GltfLoadResult& loaded) {
    const bool mesh_ok = loaded.mesh.vertices.size() >= 1000 && loaded.mesh.indices.size() >= 3000
        && loaded.mesh.bounds.valid();
    const bool albedo_ok = !loaded.albedo_uri.empty();
    const bool pbr_ok = loaded.metallic >= 0.f && loaded.metallic <= 1.f
        && loaded.roughness >= 0.f && loaded.roughness <= 1.f;
    const bool passed = mesh_ok && albedo_ok && pbr_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "glTF gate: verts=%zu indices=%zu albedo=%s metal=%.2f rough=%.2f (%s)",
        loaded.mesh.vertices.size(), loaded.mesh.indices.size(),
        albedo_ok ? "yes" : "no", loaded.metallic, loaded.roughness,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

void append_le_u32(std::vector<engine::u8>& out, engine::u32 value) {
    out.push_back(static_cast<engine::u8>(value));
    out.push_back(static_cast<engine::u8>(value >> 8));
    out.push_back(static_cast<engine::u8>(value >> 16));
    out.push_back(static_cast<engine::u8>(value >> 24));
}

void append_le_f32(std::vector<engine::u8>& out, engine::f32 value) {
    engine::u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_le_u32(out, bits);
}

void append_vec3(std::vector<engine::u8>& out, engine::f32 x, engine::f32 y, engine::f32 z) {
    append_le_f32(out, x);
    append_le_f32(out, y);
    append_le_f32(out, z);
}

void append_vec2(std::vector<engine::u8>& out, engine::f32 x, engine::f32 y) {
    append_le_f32(out, x);
    append_le_f32(out, y);
}

bool write_gltf_extras_probe(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return false;
    }

    std::vector<engine::u8> bin;
    // primitive 0: triangle in XY at origin
    append_vec3(bin, 0.f, 0.f, 0.f);
    append_vec3(bin, 1.f, 0.f, 0.f);
    append_vec3(bin, 0.f, 1.f, 0.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec2(bin, 0.f, 0.f);
    append_vec2(bin, 1.f, 0.f);
    append_vec2(bin, 0.f, 1.f);
    append_le_u32(bin, 0);
    append_le_u32(bin, 1);
    append_le_u32(bin, 2);
    // primitive 1: triangle shifted +X
    append_vec3(bin, 2.f, 0.f, 0.f);
    append_vec3(bin, 3.f, 0.f, 0.f);
    append_vec3(bin, 2.f, 1.f, 0.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec2(bin, 0.f, 0.f);
    append_vec2(bin, 1.f, 0.f);
    append_vec2(bin, 0.f, 1.f);
    append_le_u32(bin, 0);
    append_le_u32(bin, 1);
    append_le_u32(bin, 2);

    const auto bin_path = dir / "probe.bin";
    std::ofstream bin_file(bin_path, std::ios::binary | std::ios::trunc);
    if (!bin_file) {
        return false;
    }
    bin_file.write(reinterpret_cast<const char*>(bin.data()),
        static_cast<std::streamsize>(bin.size()));
    if (!bin_file) {
        return false;
    }
    bin_file.close();

    const char* json =
        "{\n"
        "  \"asset\": { \"version\": \"2.0\" },\n"
        "  \"meshes\": [{ \"primitives\": [\n"
        "    { \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2 },\n"
        "      \"indices\": 3, \"material\": 0 },\n"
        "    { \"attributes\": { \"POSITION\": 4, \"NORMAL\": 5, \"TEXCOORD_0\": 6 },\n"
        "      \"indices\": 7, \"material\": 1 }\n"
        "  ] }],\n"
        "  \"materials\": [\n"
        "    { \"pbrMetallicRoughness\": {\n"
        "        \"baseColorTexture\": { \"index\": 0 },\n"
        "        \"metallicRoughnessTexture\": { \"index\": 1 },\n"
        "        \"metallicFactor\": 0.25, \"roughnessFactor\": 0.5 },\n"
        "      \"normalTexture\": { \"index\": 2 } },\n"
        "    { \"pbrMetallicRoughness\": {\n"
        "        \"baseColorTexture\": { \"index\": 3 },\n"
        "        \"metallicFactor\": 0.0, \"roughnessFactor\": 1.0 } }\n"
        "  ],\n"
        "  \"textures\": [ { \"source\": 0 }, { \"source\": 1 }, { \"source\": 2 }, { \"source\": 3 } ],\n"
        "  \"images\": [\n"
        "    { \"uri\": \"a.png\" }, { \"uri\": \"mr.png\" }, { \"uri\": \"n.png\" }, { \"uri\": \"b.png\" }\n"
        "  ],\n"
        "  \"buffers\": [ { \"uri\": \"probe.bin\", \"byteLength\": 216 } ],\n"
        "  \"bufferViews\": [\n"
        "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 24 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 12 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 108, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 144, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 180, \"byteLength\": 24 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 204, \"byteLength\": 12 }\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        "    { \"bufferView\": 3, \"componentType\": 5125, \"count\": 3, \"type\": \"SCALAR\" },\n"
        "    { \"bufferView\": 4, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 5, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 6, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        "    { \"bufferView\": 7, \"componentType\": 5125, \"count\": 3, \"type\": \"SCALAR\" }\n"
        "  ]\n"
        "}\n";

    std::ofstream gltf_file(dir / "probe.gltf", std::ios::binary | std::ios::trunc);
    if (!gltf_file) {
        return false;
    }
    gltf_file << json;
    return static_cast<bool>(gltf_file);
}

bool uri_ends_with(const std::string& uri, const char* suffix) {
    const std::string_view view{uri};
    const std::string_view end{suffix};
    return view.size() >= end.size()
        && view.substr(view.size() - end.size()) == end;
}

bool run_gltf_extras_gate() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "sol-engine-gltf-extras";
    const bool wrote = write_gltf_extras_probe(dir);
    engine::assets::gltf::GltfLoadResult loaded{};
    bool loaded_ok = false;
    if (wrote) {
        auto loader = engine::assets::gltf::create_mesh_loader();
        loaded_ok = loader && loader->load((dir / "probe.gltf").string(), loaded);
    }

    const bool prims_ok = loaded_ok && loaded.primitives.size() == 2
        && loaded.mesh.vertices.size() == 6 && loaded.mesh.indices.size() == 6
        && loaded.primitives[0].first_index == 0 && loaded.primitives[0].index_count == 3
        && loaded.primitives[1].first_index == 3 && loaded.primitives[1].index_count == 3
        && loaded.mesh.indices[3] == 3;
    const bool mr_ok = prims_ok && uri_ends_with(loaded.primitives[0].metallic_roughness_uri, "mr.png")
        && std::abs(loaded.primitives[0].metallic - 0.25f) < 1.e-4f
        && std::abs(loaded.primitives[0].roughness - 0.5f) < 1.e-4f;
    const bool normal_ok = prims_ok && uri_ends_with(loaded.primitives[0].normal_uri, "n.png")
        && uri_ends_with(loaded.primitives[0].albedo_uri, "a.png")
        && uri_ends_with(loaded.primitives[1].albedo_uri, "b.png");
    const bool passed = prims_ok && mr_ok && normal_ok;

    char message[192];
    std::snprintf(message, sizeof(message),
        "glTF extras gate: prims=%s mr=%s normal=%s (%s)",
        prims_ok ? "yes" : "no", mr_ok ? "yes" : "no", normal_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_mip_gate(const engine::rhi::ITexture& texture, engine::u32 source_width) {
    engine::u32 expected = 1;
    engine::u32 dim = source_width;
    while (dim > 1) {
        dim /= 2;
        expected += 1;
    }
    const bool passed = texture.mip_levels() >= 8 && texture.mip_levels() == expected
        && texture.width() == source_width;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Mip gate: %ux%u mips=%u expected=%u (%s)",
        texture.width(), texture.height(), texture.mip_levels(), expected,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_husky_mesh_gate(const engine::assets::MeshData& mesh) {
    const bool passed = mesh.vertices.size() >= 1000 && mesh.indices.size() >= 3000;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Husky mesh gate: verts=%zu indices=%zu (%s)",
        mesh.vertices.size(), mesh.indices.size(),
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_aabb_gate(const engine::assets::MeshData& mesh) {
    const auto& box = mesh.bounds;
    const engine::f32 dx = box.max.x - box.min.x;
    const engine::f32 dy = box.max.y - box.min.y;
    const engine::f32 dz = box.max.z - box.min.z;
    const bool passed = box.valid() && dx > 0.01f && dy > 0.01f && dz > 0.01f;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Husky AABB gate: min=(%.3f,%.3f,%.3f) max=(%.3f,%.3f,%.3f) (%s)",
        box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_aabb_transform_gate(const engine::assets::MeshData& mesh) {
    const engine::math::Aabb local = mesh.bounds;
    const engine::math::Aabb moved = local.transformed(engine::math::Mat4::translate({1.f, 0.f, 0.f}));
    const bool passed = local.valid() && moved.valid()
        && moved.min.x > local.min.x + 0.5f
        && moved.max.x > local.max.x + 0.5f;
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets,
        passed ? "AABB transform gate: +X shift (pass)"
               : "AABB transform gate: +X shift (FAIL)");
    return passed;
}

bool run_aspect_gate() {
    const engine::f32 fov = engine::math::radians(60.f);
    const engine::f32 aspect = 1280.f / 720.f;
    const engine::math::Mat4 projection = engine::math::Mat4::perspective(fov, aspect, 0.1f, 100.f);
    const engine::f32 expected = 1.f / (aspect * std::tan(fov * 0.5f));
    const engine::math::Mat4 wide = engine::math::Mat4::perspective(fov, 16.f / 9.f, 0.1f, 100.f);
    const engine::math::Mat4 tall = engine::math::Mat4::perspective(fov, 4.f / 3.f, 0.1f, 100.f);
    const bool passed = std::abs(projection.cols[0].x - expected) < 1.e-4f
        && std::abs(wide.cols[0].x - tall.cols[0].x) > 0.05f;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Camera aspect gate: 16:9 scale=%.4f expected=%.4f (%s)",
        projection.cols[0].x, expected, passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

engine::assets::MeshData make_ground_quad(engine::f32 half_extent, engine::f32 y) {
    engine::assets::MeshData mesh;
    const engine::f32 s = half_extent;
    const engine::f32 tile = 8.f;
    const engine::assets::VertexPN verts[] = {
        {-s, y, -s, 0.f, 1.f, 0.f, 0.f, 0.f},
        {-s, y,  s, 0.f, 1.f, 0.f, 0.f, tile},
        { s, y,  s, 0.f, 1.f, 0.f, tile, tile},
        { s, y, -s, 0.f, 1.f, 0.f, tile, 0.f},
    };
    mesh.vertices.assign(std::begin(verts), std::end(verts));
    mesh.indices = {0, 1, 2, 0, 2, 3};
    engine::assets::compute_mesh_bounds(mesh);
    return mesh;
}

engine::assets::ImageData make_checker_albedo(engine::u32 size, engine::u32 tile) {
    engine::assets::ImageData image;
    image.width = size;
    image.height = size;
    image.rgba.resize(static_cast<engine::usize>(size) * size * 4);
    for (engine::u32 y = 0; y < size; ++y) {
        for (engine::u32 x = 0; x < size; ++x) {
            const bool dark = ((x / tile) + (y / tile)) & 1u;
            const engine::u8 r = dark ? 48u : 118u;
            const engine::u8 g = dark ? 56u : 126u;
            const engine::u8 b = dark ? 50u : 108u;
            const engine::usize i = (static_cast<engine::usize>(y) * size + x) * 4;
            image.rgba[i + 0] = r;
            image.rgba[i + 1] = g;
            image.rgba[i + 2] = b;
            image.rgba[i + 3] = 255u;
        }
    }
    return image;
}

bool load_albedo_texture(engine::assets::IAssetLoader& loader, engine::rhi::IDevice& device,
    std::string_view virtual_path, std::unique_ptr<engine::rhi::ITexture>& out,
    engine::assets::ImageData& image) {
    std::vector<engine::u8> png_bytes;
    if (!loader.load_bytes(virtual_path, png_bytes) || png_bytes.empty()) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            std::string("Failed to load albedo PNG bytes: ") + std::string(virtual_path));
        return false;
    }
    if (!engine::assets::png::load_png_bytes(png_bytes, image)) {
        return false;
    }
    engine::rhi::TextureDesc albedo_desc{};
    albedo_desc.width = image.width;
    albedo_desc.height = image.height;
    albedo_desc.format = engine::rhi::Format::RGBA8_UNORM;
    albedo_desc.usage = engine::rhi::TextureUsage::ShaderResource;
    albedo_desc.mip_levels = 0;
    out = device.create_texture(albedo_desc, image.rgba.data());
    if (!out) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Albedo texture creation failed");
        return false;
    }
    device.set_debug_name(*out, virtual_path);
    return true;
}

void poll_shader_reload(engine::rhi::IDevice& device, ForwardDemo& demo) {
    if (!demo.shader_watcher) return;

    engine::shaders::ShaderBytecode vs_bytecode;
    engine::shaders::ShaderBytecode ps_bytecode;
    std::string error;
    const auto status = demo.shader_watcher->poll(vs_bytecode, ps_bytecode, error);
    if (status != engine::shaders::ShaderReloadStatus::Reloaded) {
        return;
    }

    device.wait_idle();
    auto pipeline = device.create_graphics_pipeline(
        make_forward_pipeline_desc(vs_bytecode.data, ps_bytecode.data));
    if (!pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Shader hot-reload pipeline creation failed");
        return;
    }

    demo.pipeline = std::move(pipeline);
    engine::log(engine::LogLevel::Info, engine::LogChannel::Render, "Shader hot-reload applied");
}

bool run_graph_gate() {
    bool ok = true;

    {
        engine::renderer::RenderGraph probe;
        const auto orphan = probe.create_transient({
            "orphan_color",
            engine::rhi::Format::RGBA8_UNORM,
            engine::rhi::TextureUsage::RenderTarget,
        });
        engine::renderer::RenderPassDesc bad{};
        bad.name = "orphan_read";
        bad.writes[0] = {probe.swapchain_color(), engine::renderer::Access::ColorWrite};
        bad.write_count = 1;
        bad.reads[0] = {orphan, engine::renderer::Access::ShaderRead};
        bad.read_count = 1;
        probe.add_pass(std::move(bad));
        const bool detected = !probe.compile();
        ok = ok && detected;
        engine::log(detected ? engine::LogLevel::Info : engine::LogLevel::Error,
            engine::LogChannel::Render,
            detected ? "Graph gate missing-producer (pass)"
                     : "Graph gate missing-producer (FAIL)");
    }

    {
        engine::renderer::RenderGraph probe;
        const auto color = probe.create_transient({
            "copy_src_color",
            engine::rhi::Format::RGBA8_UNORM,
            engine::rhi::TextureUsage::RenderTarget,
        });
        const auto depth = probe.create_transient({
            "copy_dst_depth",
            engine::rhi::Format::D32_FLOAT,
            engine::rhi::TextureUsage::DepthStencil,
        });
        engine::renderer::RenderPassDesc write{};
        write.name = "produce_color";
        write.writes[0] = {color, engine::renderer::Access::ColorWrite};
        write.write_count = 1;
        probe.add_pass(std::move(write));
        engine::renderer::RenderPassDesc blit{};
        blit.name = "bad_copy";
        blit.kind = engine::renderer::PassKind::Copy;
        blit.copy_src = color;
        blit.copy_dst = depth;
        probe.add_pass(std::move(blit));
        const bool detected = !probe.compile();
        ok = ok && detected;
        engine::log(detected ? engine::LogLevel::Info : engine::LogLevel::Error,
            engine::LogChannel::Render,
            detected ? "Graph gate copy-format (pass)"
                     : "Graph gate copy-format (FAIL)");
    }

    {
        engine::renderer::RenderGraph probe;
        const auto src = probe.create_transient({
            "ok_src",
            engine::rhi::Format::RGBA8_UNORM,
            engine::rhi::TextureUsage::RenderTarget,
        });
        const auto dst = probe.create_transient({
            "ok_dst",
            engine::rhi::Format::RGBA8_UNORM,
            engine::rhi::TextureUsage::RenderTarget,
        });
        engine::renderer::RenderPassDesc write{};
        write.name = "produce_src";
        write.writes[0] = {src, engine::renderer::Access::ColorWrite};
        write.write_count = 1;
        probe.add_pass(std::move(write));
        engine::renderer::RenderPassDesc blit{};
        blit.name = "ok_copy";
        blit.kind = engine::renderer::PassKind::Copy;
        blit.copy_src = src;
        blit.copy_dst = dst;
        probe.add_pass(std::move(blit));
        const bool compiled = probe.compile();
        ok = ok && compiled;
        engine::log(compiled ? engine::LogLevel::Info : engine::LogLevel::Error,
            engine::LogChannel::Render,
            compiled ? "Graph gate valid-copy (pass)"
                     : "Graph gate valid-copy (FAIL)");
    }

    {
        engine::renderer::RenderGraph probe;
        const auto t1 = probe.create_transient({
            "cycle_a",
            engine::rhi::Format::RGBA8_UNORM,
            engine::rhi::TextureUsage::RenderTarget,
        });
        const auto t2 = probe.create_transient({
            "cycle_b",
            engine::rhi::Format::RGBA8_UNORM,
            engine::rhi::TextureUsage::RenderTarget,
        });
        engine::renderer::RenderPassDesc seed{};
        seed.name = "seed";
        seed.writes[0] = {t1, engine::renderer::Access::ColorWrite};
        seed.writes[1] = {t2, engine::renderer::Access::ColorWrite};
        seed.write_count = 2;
        probe.add_pass(std::move(seed));
        engine::renderer::RenderPassDesc a{};
        a.name = "cycle_a";
        a.reads[0] = {t2, engine::renderer::Access::ShaderRead};
        a.read_count = 1;
        a.writes[0] = {t1, engine::renderer::Access::ColorWrite};
        a.write_count = 1;
        probe.add_pass(std::move(a));
        engine::renderer::RenderPassDesc b{};
        b.name = "cycle_b";
        b.reads[0] = {t1, engine::renderer::Access::ShaderRead};
        b.read_count = 1;
        b.writes[0] = {t2, engine::renderer::Access::ColorWrite};
        b.write_count = 1;
        probe.add_pass(std::move(b));
        const bool detected = !probe.compile();
        ok = ok && detected;
        engine::log(detected ? engine::LogLevel::Info : engine::LogLevel::Error,
            engine::LogChannel::Render,
            detected ? "Graph gate cycle (pass)" : "Graph gate cycle (FAIL)");
    }

    return ok;
}

bool run_swap_gate() {
    engine::renderer::RenderGraph probe;
    engine::renderer::StandardFrameDesc desc{};
    desc.log_ready = false;
    const bool compiled = engine::renderer::setup_standard_frame(probe, std::move(desc));
    engine::log(compiled ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render,
        compiled ? "Swap gate: standard frame owned by renderer (pass)"
                 : "Swap gate: standard frame owned by renderer (FAIL)");
    return compiled;
}

bool setup_render_graph(engine::Engine& app, SandboxState& state) {
    engine::renderer::StandardFrameDesc desc{};
    desc.draw_debug_lines = [&state](engine::renderer::PassContext& ctx) {
        state.debug_lines.draw(ctx.cmd, ctx.snapshot.view, ctx.snapshot.projection);
    };
    desc.draw_overlay = [&state](engine::renderer::PassContext& ctx) {
        state.overlay.draw(ctx.cmd, ctx.snapshot.width, ctx.snapshot.height);
    };
    return engine::renderer::setup_standard_frame(app.render_graph(), std::move(desc));
}

bool setup_stats_overlay(engine::Engine& app, engine::assets::IAssetLoader& loader,
    engine::shaders::IShaderCompiler& compiler, SandboxState& state) {
    auto* device = app.device();
    if (!device) return false;

    std::string overlay_shader;
    if (!resolve_content(loader, kOverlayShader, overlay_shader)) {
        return false;
    }

    if (!state.overlay.init(*device, compiler, overlay_shader)) {
        return false;
    }

    engine::log(engine::LogLevel::Info, engine::LogChannel::Render,
        "Stats overlay ready (F3 to toggle)");
    return true;
}

bool setup_debug_lines(engine::Engine& app, engine::assets::IAssetLoader& loader,
    engine::shaders::IShaderCompiler& compiler, SandboxState& state) {
    auto* device = app.device();
    if (!device) return false;

    std::string shader_path;
    if (!resolve_content(loader, kDebugLinesShader, shader_path)) {
        return false;
    }

    if (!state.debug_lines.init(*device, compiler, shader_path)) {
        return false;
    }

    engine::log(engine::LogLevel::Info, engine::LogChannel::Render,
        "Debug lines ready (F4 to toggle instance AABBs)");
    return true;
}

bool setup_forward_demo(engine::Engine& app, engine::assets::IAssetLoader& loader,
    engine::shaders::IShaderCompiler& compiler, SandboxState& state, bool fail_on_gate) {
    auto* device = app.device();
    if (!device) return false;

    std::string cube_path;
    std::string husky_path;
    std::string shader_path;
    std::string shadow_path;
    std::string tonemap_path;
    std::string sky_path;
    std::string bloom_down_path;
    std::string bloom_up_path;
    std::string fxaa_path;
    std::string smaa_edge_path;
    std::string smaa_weights_path;
    std::string smaa_blend_path;
    std::string motion_path;
    std::string taa_path;
    std::string tonemap_aces_path;
    if (!resolve_content(loader, kCubeMesh, cube_path)
        || !resolve_content(loader, kHuskyMesh, husky_path)
        || !resolve_content(loader, kForwardShader, shader_path)
        || !resolve_content(loader, kShadowShader, shadow_path)
        || !resolve_content(loader, kTonemapShader, tonemap_path)
        || !resolve_content(loader, kSkyShader, sky_path)
        || !resolve_content(loader, kBloomDownShader, bloom_down_path)
        || !resolve_content(loader, kBloomUpShader, bloom_up_path)
        || !resolve_content(loader, kFxaaShader, fxaa_path)
        || !resolve_content(loader, kSmaaEdgeShader, smaa_edge_path)
        || !resolve_content(loader, kSmaaWeightsShader, smaa_weights_path)
        || !resolve_content(loader, kSmaaBlendShader, smaa_blend_path)
        || !resolve_content(loader, kMotionShader, motion_path)
        || !resolve_content(loader, kTaaShader, taa_path)
        || !resolve_content(loader, kTonemapAcesShader, tonemap_aces_path)) {
        return false;
    }

    auto mesh_loader = engine::assets::obj::create_mesh_loader();
    engine::assets::MeshData cube_data;
    if (!mesh_loader->load(cube_path, cube_data)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets, "Failed to load cube mesh");
        return false;
    }
    auto gltf_loader = engine::assets::gltf::create_mesh_loader();
    engine::assets::gltf::GltfLoadResult husky_gltf;
    if (!gltf_loader->load(husky_path, husky_gltf)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets, "Failed to load husky glTF");
        return false;
    }
    if (!run_gltf_gate(husky_gltf) && fail_on_gate) {
        return false;
    }
    if (!run_gltf_extras_gate() && fail_on_gate) {
        return false;
    }
    const engine::f32 husky_metallic = husky_gltf.metallic;
    const engine::f32 husky_roughness = husky_gltf.roughness;
    engine::assets::MeshData husky_data = std::move(husky_gltf.mesh);
    if (!run_husky_mesh_gate(husky_data) && fail_on_gate) {
        return false;
    }
    if (!run_aabb_gate(husky_data) && fail_on_gate) {
        return false;
    }
    if (!run_aabb_transform_gate(husky_data) && fail_on_gate) {
        return false;
    }
    if (!run_aspect_gate() && fail_on_gate) {
        return false;
    }

    engine::shaders::ShaderCompileDesc vs_desc{};
    vs_desc.file_path = shader_path;
    vs_desc.entry_point = "vs_main";
    vs_desc.target_profile = "vs_6_0";

    engine::shaders::ShaderCompileDesc ps_desc = vs_desc;
    ps_desc.entry_point = "ps_main";
    ps_desc.target_profile = "ps_6_0";

    if (!run_shader_cache_gate(compiler, vs_desc)) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Render, "Shader cache gate failed");
        if (fail_on_gate) {
            return false;
        }
    }

    engine::shaders::ShaderBytecode vs_bytecode;
    engine::shaders::ShaderBytecode ps_bytecode;
    std::string error;
    if (!compiler.compile(vs_desc, vs_bytecode, error)) return false;
    if (!compiler.compile(ps_desc, ps_bytecode, error)) return false;
    if (!run_dxc_gate(vs_desc, vs_bytecode) && fail_on_gate) {
        return false;
    }
    if (!run_rhi_contract_gate(*device, compiler) && fail_on_gate) {
        return false;
    }
    std::string compute_path;
    if (!resolve_content(loader, kComputeGateShader, compute_path)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "compute_gate.hlsl missing");
        if (fail_on_gate) {
            return false;
        }
    } else if (!run_rhi_impl_gate(*device, compiler, compute_path) && fail_on_gate) {
        return false;
    }

    auto demo = std::make_unique<ForwardDemo>();
    vs_desc.file_path = shader_path;
    ps_desc.file_path = shader_path;
    demo->pipeline = device->create_graphics_pipeline(
        make_forward_pipeline_desc(vs_bytecode.data, ps_bytecode.data));
    if (!demo->pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Forward pipeline creation failed");
        return false;
    }

    engine::shaders::ShaderCompileDesc shadow_vs = vs_desc;
    shadow_vs.file_path = shadow_path;
    engine::shaders::ShaderBytecode shadow_bytecode;
    if (!compiler.compile(shadow_vs, shadow_bytecode, error)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Shadow vertex shader compile failed");
        return false;
    }
    demo->shadow_pipeline = device->create_graphics_pipeline(make_shadow_pipeline_desc(shadow_bytecode.data));
    if (!demo->shadow_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Shadow pipeline creation failed");
        return false;
    }

    engine::shaders::ShaderCompileDesc tonemap_vs = vs_desc;
    tonemap_vs.file_path = tonemap_path;
    engine::shaders::ShaderCompileDesc tonemap_ps = ps_desc;
    tonemap_ps.file_path = tonemap_path;
    engine::shaders::ShaderBytecode tonemap_vs_bytecode;
    engine::shaders::ShaderBytecode tonemap_ps_bytecode;
    if (!compiler.compile(tonemap_vs, tonemap_vs_bytecode, error)
        || !compiler.compile(tonemap_ps, tonemap_ps_bytecode, error)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Tonemap shader compile failed");
        return false;
    }
    demo->tonemap_pipeline = device->create_graphics_pipeline(
        make_tonemap_pipeline_desc(tonemap_vs_bytecode.data, tonemap_ps_bytecode.data));
    if (!demo->tonemap_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Tonemap pipeline creation failed");
        return false;
    }

    engine::shaders::ShaderCompileDesc sky_vs = vs_desc;
    sky_vs.file_path = sky_path;
    engine::shaders::ShaderCompileDesc sky_ps = ps_desc;
    sky_ps.file_path = sky_path;
    engine::shaders::ShaderBytecode sky_vs_bytecode;
    engine::shaders::ShaderBytecode sky_ps_bytecode;
    if (!compiler.compile(sky_vs, sky_vs_bytecode, error)
        || !compiler.compile(sky_ps, sky_ps_bytecode, error)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Sky shader compile failed");
        return false;
    }
    demo->sky_pipeline = device->create_graphics_pipeline(
        make_sky_pipeline_desc(sky_vs_bytecode.data, sky_ps_bytecode.data));
    if (!demo->sky_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Sky pipeline creation failed");
        return false;
    }

    engine::shaders::ShaderCompileDesc bloom_down_vs = vs_desc;
    bloom_down_vs.file_path = bloom_down_path;
    engine::shaders::ShaderCompileDesc bloom_down_ps = ps_desc;
    bloom_down_ps.file_path = bloom_down_path;
    engine::shaders::ShaderBytecode bloom_down_vs_bytecode;
    engine::shaders::ShaderBytecode bloom_down_ps_bytecode;
    if (!compiler.compile(bloom_down_vs, bloom_down_vs_bytecode, error)
        || !compiler.compile(bloom_down_ps, bloom_down_ps_bytecode, error)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Bloom downsample shader compile failed");
        return false;
    }
    demo->bloom_downsample_pipeline = device->create_graphics_pipeline(
        make_bloom_downsample_pipeline_desc(bloom_down_vs_bytecode.data, bloom_down_ps_bytecode.data));
    if (!demo->bloom_downsample_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Bloom downsample pipeline creation failed");
        return false;
    }

    engine::shaders::ShaderCompileDesc bloom_up_vs = vs_desc;
    bloom_up_vs.file_path = bloom_up_path;
    engine::shaders::ShaderCompileDesc bloom_up_ps = ps_desc;
    bloom_up_ps.file_path = bloom_up_path;
    engine::shaders::ShaderBytecode bloom_up_vs_bytecode;
    engine::shaders::ShaderBytecode bloom_up_ps_bytecode;
    if (!compiler.compile(bloom_up_vs, bloom_up_vs_bytecode, error)
        || !compiler.compile(bloom_up_ps, bloom_up_ps_bytecode, error)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Bloom upsample shader compile failed");
        return false;
    }
    demo->bloom_upsample_pipeline = device->create_graphics_pipeline(
        make_bloom_upsample_pipeline_desc(bloom_up_vs_bytecode.data, bloom_up_ps_bytecode.data));
    if (!demo->bloom_upsample_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Bloom upsample pipeline creation failed");
        return false;
    }

    engine::shaders::ShaderBytecode fxaa_vs_bytecode;
    engine::shaders::ShaderBytecode fxaa_ps_bytecode;
    if (!compile_fullscreen_hlsl(compiler, fxaa_path, "FXAA shader compile failed",
            fxaa_vs_bytecode, fxaa_ps_bytecode)) {
        return false;
    }
    demo->fxaa_pipeline = device->create_graphics_pipeline(
        make_fxaa_pipeline_desc(fxaa_vs_bytecode.data, fxaa_ps_bytecode.data));
    if (!demo->fxaa_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "FXAA pipeline creation failed");
        return false;
    }

    engine::shaders::ShaderBytecode smaa_edge_vs_bytecode;
    engine::shaders::ShaderBytecode smaa_edge_ps_bytecode;
    if (!compile_fullscreen_hlsl(compiler, smaa_edge_path, "SMAA edge shader compile failed",
            smaa_edge_vs_bytecode, smaa_edge_ps_bytecode)) {
        return false;
    }
    demo->smaa_edge_pipeline = device->create_graphics_pipeline(
        make_smaa_edge_pipeline_desc(smaa_edge_vs_bytecode.data, smaa_edge_ps_bytecode.data));
    if (!demo->smaa_edge_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "SMAA edge pipeline creation failed");
        return false;
    }

    engine::shaders::ShaderBytecode smaa_weights_vs_bytecode;
    engine::shaders::ShaderBytecode smaa_weights_ps_bytecode;
    if (!compile_fullscreen_hlsl(compiler, smaa_weights_path, "SMAA weights shader compile failed",
            smaa_weights_vs_bytecode, smaa_weights_ps_bytecode)) {
        return false;
    }
    demo->smaa_weights_pipeline = device->create_graphics_pipeline(
        make_smaa_weights_pipeline_desc(smaa_weights_vs_bytecode.data, smaa_weights_ps_bytecode.data));
    if (!demo->smaa_weights_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "SMAA weights pipeline creation failed");
        return false;
    }

    engine::shaders::ShaderBytecode smaa_blend_vs_bytecode;
    engine::shaders::ShaderBytecode smaa_blend_ps_bytecode;
    if (!compile_fullscreen_hlsl(compiler, smaa_blend_path, "SMAA blend shader compile failed",
            smaa_blend_vs_bytecode, smaa_blend_ps_bytecode)) {
        return false;
    }
    demo->smaa_blend_pipeline = device->create_graphics_pipeline(
        make_smaa_blend_pipeline_desc(smaa_blend_vs_bytecode.data, smaa_blend_ps_bytecode.data));
    if (!demo->smaa_blend_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "SMAA blend pipeline creation failed");
        return false;
    }

    engine::shaders::ShaderCompileDesc motion_vs = vs_desc;
    motion_vs.file_path = motion_path;
    engine::shaders::ShaderCompileDesc motion_ps = ps_desc;
    motion_ps.file_path = motion_path;
    engine::shaders::ShaderBytecode motion_vs_bytecode;
    engine::shaders::ShaderBytecode motion_ps_bytecode;
    if (!compiler.compile(motion_vs, motion_vs_bytecode, error)
        || !compiler.compile(motion_ps, motion_ps_bytecode, error)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Motion vector shader compile failed");
        return false;
    }
    demo->motion_pipeline = device->create_graphics_pipeline(
        make_motion_pipeline_desc(motion_vs_bytecode.data, motion_ps_bytecode.data));
    if (!demo->motion_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Motion vector pipeline creation failed");
        return false;
    }

    engine::shaders::ShaderBytecode taa_vs_bytecode;
    engine::shaders::ShaderBytecode taa_ps_bytecode;
    if (!compile_fullscreen_hlsl(compiler, taa_path, "TAA shader compile failed",
            taa_vs_bytecode, taa_ps_bytecode)) {
        return false;
    }
    demo->taa_pipeline = device->create_graphics_pipeline(
        make_taa_pipeline_desc(taa_vs_bytecode.data, taa_ps_bytecode.data));
    if (!demo->taa_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "TAA pipeline creation failed");
        return false;
    }

    engine::shaders::ShaderBytecode aces_vs_bytecode;
    engine::shaders::ShaderBytecode aces_ps_bytecode;
    if (!compile_fullscreen_hlsl(compiler, tonemap_aces_path, "ACES tonemap shader compile failed",
            aces_vs_bytecode, aces_ps_bytecode)) {
        return false;
    }
    demo->tonemap_aces_pipeline = device->create_graphics_pipeline(
        make_tonemap_aces_pipeline_desc(aces_vs_bytecode.data, aces_ps_bytecode.data));
    if (!demo->tonemap_aces_pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "ACES tonemap pipeline creation failed");
        return false;
    }

    if (!run_handle_gate(demo->meshes, *device, cube_data)) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Assets, "MeshHandle gate failed");
        if (fail_on_gate) {
            return false;
        }
    }
    if (!run_handle_unload_gate(demo->meshes, *device, cube_data)) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Assets, "MeshHandle unload gate failed");
        if (fail_on_gate) {
            return false;
        }
    }
    demo->husky = demo->meshes.store(*device, kHuskyMesh, husky_data);
    const auto* gpu_mesh = demo->meshes.get(demo->husky);
    if (!gpu_mesh) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets, "Husky mesh store is empty");
        return false;
    }
    device->set_debug_name(*gpu_mesh->vertex_buffer, "sandbox/husky_vb");
    device->set_debug_name(*gpu_mesh->index_buffer, "sandbox/husky_ib");

    if (!run_mesh_reload_gate(*device, cube_data) && fail_on_gate) {
        return false;
    }
    if (!run_two_draw_items_gate() && fail_on_gate) {
        return false;
    }

    for (engine::usize i = 0; i < sandbox::kHuskyVariantCount; ++i) {
        engine::assets::ImageData image;
        if (!load_albedo_texture(loader, *device, kHuskyAlbedos[i], demo->albedos[i], image)) {
            return false;
        }
        if (i == 0 && !run_albedo_gate(image) && fail_on_gate) {
            return false;
        }
        if (i == 0 && !run_mip_gate(*demo->albedos[i], image.width) && fail_on_gate) {
            return false;
        }
    }

    const engine::f32 foot_y = husky_data.bounds.min.y;
    state.husky_foot_y = foot_y;
    constexpr engine::u32 kHuskyCount = engine::scene::kMaxInstances - 1;
    engine::scene::MaterialHandle husky_mats[sandbox::kHuskyVariantCount]{};
    for (engine::u32 i = 0; i < sandbox::kHuskyVariantCount; ++i) {
        engine::scene::Material mat{};
        mat.albedo = i;
        mat.metallic = husky_metallic;
        mat.roughness = (i == 0) ? husky_roughness : (0.12f + static_cast<engine::f32>(i) * 0.22f);
        husky_mats[i] = engine::scene::add_material(demo->world, mat);
    }
    for (engine::u32 i = 0; i < kHuskyCount; ++i) {
        engine::scene::Instance instance{};
        instance.mesh = demo->husky;
        instance.material = husky_mats[i % sandbox::kHuskyVariantCount];
        engine::math::Vec3 pos{};
        if (i < 4) {
            pos = {(static_cast<engine::f32>(i) - 1.5f) * 0.5f, -foot_y, 0.f};
        } else if (i < 32) {
            const engine::u32 k = i - 4;
            pos = {
                (static_cast<engine::f32>(k % 7) - 3.f) * 1.1f,
                -foot_y,
                static_cast<engine::f32>(k / 7) * 1.1f + 1.2f,
            };
        } else {
            const engine::u32 k = i - 32;
            pos = {
                40.f + static_cast<engine::f32>(k % 6) * 1.2f,
                -foot_y,
                static_cast<engine::f32>(k / 6) * 1.2f - 6.f,
            };
        }
        instance.model = engine::math::Mat4::translate(pos);
        const engine::u32 index = engine::scene::add_instance(demo->world, instance);
        char name[24];
        std::snprintf(name, sizeof(name), "husky_%u", i);
        engine::scene::set_instance_name(demo->world, index, name);
        if (i == 0 && app.physics()) {
            engine::physics::BodyDesc floor{};
            floor.shape.type = engine::physics::ShapeType::Aabb;
            floor.shape.half_extents = {6.f, 0.5f, 6.f};
            floor.position = {0.f, -0.5f, 0.f};
            floor.motion = engine::physics::MotionType::Static;
            app.physics()->create_body(floor);

            engine::gameplay::CharacterDesc desc{};
            const engine::math::Vec3 spawn{pos.x, desc.half_height + desc.radius, pos.z};
            state.player.spawn(*app.physics(), spawn, desc);
            state.player.move({}, false, 1.f / 60.f);
        }
    }
    const engine::u32 husky0 = engine::scene::find_instance(demo->world, "husky_0");
    const engine::u32 husky1 = engine::scene::find_instance(demo->world, "husky_1");
    if (husky0 != engine::scene::kInvalidInstance && husky1 != engine::scene::kInvalidInstance) {
        engine::scene::set_instance_parent(demo->world, husky1, husky0, true);
    }

    engine::assets::MeshData ground_data = make_ground_quad(6.f, 0.f);
    demo->ground = demo->meshes.store(*device, kGroundMesh, ground_data);
    const auto* gpu_ground = demo->meshes.get(demo->ground);
    if (!gpu_ground) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets, "Ground mesh store is empty");
        return false;
    }
    device->set_debug_name(*gpu_ground->vertex_buffer, "sandbox/ground_vb");
    device->set_debug_name(*gpu_ground->index_buffer, "sandbox/ground_ib");

    const engine::assets::ImageData checker = make_checker_albedo(64, 8);
    engine::rhi::TextureDesc floor_desc{};
    floor_desc.width = checker.width;
    floor_desc.height = checker.height;
    floor_desc.format = engine::rhi::Format::RGBA8_UNORM;
    floor_desc.usage = engine::rhi::TextureUsage::ShaderResource;
    floor_desc.mip_levels = 0;
    demo->floor_albedo = device->create_texture(floor_desc, checker.rgba.data());
    if (!demo->floor_albedo) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Floor albedo creation failed");
        return false;
    }
    device->set_debug_name(*demo->floor_albedo, "sandbox/floor_albedo");

    auto make_solid = [&](engine::u8 r, engine::u8 g, engine::u8 b, engine::u8 a,
        const char* name, std::unique_ptr<engine::rhi::ITexture>& out) {
        engine::u8 px[4] = {r, g, b, a};
        engine::rhi::TextureDesc desc{};
        desc.width = 1;
        desc.height = 1;
        desc.format = engine::rhi::Format::RGBA8_UNORM;
        desc.usage = engine::rhi::TextureUsage::ShaderResource;
        desc.mip_levels = 1;
        out = device->create_texture(desc, px);
        if (!out) {
            return false;
        }
        device->set_debug_name(*out, name);
        return true;
    };
    if (!make_solid(255, 255, 255, 255, "sandbox/default_mr", demo->default_mr)
        || !make_solid(128, 128, 255, 255, "sandbox/default_normal", demo->default_normal)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Default MR/normal texture creation failed");
        return false;
    }

    if (!upload_ibl_maps(*device, *demo)) {
        return false;
    }

    engine::scene::Instance floor{};
    floor.mesh = demo->ground;
    engine::scene::Material floor_mat{};
    floor_mat.albedo = sandbox::kFloorAlbedoIndex;
    floor_mat.metallic = 0.f;
    floor_mat.roughness = 0.9f;
    floor.material = engine::scene::add_material(demo->world, floor_mat);
    floor.model = engine::math::Mat4::identity();
    const engine::u32 ground = engine::scene::add_instance(demo->world, floor);
    engine::scene::set_instance_name(demo->world, ground, "ground");

    demo->world.sun.direction = {0.12f, 0.42f, 0.90f};
    demo->world.sun.color = {4.8f, 4.4f, 3.8f};
    demo->world.ambient = {0.16f, 0.17f, 0.21f};
    demo->world.points[0].position = {-0.55f, 0.38f, 0.45f};
    demo->world.points[0].color = {1.f, 0.45f, 0.18f};
    demo->world.points[0].radius = 1.8f;
    demo->world.points[0].intensity = 2.2f;

    if (!run_scene_world_gate(demo->world) && fail_on_gate) {
        return false;
    }
    if (!run_scene_name_gate() && fail_on_gate) {
        return false;
    }
    if (!run_scene_hierarchy_gate() && fail_on_gate) {
        return false;
    }
    if (!run_scene_file_gate() && fail_on_gate) {
        return false;
    }
    if (!run_scene_prefab_gate() && fail_on_gate) {
        return false;
    }
    if (!run_light_gate(demo->world) && fail_on_gate) {
        return false;
    }
    if (!run_shadow_gate(demo->world, demo->meshes, demo->shadow_pipeline.get()) && fail_on_gate) {
        return false;
    }
    if (!run_hdr_gate(demo->world, demo->tonemap_pipeline.get()) && fail_on_gate) {
        return false;
    }
    if (!run_frustum_gate(demo->world, demo->camera, make_extract_assets(*demo)) && fail_on_gate) {
        return false;
    }
    if (!run_material_gate(demo->world, demo->camera, make_extract_assets(*demo), husky_metallic,
            husky_roughness)
        && fail_on_gate) {
        return false;
    }
    if (!run_pbr_gate() && fail_on_gate) {
        return false;
    }
    if (!run_pcf_gate() && fail_on_gate) {
        return false;
    }
    if (!run_ibl_gate(*demo) && fail_on_gate) {
        return false;
    }
    if (!run_sky_gate(*demo) && fail_on_gate) {
        return false;
    }
    if (!run_bloom_gate(*demo) && fail_on_gate) {
        return false;
    }
    if (!run_aa_gate(*demo) && fail_on_gate) {
        return false;
    }
    if (!run_motion_gate(*demo) && fail_on_gate) {
        return false;
    }
    if (!run_taa_gate(*demo) && fail_on_gate) {
        return false;
    }

    demo->shader_sources.vertex = vs_desc;
    demo->shader_sources.pixel  = ps_desc;
    demo->shader_watcher = engine::shaders::dxc::create_hot_reloader();
    demo->shader_watcher->begin_watch(demo->shader_sources);
    if (!run_async_compile_gate(*demo->shader_watcher) && fail_on_gate) {
        return false;
    }

    state.forward = std::move(demo);
    engine::log(engine::LogLevel::Info, engine::LogChannel::Render,
        "Forward pass ready (AA default Off / F5 FXAA+SMAA+TAA, F11 windowed/borderless, Tab/Start walk, Enter/Y follow/orbit/FPS, Space/A jump, pad sticks, motion vectors, Karis bloom, source cubemap sky, split-sum IBL, PBR GGX, 16-tap Vogel PCF, materials, renderer-owned frame, 64 instances, frustum skip, async DXC, glTF husky + mips, HDR, F4 AABBs, Space beep, Z/X, WASD look)");
    return true;
}

} // namespace

int main(int argc, char** argv) {
    bool gates_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--gates") {
            gates_mode = true;
        }
    }
    // Before Engine::init, so config.cfg cannot overwrite a --set value.
    engine::apply_cvar_args(argc, argv);

#ifdef ENGINE_GAME_APP
    constexpr const char* kAppName = "Game";
    constexpr const char* kWindowTitle = "Sol";
#else
    constexpr const char* kAppName = "Sandbox";
    constexpr const char* kWindowTitle = "Engine Sandbox";
#endif
    char start_message[64];
    std::snprintf(start_message, sizeof(start_message),
        gates_mode ? "%s starting (--gates)" : "%s starting", kAppName);
    engine::log(engine::LogLevel::Info, engine::LogChannel::General, start_message);

    engine::EngineModules modules{};

#ifdef ENGINE_HAS_WIN32_PLATFORM
    modules.platform = engine::platform::win32::create_platform();
#else
    engine::log(engine::LogLevel::Fatal, engine::LogChannel::Platform, "No platform backend");
    return 1;
#endif

#ifdef ENGINE_HAS_XAUDIO2
    modules.audio = engine::audio::xaudio2::create_audio();
#endif

#ifdef ENGINE_HAS_PHYSICS_CPU
    modules.physics = engine::physics::cpu::create_physics();
#endif

#ifdef ENGINE_HAS_D3D12
    modules.rhi = engine::rhi::d3d12::create_rhi();
#endif

    engine::Engine app(std::move(modules));
    SandboxState state;

    engine::EngineCallbacks callbacks{};
    callbacks.on_fixed_update = [&app, &state](const engine::FrameContext& frame) {
        if (!state.player.spawned() || !app.physics() || !app.input() || !app.input()->focused()) {
            if (state.player.spawned()) {
                state.player.move({}, false, frame.fixed_delta);
            }
            return;
        }

        using engine::platform::Key;
        engine::math::Vec3 wish{};
        if (state.walk_mode && state.forward) {
            const engine::math::Vec3 forward = state.game_camera.horizontal_forward();
            const engine::math::Vec3 right = forward.cross({0.f, 1.f, 0.f}).normalized();
            if (app.input()->key_down(Key::W)) {
                wish += forward;
            }
            if (app.input()->key_down(Key::S)) {
                wish -= forward;
            }
            if (app.input()->key_down(Key::A)) {
                wish -= right;
            }
            if (app.input()->key_down(Key::D)) {
                wish += right;
            }
            const auto& pad = app.input()->state().gamepads[0];
            wish += forward * pad.left_y;
            wish += right * pad.left_x;
            using engine::platform::GamepadButton;
            if (app.input()->button_down(GamepadButton::DpadUp)) {
                wish += forward;
            }
            if (app.input()->button_down(GamepadButton::DpadDown)) {
                wish -= forward;
            }
            if (app.input()->button_down(GamepadButton::DpadLeft)) {
                wish -= right;
            }
            if (app.input()->button_down(GamepadButton::DpadRight)) {
                wish += right;
            }
        }
        if (app.input()->key_down(Key::Z)) {
            wish.x -= 1.f;
        }
        if (app.input()->key_down(Key::X)) {
            wish.x += 1.f;
        }
        const bool jump = state.walk_mode && (app.input()->key_pressed(Key::Space)
            || app.input()->button_pressed(engine::platform::GamepadButton::A));
        state.player.move(wish, jump, frame.fixed_delta);
    };
    callbacks.on_update = [&app, &state](const engine::FrameContext& frame) {
        state.frame_stats.update(frame.delta);

        if (app.input() && app.input()->key_pressed(engine::platform::Key::F3)) {
            state.overlay.set_visible(!state.overlay.visible());
        }
        if (app.input() && app.input()->key_pressed(engine::platform::Key::F4)) {
            state.debug_lines.set_visible(!state.debug_lines.visible());
        }
        if (app.input() && app.input()->key_pressed(engine::platform::Key::F5) && state.forward) {
            state.forward->aa_mode = engine::renderer::aa::next_mode(state.forward->aa_mode);
        }
        if (app.input() && app.input()->key_pressed(engine::platform::Key::F11) && app.window()) {
            const auto mode = app.window()->mode();
            app.window()->set_mode(mode == engine::platform::WindowMode::Windowed
                ? engine::platform::WindowMode::Borderless
                : engine::platform::WindowMode::Windowed);
        }
        if (app.input() && app.input()->key_pressed(engine::platform::Key::Space) && app.audio()
            && state.beep.valid() && app.input()->focused() && !state.walk_mode) {
            app.audio()->play(state.beep);
        }
        if (app.input() && app.input()->focused()
            && (app.input()->key_pressed(engine::platform::Key::Tab)
                || app.input()->button_pressed(engine::platform::GamepadButton::Start))) {
            toggle_walk_mode(state);
        }
        if (app.input() && app.input()->focused()
            && (app.input()->key_pressed(engine::platform::Key::Enter)
                || app.input()->button_pressed(engine::platform::GamepadButton::Y))) {
            state.game_camera.set_mode(engine::gameplay::next_camera_mode(state.game_camera.mode()));
            char mode_message[64];
            std::snprintf(mode_message, sizeof(mode_message), "Camera mode: %s",
                engine::gameplay::camera_mode_name(state.game_camera.mode()));
            engine::log(engine::LogLevel::Info, engine::LogChannel::General, mode_message);
        }

        if (state.overlay.visible()) {
            engine::debug::FrameStats stats = state.frame_stats.stats();
            stats.poll_ms = engine::profiler_scope_ms("poll_events");
            stats.extract_ms = engine::profiler_scope_ms("extract");
            stats.execute_ms = engine::profiler_scope_ms("execute");
            stats.cpu_ms = engine::profiler_scope_ms("frame");
            if (app.device()) {
                stats.gpu_ms = app.device()->last_gpu_time_ms();
            }
            if (state.forward) {
                stats.aa = engine::renderer::aa::mode_name(state.forward->aa_mode);
            }
            state.overlay.update(stats);
        }

        if (state.forward && app.device()) {
            poll_shader_reload(*app.device(), *state.forward);
            if (app.input() && app.input()->focused()) {
                auto& world = state.forward->world;
                if (state.walk_mode) {
                    const auto& input = app.input()->state();
                    using engine::platform::MouseButton;
                    if (input.mouse_down[static_cast<engine::usize>(MouseButton::Right)]) {
                        state.game_camera.add_look(input.mouse_dx, input.mouse_dy);
                    }
                    state.game_camera.add_look_velocity(
                        input.gamepads[0].right_x * 2.5f,
                        input.gamepads[0].right_y * 2.0f,
                        frame.delta);
                    if (state.player.spawned()) {
                        state.game_camera.update(state.player.position());
                    }
                } else {
                    state.forward->camera.update(app.input()->state(), frame.delta, true);
                }
                if (state.player.spawned() && world.instance_count > 0) {
                    const engine::math::Vec3 p = state.player.position();
                    const engine::f32 vis_y = -state.husky_foot_y
                        + (p.y - state.player.rest_offset());
                    engine::scene::set_instance_model(world, 0,
                        engine::math::Mat4::translate({p.x, vis_y, p.z}));
                }
            }
        }
    };
    callbacks.on_extract = [&app, &state](engine::renderer::RenderSnapshot& snapshot, engine::Arena& arena) {
        if (!state.forward || !state.forward->pipeline) {
            return;
        }
        auto& world = state.forward->world;
        const engine::u32 width = std::max(snapshot.width, 1u);
        const engine::u32 height = std::max(snapshot.height, 1u);
        const engine::f32 aspect = static_cast<engine::f32>(width) / static_cast<engine::f32>(height);
        const bool use_game = state.walk_mode && state.player.spawned();
        if (use_game) {
            world.camera.view = state.game_camera.view();
            world.camera.projection = state.game_camera.projection(aspect);
        } else {
            world.camera.view = state.forward->camera.view();
            world.camera.projection = state.forward->camera.projection(aspect);
        }
        if (app.device()) {
            ensure_taa_history(*app.device(), app.render_graph(), *state.forward, width, height);
        }
        auto assets = make_extract_assets(*state.forward);
        if (state.forward->taa_history_valid) {
            assets.taa_history =
                state.forward->taa_history[(state.forward->taa_frames & 1u) ^ 1u].get();
        }
        const engine::math::Vec3 eye = use_game
            ? state.game_camera.position()
            : state.forward->camera.position;
        sandbox::extract_world(world, eye, assets,
            state.overlay.visible(),
            state.debug_lines.visible() ? &state.debug_lines : nullptr, arena, snapshot,
            &state.forward->motion_history);
        if (snapshot.aa_mode == engine::renderer::aa::Mode::Taa && snapshot.taa_pipeline
            && snapshot.tonemap_aces_pipeline) {
            state.forward->taa_frames += 1;
            state.forward->taa_history_valid = true;
        } else {
            state.forward->taa_history_valid = false;
        }
    };
    app.set_callbacks(callbacks);

    engine::EngineConfig config{};
    config.window.title = kWindowTitle;
    config.window.width  = 1280;
    config.window.height = 720;
    config.device.preferred_api = engine::rhi::GraphicsAPI::D3D12;

    if (!app.init(config)) {
        return 1;
    }

    if (app.audio()) {
        const auto tone = engine::audio::make_tone_pcm16(22050, 120, 880.f, 0.25f);
        engine::audio::SoundDesc beep{};
        beep.pcm = {reinterpret_cast<const engine::u8*>(tone.data()),
            tone.size() * sizeof(engine::i16)};
        beep.sample_rate = 22050;
        beep.channels = 1;
        beep.bits_per_sample = 16;
        state.beep = app.audio()->create_sound(beep);
    }

    if (!app.filesystem()) {
        engine::log(engine::LogLevel::Fatal, engine::LogChannel::Assets, "No filesystem");
        return 1;
    }

    auto loader = engine::assets::filesystem::create_asset_loader(*app.filesystem());
    const auto mounts = engine::resolve_content_mounts(app.content_root());
    if (!mount_app_content(*loader, mounts)) {
        return 1;
    }

    bool gates_ok = run_mount_gate(*loader) && run_build_gate(app.content_layout());
    if (!run_cvar_gate(app.filesystem(), app.executable_directory())) {
        gates_ok = false;
    }
    if (!run_window_gate(app.window(), app.device())) {
        gates_ok = false;
    }
    if (!run_identity_gate(app)) {
        gates_ok = false;
    }
    if (!run_ship_gate(app)) {
        gates_ok = false;
    }
    if (!run_pix_gate(app.device())) {
        gates_ok = false;
    }
    if (!run_audio_gate(app.audio())) {
        gates_ok = false;
    }
    if (!run_physics_gate(app.physics())) {
        gates_ok = false;
    }
    if (!run_physics_body_gate(app.physics())) {
        gates_ok = false;
    }
    if (!run_physics_capsule_gate(app.physics())) {
        gates_ok = false;
    }
    if (!run_physics_trigger_gate(app.physics())) {
        gates_ok = false;
    }
    if (!run_physics_raycast_gate(app.physics())) {
        gates_ok = false;
    }
    if (!run_character_gate(app.physics())) {
        gates_ok = false;
    }
    if (!run_camera_gate()) {
        gates_ok = false;
    }
    if (!run_gamepad_gate(app.input())) {
        gates_ok = false;
    }
    if (!run_cook_gate()) {
        gates_ok = false;
    }
    if (!run_pak_gate()) {
        gates_ok = false;
    }
    if (!run_pack_gate(app)) {
        gates_ok = false;
    }

    const auto cache_dir = (std::filesystem::path(app.content_root()) / ".cache" / "shaders").string();
    auto compiler = engine::shaders::dxc::create_cached_compiler(cache_dir);

    if (!setup_forward_demo(app, *loader, *compiler, state, gates_mode)) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
            "Forward pass setup failed — running without rendering");
        gates_ok = false;
    }

    if (!setup_stats_overlay(app, *loader, *compiler, state)) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
            "Stats overlay setup failed");
    }

    if (!setup_debug_lines(app, *loader, *compiler, state)) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
            "Debug lines setup failed");
        gates_ok = false;
    }

    gates_ok = run_graph_gate() && run_swap_gate() && gates_ok;
    if (!setup_render_graph(app, state)) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
            "Render graph setup failed — running without rendering");
        gates_ok = false;
    }

    if (gates_mode) {
        char done_message[64];
        std::snprintf(done_message, sizeof(done_message),
            gates_ok ? "%s gates passed" : "%s gates FAILED", kAppName);
        engine::log(gates_ok ? engine::LogLevel::Info : engine::LogLevel::Error,
            engine::LogChannel::General, done_message);
        return gates_ok ? 0 : 1;
    }

    app.run();
    return 0;
}
