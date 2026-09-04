#pragma once

// Everything the sandbox's gate files and main.cpp both need.
//
// This exists because main.cpp used to be one anonymous namespace, and an
// anonymous namespace is translation-unit local: no gate could move to its own
// file while the constants, cvars and app types it reads lived in one. Lifting
// them into `namespace sandbox` is what made the per-domain gate files possible.
//
// Definitions live in sandbox_common.cpp, except constants (constexpr, so a
// per-TU copy is correct and free) and the three app types.

#include <engine/engine.hpp>
#include <engine/cvar_file.hpp>
#include <engine/audio/audio.hpp>
#include <engine/audio/wav.hpp>
#include <engine/core/arena.hpp>
#include <engine/core/frame.hpp>
#include <engine/core/assert.hpp>
#include <engine/core/clock.hpp>
#include <engine/core/cvar.hpp>
#include <engine/core/log.hpp>
#include <engine/core/log_file.hpp>
#include <engine/core/profile.hpp>
#include <engine/renderer/render_graph.hpp>
#include <engine/renderer/render_snapshot.hpp>
#include <engine/renderer/extract.hpp>
#include <engine/renderer/frame_pipelines.hpp>
#include <engine/renderer/ibl.hpp>
#include <engine/renderer/pbr.hpp>
#include <engine/renderer/pcf.hpp>
#include <engine/renderer/sky.hpp>
#include <engine/renderer/aa.hpp>
#include <engine/renderer/bloom.hpp>
#include <engine/renderer/motion.hpp>
#include <engine/renderer/taa.hpp>
#include <engine/renderer/tonemap.hpp>
#include <engine/renderer/standard_frame.hpp>
#include <engine/assets/filesystem/asset_loader_filesystem.hpp>
#include <engine/assets/gpu/mesh_store.hpp>
#include <engine/assets/gpu/texture_store.hpp>
#include <engine/assets/gpu/mesh_upload.hpp>
#include <engine/assets/obj/mesh_loader_obj.hpp>
#include <engine/assets/gltf/mesh_loader_gltf.hpp>
#include <engine/assets/cooked.hpp>
#include <engine/assets/pak.hpp>
#include <engine/assets/image.hpp>
#include <engine/math/aabb.hpp>
#include <engine/math/constants.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/mip.hpp>
#include <engine/math/srgb.hpp>
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

// The backend factories, for the two gates that stand up their own device
// rather than borrowing the sandbox's: the offscreen parity pair. Guarded
// because a configure with a backend switched off does not build its package.
#ifdef ENGINE_HAS_D3D12
#include <engine/rhi/d3d12/rhi_d3d12.hpp>
#endif
#ifdef ENGINE_HAS_VULKAN
#include <engine/rhi/vulkan/rhi_vulkan.hpp>
#endif
#include <engine/debug/debug_lines.hpp>
#include <engine/debug/frame_stats.hpp>
#include <engine/debug/stats_overlay.hpp>
#include <engine/shaders/shader_hot_reload.hpp>
#include <engine/scene_render/extract.hpp>
#include <algorithm>
#include <chrono>
#include <exception>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <iterator>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sandbox {

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
constexpr const char* kSrgbGateShader = "/shaders/srgb_gate.hlsl";
constexpr const char* kStorageTextureGateShader = "/shaders/storage_texture_gate.hlsl";
constexpr const char* kMsaaGateShader = "/shaders/msaa_gate.hlsl";
constexpr const char* kTransparencyGateShader = "/shaders/transparency_gate.hlsl";
constexpr const char* kBackendParityGateShader = "/shaders/backend_parity_gate.hlsl";
constexpr const char* kParityMeshGateShader = "/shaders/parity_mesh_gate.hlsl";
constexpr const char* kParityTextureGateShader
    = "/shaders/parity_texture_gate.hlsl";
