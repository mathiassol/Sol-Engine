#pragma once

// Every gate, declared once. The definitions live in gates_<domain>.cpp beside
// this file; main.cpp calls them and owns the sequence.
//
// A gate is a plain function asserting on real values and logging a line ending
// (pass) or (FAIL) - see "What a gate is" in CLAUDE.md. What changed is only
// where they live: main.cpp held all 72 and was 26% of the engine.

#include "../sandbox_common.hpp"

namespace sandbox {

// ── assets ──
bool run_parser_fuzz_gate();

bool run_mount_gate(engine::assets::IAssetLoader& loader);

bool run_mount_containment_gate(engine::assets::IAssetLoader& loader);

bool run_handle_gate(engine::assets::gpu::GpuMeshStore& store, engine::rhi::IDevice& device,
    const engine::assets::MeshData& mesh_data);

bool run_handle_unload_gate(engine::assets::gpu::GpuMeshStore& store, engine::rhi::IDevice& device,
    const engine::assets::MeshData& mesh_data);

bool run_mesh_reload_gate(engine::rhi::IDevice& device, const engine::assets::MeshData& mesh_data);

bool run_cook_gate();

bool run_pak_gate();

bool run_pack_gate(engine::Engine& app);

bool run_albedo_gate(const engine::assets::ImageData& image,
    const engine::rhi::ITexture& texture);

bool run_gltf_gate(const engine::assets::gltf::GltfLoadResult& loaded);

bool run_gltf_extras_gate();

bool run_gltf_validate_gate();

bool run_gltf_node_transform_gate();

bool run_husky_mesh_gate(const engine::assets::MeshData& mesh);

bool run_aabb_gate(const engine::assets::MeshData& mesh);

bool run_aabb_transform_gate(const engine::assets::MeshData& mesh);

// ── core ──
bool run_gate_registry_gate();

bool run_file_log_gate();

bool run_arena_gate();

bool run_frame_timer_gate();

bool run_math_guard_gate();

bool run_cvar_gate(engine::platform::IFileSystem* fs, const std::string& scratch_dir);

bool run_identity_gate(engine::Engine& app);

bool run_ship_gate(engine::Engine& app);

bool run_build_gate(engine::ContentLayout layout);

bool run_exposure_gate();

bool run_quality_preset_gate();

bool run_reflect_gate();

// ── physics ──
bool run_physics_gate(engine::physics::IPhysics* physics);

bool run_physics_body_gate(engine::physics::IPhysics* physics);

bool run_physics_capsule_gate(engine::physics::IPhysics* physics);

bool run_physics_trigger_gate(engine::physics::IPhysics* physics);

bool run_physics_raycast_gate(engine::physics::IPhysics* physics);

bool run_character_gate(engine::physics::IPhysics* physics);

// ── platform ──
bool run_minidump_gate();

bool run_window_gate(engine::platform::IWindow* window, engine::rhi::IDevice* device);

bool run_audio_gate(engine::audio::IAudio* audio);

bool run_gamepad_gate(engine::platform::IInput* input);

// ── renderer ──
bool run_two_draw_items_gate();

bool run_pipeline_set_gate(const engine::renderer::FramePipelines& pipelines);

bool run_shadow_gate(const engine::scene::World& world,
    const engine::assets::gpu::GpuMeshStore& meshes,
    const engine::rhi::IGraphicsPipeline* shadow_pipeline);

bool run_hdr_gate(const engine::scene::World& world,
    const engine::rhi::IGraphicsPipeline* tonemap_pipeline);

bool run_async_compile_gate(engine::shaders::IShaderHotReloader& watcher);

bool run_frustum_gate(const engine::scene::World& world, const FlyCamera& camera,
    const sandbox::WorldExtractAssets& assets);

bool run_material_gate(const engine::scene::World& world, const FlyCamera& camera,
    const sandbox::WorldExtractAssets& assets, engine::f32 gltf_metallic,
    engine::f32 gltf_roughness);

bool run_pbr_gate();

bool run_ibl_gate(const ForwardDemo& demo);

bool run_sky_gate(const ForwardDemo& demo);

bool run_bloom_gate(const ForwardDemo& demo);

bool run_aa_gate(const ForwardDemo& demo);

bool run_taa_gate(const ForwardDemo& demo);

bool run_instance_capacity_gate();

bool run_instancing_gate();

bool run_motion_gate(const ForwardDemo& demo);

bool run_pcf_gate();

// RHI #25: the frame loop itself, which nothing covered.
//
// Every other GPU gate drives begin_frame / record / submit / wait_idle /
// read_texture and **never presents**. So the whole present path - acquire,
// the swapchain semaphores, the layout the backbuffer must be in, and the
// driver actually executing a full 26-pass frame - had no coverage at all,
// and RHI #24 shipped a Vulkan backend whose suite was green and whose first
// live frame crashed.
//
// This runs the real compiled graph through the real extract, the same way
// Engine::render() does, for enough frames to wrap the slot ring and cycle
// the swapchain images. `extract` is the app's own on_extract callback rather
// than a copy of it: a gate that built its own snapshot would stop resembling
// the frame it is meant to protect.
bool run_frame_loop_gate(engine::rhi::IDevice& device, engine::renderer::RenderGraph& graph,
    const std::function<void(engine::renderer::RenderSnapshot&, engine::Arena&)>& extract,
    const char* api);

// Renderer #16: alpha blending, measured rather than asserted. An opaque
// underlay, a blended overlay, and a readback compared against
// src*a + dst*(1-a) computed on the CPU.
//
// Takes `target` and `api` so the same function runs on both devices and the
// two sets of numbers sit next to each other - the shape RHI #24 established,
// and worth having here because this is the first geometry consumer either
// backend has had for BlendMode::Alpha. Both were written from the same
// description and nothing had compared them.
bool run_transparency_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api);

