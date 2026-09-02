#include "gate_registry.hpp"

#include <cstdio>

namespace sandbox {

// Order matches gates.hpp, which matches the order the gates are defined in.
// Cpu entries carry a lambda that adapts the gate's own signature to what a
// headless run can supply; Gpu entries carry nullptr and are called directly
// from main.cpp, where the device and the built demo exist.
const GateEntry kGates[] = {
    {"run_parser_fuzz_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_parser_fuzz_gate(); }},
    {"run_mount_gate", GateKind::Cpu,
        [](const CpuGateContext& ctx) { return run_mount_gate(*ctx.loader); }},
    {"run_mount_containment_gate", GateKind::Cpu,
        [](const CpuGateContext& ctx) { return run_mount_containment_gate(*ctx.loader); }},
    {"run_handle_gate", GateKind::Gpu, nullptr},
    {"run_handle_unload_gate", GateKind::Gpu, nullptr},
    {"run_mesh_reload_gate", GateKind::Gpu, nullptr},
    {"run_cook_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_cook_gate(); }},
    {"run_pak_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_pak_gate(); }},
    {"run_pack_gate", GateKind::Gpu, nullptr},
    {"run_albedo_gate", GateKind::Gpu, nullptr},
    {"run_gltf_gate", GateKind::Gpu, nullptr},
    {"run_gltf_extras_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_gltf_extras_gate(); }},
    {"run_gltf_validate_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_gltf_validate_gate(); }},
    {"run_gltf_node_transform_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_gltf_node_transform_gate(); }},
    {"run_husky_mesh_gate", GateKind::Gpu, nullptr},
    {"run_aabb_gate", GateKind::Gpu, nullptr},
    {"run_aabb_transform_gate", GateKind::Gpu, nullptr},
    {"run_gate_registry_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_gate_registry_gate(); }},
    {"run_file_log_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_file_log_gate(); }},
    {"run_arena_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_arena_gate(); }},
    {"run_frame_timer_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_frame_timer_gate(); }},
    {"run_math_guard_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_math_guard_gate(); }},
    {"run_cvar_gate", GateKind::Cpu,
        [](const CpuGateContext& ctx) { return run_cvar_gate(ctx.fs, ctx.scratch_dir); }},
    {"run_identity_gate", GateKind::Gpu, nullptr},
    {"run_ship_gate", GateKind::Gpu, nullptr},
    {"run_build_gate", GateKind::Cpu,
        [](const CpuGateContext& ctx) { return run_build_gate(ctx.layout); }},
    {"run_exposure_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_exposure_gate(); }},
    {"run_quality_preset_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_quality_preset_gate(); }},
    {"run_physics_gate", GateKind::Cpu,
        [](const CpuGateContext& ctx) { return run_physics_gate(ctx.physics); }},
    {"run_physics_body_gate", GateKind::Cpu,
        [](const CpuGateContext& ctx) { return run_physics_body_gate(ctx.physics); }},
    {"run_physics_capsule_gate", GateKind::Cpu,
        [](const CpuGateContext& ctx) { return run_physics_capsule_gate(ctx.physics); }},
    {"run_physics_trigger_gate", GateKind::Cpu,
        [](const CpuGateContext& ctx) { return run_physics_trigger_gate(ctx.physics); }},
    {"run_physics_raycast_gate", GateKind::Cpu,
        [](const CpuGateContext& ctx) { return run_physics_raycast_gate(ctx.physics); }},
    {"run_character_gate", GateKind::Cpu,
        [](const CpuGateContext& ctx) { return run_character_gate(ctx.physics); }},
    {"run_minidump_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_minidump_gate(); }},
    {"run_window_gate", GateKind::Gpu, nullptr},
    {"run_audio_gate", GateKind::Gpu, nullptr},
    {"run_gamepad_gate", GateKind::Gpu, nullptr},
    {"run_two_draw_items_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_two_draw_items_gate(); }},
    {"run_pipeline_set_gate", GateKind::Gpu, nullptr},
    {"run_shadow_gate", GateKind::Gpu, nullptr},
    {"run_hdr_gate", GateKind::Gpu, nullptr},
    {"run_async_compile_gate", GateKind::Gpu, nullptr},
    {"run_frustum_gate", GateKind::Gpu, nullptr},
    {"run_material_gate", GateKind::Gpu, nullptr},
    {"run_pbr_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_pbr_gate(); }},
    {"run_ibl_gate", GateKind::Gpu, nullptr},
    {"run_sky_gate", GateKind::Gpu, nullptr},
    {"run_bloom_gate", GateKind::Gpu, nullptr},
    {"run_aa_gate", GateKind::Gpu, nullptr},
    {"run_taa_gate", GateKind::Gpu, nullptr},
    {"run_instance_capacity_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_instance_capacity_gate(); }},
    {"run_instancing_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_instancing_gate(); }},
    {"run_motion_gate", GateKind::Gpu, nullptr},
    {"run_pcf_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_pcf_gate(); }},
    {"run_depth_convention_gate", GateKind::Gpu, nullptr},
    {"run_compute_pass_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_compute_pass_gate(); }},
    {"run_graph_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_graph_gate(); }},
    {"run_pix_gate", GateKind::Gpu, nullptr},
    {"run_shader_cache_gate", GateKind::Gpu, nullptr},
    {"run_dxc_gate", GateKind::Gpu, nullptr},
    {"run_rhi_contract_gate", GateKind::Gpu, nullptr},
    {"run_color_space_gate", GateKind::Gpu, nullptr},
    {"run_storage_texture_gate", GateKind::Gpu, nullptr},
    {"run_msaa_gate", GateKind::Gpu, nullptr},
    {"run_spirv_gate", GateKind::Gpu, nullptr},
    {"run_vulkan_device_gate", GateKind::Gpu, nullptr},
    {"run_backend_parity_gate", GateKind::Gpu, nullptr},
    {"run_parity_mesh_gate", GateKind::Gpu, nullptr},
    {"run_parity_texture_gate", GateKind::Gpu, nullptr},
    {"run_parity_depth_gate", GateKind::Gpu, nullptr},
    {"run_rhi_impl_gate", GateKind::Gpu, nullptr},
    {"run_mip_gate", GateKind::Gpu, nullptr},
    {"run_aspect_gate", GateKind::Gpu, nullptr},
    {"run_frame_ring_budget_gate", GateKind::Gpu, nullptr},
    {"run_swap_gate", GateKind::Gpu, nullptr},
    {"run_scene_world_gate", GateKind::Gpu, nullptr},
    {"run_scene_load_gate", GateKind::Cpu,
        [](const CpuGateContext& ctx) { return run_scene_load_gate(*ctx.loader); }},
    {"run_scene_capacity_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_scene_capacity_gate(); }},
    {"run_scene_name_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_scene_name_gate(); }},
    {"run_scene_hierarchy_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_scene_hierarchy_gate(); }},
    {"run_scene_file_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_scene_file_gate(); }},
    {"run_scene_prefab_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_scene_prefab_gate(); }},
    {"run_light_gate", GateKind::Gpu, nullptr},
    {"run_camera_gate", GateKind::Cpu,
        [](const CpuGateContext&) { return run_camera_gate(); }},
};

const engine::usize kGateCount = sizeof(kGates) / sizeof(kGates[0]);

bool run_cpu_gates(const CpuGateContext& ctx) {
    bool all_ok = true;
    engine::u32 ran = 0;
    for (const GateEntry& entry : kGates) {
        if (entry.kind != GateKind::Cpu) {
            continue;
        }
        ENGINE_ASSERT_MSG(entry.cpu_fn != nullptr, "a Cpu gate entry must carry a function");
        // Not `&&`: short-circuiting would let one red gate hide the next, which
        // is finding S3 and is fixed everywhere else in this file's ancestry.
        if (!entry.cpu_fn(ctx)) {
            all_ok = false;
        }
        ++ran;
    }
    char message[160];
    std::snprintf(message, sizeof(message),
        "Headless gates: ran=%u of %u registered (%s)", ran,
        static_cast<engine::u32>(kGateCount), all_ok ? "pass" : "FAIL");
    engine::log(all_ok ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return all_ok;
}

} // namespace sandbox