constexpr const char* kParityDepthGateShader = "/shaders/parity_depth_gate.hlsl";
constexpr engine::u32 kComputeGateMagic = 0xC0DE0001u;
constexpr const char* kCubeMesh = "/content/meshes/cube.obj";
constexpr const char* kHuskyMesh = "/content/meshes/cartoon_husky.gltf";
// Demo content, not an engine concept. This lived in `engine::scene_render`
// for one commit, because the bridge's hardcoded albedo branch indexed an
// array of husky textures with it; that branch is gone, so it belongs back
// here beside the four paths it counts.
constexpr engine::u32 kHuskyVariantCount = 4;
constexpr const char* kHuskyAlbedos[] = {
    "/content/textures/husky/Cartoon_Husky_Albedo1.png",
    "/content/textures/husky/Cartoon_Husky_Albedo2.png",
    "/content/textures/husky/Cartoon_Husky_Albedo3.png",
    "/content/textures/husky/Cartoon_Husky_Albedo4.png",
};
constexpr const char* kGroundMesh = "/content/meshes/ground_quad";
constexpr const char* kDemoScene = "/content/scenes/demo.solscene";
constexpr const char* kOverlayShader = "/debug/shaders/overlay.hlsl";
constexpr const char* kDebugLinesShader = "/debug/shaders/debug_lines.hlsl";
constexpr const char* kTestFile = "/content/test.txt";

struct QualityPreset {
    const char* name;
    engine::renderer::aa::Mode aa;
    engine::u32 shadow_size;
};

constexpr QualityPreset kQualityPresets[] = {
    {"low", engine::renderer::aa::Mode::Off, 512},
    {"medium", engine::renderer::aa::Mode::Fxaa, 1024},
    {"high", engine::renderer::aa::Mode::Taa, 2048},
};

struct QualitySettings {
    engine::renderer::aa::Mode aa = engine::renderer::aa::Mode::Off;
    engine::u32 shadow_size = engine::renderer::kShadowMapSize;
};

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
            if (input.keys_down[static_cast<engine::usize>(Key::W)])
                position = position + forward * speed;
            if (input.keys_down[static_cast<engine::usize>(Key::S)])
                position = position - forward * speed;
            if (input.keys_down[static_cast<engine::usize>(Key::A)])
                position = position - right * speed;
            if (input.keys_down[static_cast<engine::usize>(Key::D)])
                position = position + right * speed;
            if (input.keys_down[static_cast<engine::usize>(Key::E)])
                position = position + up * speed;
            if (input.keys_down[static_cast<engine::usize>(Key::Q)])
                position = position - up * speed;
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

    engine::math::Mat4 projection(engine::f32 aspect, bool reversed_z = false) const {
        const engine::f32 fov = engine::math::radians(60.f);
        return reversed_z
            ? engine::math::Mat4::perspective_reversed_z(fov, aspect, 0.1f, 100.f)
            : engine::math::Mat4::perspective(fov, aspect, 0.1f, 100.f);
    }
};

struct ForwardDemo {
    engine::renderer::FramePipelines pipelines;
    std::vector<std::unique_ptr<engine::rhi::IGraphicsPipeline>> owned;
    // Takes ownership and points `field` at the new pipeline. Replaces that
    // field's previous owner rather than appending: hot reload fires on every
    // shader save, and appending would retain every superseded pipeline for the
    // life of the process. `owned` holds exactly the live set.
    void adopt(engine::rhi::IGraphicsPipeline* engine::renderer::FramePipelines::*field,
        std::unique_ptr<engine::rhi::IGraphicsPipeline> p) {
        engine::rhi::IGraphicsPipeline* const previous = pipelines.*field;
        pipelines.*field = p.get();
        if (previous != nullptr) {
            for (auto& slot : owned) {
                if (slot.get() == previous) {
                    slot = std::move(p);
                    return;
                }
            }
        }
        owned.push_back(std::move(p));
    }
    std::unique_ptr<engine::rhi::ITexture> taa_history[2];
    engine::u32 taa_history_w = 0;
    engine::u32 taa_history_h = 0;
    engine::u32 taa_frames = 0;
    bool taa_history_valid = false;
    engine::renderer::motion::MotionHistory motion_history{};
    engine::renderer::aa::Mode aa_mode = engine::renderer::aa::kDefault;
    // Linear multiplier, converted from the r.exposure EV knob at startup.
    engine::f32 exposure = 1.f;
    engine::assets::gpu::GpuMeshStore meshes;
    engine::assets::MeshHandle husky{};
    engine::assets::MeshHandle ground{};
    // Every material texture the demo owns, including the three built-in
    // defaults, so the store is the one thing that decides what a handle
    // resolves to. Declared after `meshes` and before nothing that outlives
    // the device: entries own their rhi::ITexture, so the store has to die
    // first (see texture_store.hpp).
    engine::assets::gpu::GpuTextureStore textures;
    engine::assets::TextureHandle husky_albedos[kHuskyVariantCount]{};
    engine::assets::TextureHandle floor_albedo{};
    engine::assets::TextureHandle default_albedo{};
    engine::assets::TextureHandle default_normal{};
    engine::assets::TextureHandle default_mr{};
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
    // Resolved from r.quality / r.shadow_size in setup_forward_demo, consumed by
    // setup_render_graph. Those run in that order in run_app.
    engine::u32 shadow_map_size = engine::renderer::kShadowMapSize;
};

