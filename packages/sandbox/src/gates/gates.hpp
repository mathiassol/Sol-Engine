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
bool run_msaa_gate(engine::rhi::IDevice& device, engine::shaders::IShaderCompiler& compiler,
    const std::string& shader_path);

bool run_storage_texture_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path);

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