bool run_depth_convention_gate(const engine::rhi::IDevice* device);

bool run_compute_pass_gate();

bool run_graph_gate();

// ── rhi ──
bool run_pix_gate(engine::rhi::IDevice* device);

bool run_shader_cache_gate(engine::shaders::IShaderCompiler& compiler,
    const engine::shaders::ShaderCompileDesc& desc);

bool run_dxc_gate(const engine::shaders::ShaderCompileDesc& desc,
    const engine::shaders::ShaderBytecode& bytecode);

bool run_rhi_contract_gate(
    engine::rhi::IDevice& device, engine::shaders::IShaderCompiler& compiler);

bool run_color_space_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& srgb_gate_path);

// RHI #12: the Vulkan device stands up and reports what was asked for. Stands
// up its own device, so it runs on the D3D12 build too rather than only behind
// a flag - a gate behind a flag is a gate that rots.
#ifdef ENGINE_HAS_VULKAN
bool run_vulkan_device_gate();
#endif

// Shaders #5: SPIR-V really came from a SPIR-V-capable compiler, and the DXIL
// path still works from the same instance. Gpu-classified despite touching no
// device - it needs a DLL that ships with a GPU SDK.
bool run_spirv_gate(engine::shaders::IShaderCompiler& compiler, const std::string& shader_path);

// RHI #24: the first parity assertion about time rather than a pixel. Four
// frames with no wait between them, so the run wraps past the slot count and
// reuses slot 0 - which is where a missing fence wait, a pool reset in flight
// or a reused command buffer actually bites.
bool run_parity_frames_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api);

// RHI #24: depth parity. Three full-target draws where the middle one is
// nearer under the device's own convention, so the probe distinguishes no depth
// test, an inverted compare, a wrong clear value and disabled depth writes -
// four failures that look identical in a screenshot.
bool run_parity_depth_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api);

// RHI #24: texture parity. A hand-written mip level and a cube face, so the
// two things that go wrong quietly on upload - the mip offset and the array
// layer - are each read at a value the gate chose.
bool run_parity_texture_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api);

// RHI #24: vertex input parity. An indexed quad whose per-vertex normal and uv
// both reach the output, so a wrong attribute location is a wrong colour rather
// than a missing triangle.
bool run_parity_mesh_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api);

// RHI #12: the same shader, the same asserted pixels, once per backend. Takes
// the device and the shader target, so one function covers both - a per-backend
// copy would be a place for the parity comparison to be wrong in the test
// rather than in the backend. `lit_out` goes to the caller, which compares.
bool run_backend_parity_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api, engine::u32& lit_out);

// RHI #18: a 4x target reports its count, a mismatched pipeline is diagnosed by
// name, the resolve lands single-sample, and the resolved edge has the partial
// coverage a single-sample raster cannot produce.
// Called once per backend since RHI #24. It already asserts exact coverage
// numbers and that a mismatched pipeline is diagnosed by name, so running the
// same function against the second device is a stronger resolve parity check
// than a new gate would be.
// `target` and `api` are required, deliberately. They defaulted to DXIL and
// "d3d12", and the live-device call sites took the defaults - so on a Vulkan
// session this gate fed DXIL to a Vulkan device and reported the failure as
// `[d3d12]`. A default that is right for one backend is a trap on the second.
bool run_msaa_gate(engine::rhi::IDevice& device, engine::shaders::IShaderCompiler& compiler,
    const std::string& shader_path, engine::shaders::ShaderTarget target, const char* api);

// Called once per backend since RHI #24. It asserts exact packed probe values,
// so running the same function against the second device is a stronger compute
// parity check than a new gate with new expected numbers would be - and it
// covers create_compute_pipeline, dispatch, a storage image and a storage
// buffer in one pass. The `api` label only reaches the log line.
// Required for the same reason as run_msaa_gate's.
bool run_storage_texture_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api);

bool run_rhi_impl_gate(engine::rhi::IDevice& device, engine::shaders::IShaderCompiler& compiler,
    const std::string& compute_path);

bool run_mip_gate(const engine::rhi::ITexture& texture, engine::u32 source_width);

bool run_aspect_gate();

bool run_frame_ring_budget_gate(const engine::rhi::IDevice& device);

bool run_swap_gate();

// ── scene ──
bool run_scene_world_gate(const engine::scene::World& world);

bool run_scene_load_gate(engine::assets::IAssetLoader& loader);

bool run_scene_capacity_gate();

bool run_scene_name_gate();

bool run_scene_hierarchy_gate();

bool run_scene_file_gate();

bool run_scene_prefab_gate();

bool run_light_gate(const engine::scene::World& world);

bool run_camera_gate();

} // namespace sandbox