// Compile a vs/ps pair out of one .hlsl and build its pipeline.
//
// Every pass in the standard frame did this as the same sixteen lines with
// three things changed: the shader path, the label, and the desc builder.
// Six passes already used compile_fullscreen_hlsl for the first half; this
// closes the other half so adding a pass is one call rather than a paragraph
// that is easy to paste slightly wrong.
using MakePipelineDesc = engine::rhi::GraphicsPipelineDesc (*)(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention);

// ── declarations ──
extern engine::Cvar cv_gate_bool;

extern engine::Cvar cv_gate_int;

extern engine::Cvar cv_gate_float;

extern engine::Cvar cv_gate_string;

extern engine::Cvar cv_gate_prec;

extern engine::Cvar cv_gate_args;

extern engine::Cvar cv_gate_file;

extern engine::Cvar cv_text_int;

extern engine::Cvar cv_text_float;

extern engine::Cvar cv_text_string;

extern engine::Cvar cv_text_eq;

extern engine::Cvar cv_text_prec;

extern engine::Cvar cv_text_comment;

extern engine::Cvar cv_aa;

extern engine::Cvar cv_exposure;

extern engine::Cvar cv_quality;

extern engine::Cvar cv_shadow_size;

extern engine::Cvar cv_fuzz_seed;

extern engine::Cvar cv_fuzz_iterations;

bool valid_shadow_size(engine::u32 texels);

bool resolve_quality(std::string_view preset, const engine::renderer::aa::Mode* explicit_aa,
    const engine::u32* explicit_shadow, QualitySettings& out);

QualitySettings resolve_quality_from_cvars(bool warn);

void toggle_walk_mode(SandboxState& state);

engine::scene_render::WorldExtractAssets make_extract_assets(ForwardDemo& demo);

bool ensure_taa_history(engine::rhi::IDevice& device, engine::renderer::RenderGraph& graph,
    ForwardDemo& demo, engine::u32 width, engine::u32 height);

// The forward pipeline with alpha blending and no depth write.
//
// Calls make_forward_pipeline_desc and overrides two fields rather than
// copying its body. The two must not drift, and the field most likely to
// drift is the depth convention - which is the engine's most load-bearing
// value, and the reason run_depth_convention_gate exists.
engine::rhi::GraphicsPipelineDesc make_forward_transparent_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention convention);

engine::rhi::GraphicsPipelineDesc make_forward_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention convention);

engine::rhi::GraphicsPipelineDesc make_shadow_pipeline_desc(
    std::span<const engine::u8> vs, engine::rhi::DepthConvention convention);

engine::rhi::GraphicsPipelineDesc make_tonemap_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention);

engine::rhi::GraphicsPipelineDesc make_sky_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention convention);

engine::rhi::GraphicsPipelineDesc make_bloom_downsample_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention);

engine::rhi::GraphicsPipelineDesc make_bloom_upsample_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention);

engine::rhi::GraphicsPipelineDesc make_fxaa_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention);

engine::rhi::GraphicsPipelineDesc make_smaa_edge_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention);

engine::rhi::GraphicsPipelineDesc make_smaa_weights_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention);

engine::rhi::GraphicsPipelineDesc make_smaa_blend_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention);

engine::rhi::GraphicsPipelineDesc make_motion_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention);

engine::rhi::GraphicsPipelineDesc make_taa_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention);

engine::rhi::GraphicsPipelineDesc make_tonemap_aces_pipeline_desc(
    std::span<const engine::u8> vs, std::span<const engine::u8> ps,
    engine::rhi::DepthConvention);

bool mount_app_content(engine::assets::IAssetLoader& loader,
    const engine::ContentMountPaths& mounts);

bool resolve_content(engine::assets::IAssetLoader& loader, std::string_view virtual_path,
    std::string& out_physical);

// The bytecode kind this device consumes - the single place the app maps one
// vocabulary onto the other. `rhi` cannot name ShaderTarget (it would have to
// depend on `shaders`, which points the wrong way) and `shaders` has no reason
// to know about devices, so the mapping belongs here, in the app that holds
// both.
//
// Every shader the app compiles goes through this. A ShaderCompileDesc left at
// its default asks for DXIL, which a Vulkan device rejects at pipeline
// creation - so a missed site is a pass that silently does not exist rather
// than a failure that shows up red.
engine::shaders::ShaderTarget shader_target_for(const engine::rhi::IDevice& device);

// The name a gate message should print for this device. Beside the target
// because the two are always wanted together and getting one right while the
// other stays hard-coded produces a gate that reports the wrong backend - the
// depth gate printed `convention=standard` that way, and the storage-texture
// gate printed `[d3d12]` while running on Vulkan.
const char* api_name_for(const engine::rhi::IDevice& device);

bool compile_fullscreen_hlsl(engine::shaders::IShaderCompiler& compiler, const std::string& path,
    engine::shaders::ShaderTarget target, const char* fail_label,
    engine::shaders::ShaderBytecode& vs_out, engine::shaders::ShaderBytecode& ps_out);

bool build_fullscreen_pipeline(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& path, const char* name,
    MakePipelineDesc make_desc, std::unique_ptr<engine::rhi::IGraphicsPipeline>& out);

std::string read_text_file(const std::filesystem::path& path);

// Colour space: sRGB decode on the way in, sRGB encode on the way out.
//
// Four independent assertions, all against numbers derived from IEC 61966-2-1
// rather than from this code:
//
//   1. srgb_to_linear(0.5) == 0.214041. The midpoint anchor.
//   2. linear_to_srgb(0.001) == 0.01292 (12.92 * 0.001). A pow(1/2.2)
//      approximation gives 0.0195, so only the piecewise curve passes. This is
//      the assertion that discriminates.
//   3. A 2x2 half-black half-white image, mipped. Averaging encoded bytes
//      gives 127; averaging light gives 0.5 linear, which encodes to 188.
//      Alpha must still average to 127 - sRGB formats leave alpha alone, and
//      gamma-correcting it would corrupt it.
//   4. The HLSL curve matches the C++ one, read back from a compute dispatch.
//      The curve exists twice by necessity; this is what keeps them in step.
// EV stops -> linear multiplier. Clamped because a cvar is user input: 2^40 is
// inf in f32 and would poison scene_color, and the exposure gate asserts the
// clamp rather than trusting the range.
engine::f32 exposure_from_ev(engine::f32 ev);

// Texels the draw covered rather than left at the clear colour - any channel of
// RGB non-zero.
//
// Shared so both backends are counted by the same code. A per-backend counter
// is a place for a parity comparison to be wrong in the *measurement* instead
// of in the backend, which is the one failure a parity gate must not have.
engine::u32 count_lit_texels(const engine::u8* rgba, engine::u32 width, engine::u32 height);

} // namespace sandbox
