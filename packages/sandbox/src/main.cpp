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
#include <engine/debug/debug_lines.hpp>
#include <engine/debug/frame_stats.hpp>
#include <engine/debug/stats_overlay.hpp>
#include <engine/shaders/shader_hot_reload.hpp>

#include "world_extract.hpp"
#include "sandbox_common.hpp"
#include "gates/gates.hpp"
#include "gates/gate_registry.hpp"

#ifdef ENGINE_HAS_WIN32_PLATFORM
#include <engine/platform/win32/crash_win32.hpp>
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

#ifdef ENGINE_HAS_VULKAN
#include <engine/rhi/vulkan/rhi_vulkan.hpp>
#endif

// shaders-dxc used to be added under the same if() as rhi-d3d12, so one guard
// covered both. Since Shaders #5 it is added for either backend - DXC emits
// DXIL for one and SPIR-V for the other from the same HLSL - so either define
// means it is present.
#if defined(ENGINE_HAS_D3D12) || defined(ENGINE_HAS_VULKAN)
#include <engine/shaders/dxc/shader_compiler_dxc.hpp>
#include <engine/shaders/dxc/shader_hot_reload_dxc.hpp>
#endif

#ifdef ENGINE_HAS_PNG
#include <engine/assets/png/image_loader_png.hpp>
#endif

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

#ifndef ENGINE_APP_FILE_VERSION
#define ENGINE_APP_FILE_VERSION ""
#endif

namespace {

// Shared with the gate files. `using namespace` here, and only here:
// main.cpp is where every one of these was defined until they moved, so it
// reads exactly as it did. The gate files qualify or pull in what they need.
using namespace sandbox;














































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
#ifdef ENGINE_HAS_PNG
    if (!engine::assets::png::load_png_bytes(png_bytes, image)) {
        return false;
    }
#else
    // No PNG decoder compiled in. Refuse rather than upload an empty texture:
    // a black albedo looks like a lighting bug and costs an afternoon to trace.
    engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
        "No PNG decoder compiled in - cannot load albedo textures");
    return false;
#endif
    engine::rhi::TextureDesc albedo_desc{};
    albedo_desc.width = image.width;
    albedo_desc.height = image.height;
    // Albedo is colour, so it is sRGB-encoded on disk and the hardware must
    // decode it before the forward pass does any lighting maths with it. Data
    // maps — metallic-roughness, normals — stay plain RGBA8_UNORM.
    albedo_desc.format = engine::rhi::Format::RGBA8_UNORM_SRGB;
    albedo_desc.usage = engine::rhi::TextureUsage::ShaderResource;
    albedo_desc.mip_levels = 0;
    out = device.create_texture(albedo_desc, image.rgba.data());
    if (!out) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Albedo texture creation failed");
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
        make_forward_pipeline_desc(
            vs_bytecode.data, ps_bytecode.data, device.depth_convention()));
    if (!pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Shader hot-reload pipeline creation failed");
        return;
    }

    demo.adopt(&engine::renderer::FramePipelines::forward, std::move(pipeline));
    engine::log(engine::LogLevel::Info, engine::LogChannel::Render, "Shader hot-reload applied");
}




[[maybe_unused]] bool setup_render_graph(engine::Engine& app, SandboxState& state) {
    engine::renderer::StandardFrameDesc desc{};
    desc.shadow_map_size = state.shadow_map_size;
    desc.draw_debug_lines = [&state](engine::renderer::PassContext& ctx) {
        state.debug_lines.draw(ctx.cmd, ctx.snapshot.view, ctx.snapshot.projection);
    };
    desc.draw_overlay = [&state](engine::renderer::PassContext& ctx) {
        state.overlay.draw(ctx.cmd, ctx.snapshot.width, ctx.snapshot.height);
    };
    return engine::renderer::setup_standard_frame(app.render_graph(), std::move(desc));
}

[[maybe_unused]] bool setup_stats_overlay(engine::Engine& app, engine::assets::IAssetLoader& loader,
    engine::shaders::IShaderCompiler& compiler, SandboxState& state) {
    auto* device = app.device();
    if (!device) return false;

    std::string overlay_shader;
    if (!resolve_content(loader, kOverlayShader, overlay_shader)) {
        return false;
    }

    if (!state.overlay.init(*device, compiler, overlay_shader, shader_target_for(*device))) {
        return false;
    }

    engine::log(engine::LogLevel::Info, engine::LogChannel::Render,
        "Stats overlay ready (F3 to toggle)");
    return true;
}

[[maybe_unused]] bool setup_debug_lines(engine::Engine& app, engine::assets::IAssetLoader& loader,
    engine::shaders::IShaderCompiler& compiler, SandboxState& state) {
    auto* device = app.device();
    if (!device) return false;

    std::string shader_path;
    if (!resolve_content(loader, kDebugLinesShader, shader_path)) {
        return false;
    }

    if (!state.debug_lines.init(
            *device, compiler, shader_path, shader_target_for(*device))) {
        return false;
    }

    engine::log(engine::LogLevel::Info, engine::LogChannel::Render,
        "Debug lines ready (F4 to toggle instance AABBs)");
    return true;
}

[[maybe_unused]] bool setup_forward_demo(engine::Engine& app, engine::assets::IAssetLoader& loader,
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
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            "Failed to load cube mesh");
        return false;
    }
    auto gltf_loader = engine::assets::gltf::create_mesh_loader();
    engine::assets::gltf::GltfLoadResult husky_gltf;
    if (!gltf_loader->load(husky_path, husky_gltf)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            "Failed to load husky glTF");
        return false;
    }
    if (!run_gltf_gate(husky_gltf) && fail_on_gate) {
        return false;
    }
    if (!run_gltf_extras_gate() && fail_on_gate) {
        return false;
    }
    if (!run_gltf_validate_gate() && fail_on_gate) {
        return false;
    }
    if (!run_gltf_node_transform_gate() && fail_on_gate) {
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
    // Every desc below is copied from this one, so this is the only line that
    // has to be right - and it is the line that was missing, which cost the
    // forward pass, the overlay, the debug lines and the fifty gates they own.
    vs_desc.target = shader_target_for(*device);

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

    std::string storage_tex_path;
    if (!resolve_content(loader, kStorageTextureGateShader, storage_tex_path)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "storage_texture_gate.hlsl missing");
        if (fail_on_gate) {
            return false;
        }
    } else if (!run_storage_texture_gate(*device, compiler, storage_tex_path,
                   shader_target_for(*device), api_name_for(*device))
        && fail_on_gate) {
        return false;
    }

    std::string msaa_path;
    if (!resolve_content(loader, kMsaaGateShader, msaa_path)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "msaa_gate.hlsl missing");
        if (fail_on_gate) {
            return false;
        }
    } else if (!run_msaa_gate(*device, compiler, msaa_path, shader_target_for(*device),
                   api_name_for(*device))
        && fail_on_gate) {
        return false;
    }

    std::string transparency_path;
    if (!resolve_content(loader, kTransparencyGateShader, transparency_path)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "transparency_gate.hlsl missing");
        if (fail_on_gate) {
            return false;
        }
    } else if (!run_transparency_gate(*device, compiler, transparency_path,
                   shader_target_for(*device), api_name_for(*device))
        && fail_on_gate) {
        return false;
    }

    // An offscreen device, not the sandbox's windowed one: the contract's new
    // null-window mode needs a caller, and this gate is the only one that wants
    // a device with no swapchain. It also means the reference pixels are
    // established through the *new* API before a second backend exists to
    // disagree with them.
    //
    // parity_path is declared here and used again by the Vulkan call site
    // below: resolving the same mount twice is two places for them to differ.
#ifdef ENGINE_HAS_VULKAN
    if (!run_vulkan_device_gate() && fail_on_gate) {
        return false;
    }
#endif

    std::string parity_path;
    std::string mesh_path;
    std::string texture_path;
    std::string depth_path;
    (void)resolve_content(loader, kParityMeshGateShader, mesh_path);
    (void)resolve_content(loader, kParityTextureGateShader, texture_path);
    (void)resolve_content(loader, kParityDepthGateShader, depth_path);
    engine::u32 d3d12_lit = 0;
    if (!resolve_content(loader, kBackendParityGateShader, parity_path)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "backend_parity_gate.hlsl missing");
        if (fail_on_gate) {
            return false;
        }
    } else if (!run_spirv_gate(compiler, parity_path) && fail_on_gate) {
        // Before the parity gates, because the Vulkan one depends on SPIR-V
        // working and a failure here should name that rather than surface as a
        // pipeline that would not create.
        return false;
    }
#ifdef ENGINE_HAS_D3D12
    // Guarded like the Vulkan block below. This function takes an IDevice& and
    // is otherwise backend-agnostic, so it compiles off Windows - naming a
    // backend factory in it is the one thing that breaks that, and the Linux
    // job is what caught it.
    else if (!parity_path.empty()) {
        engine::rhi::DeviceDesc offscreen{};
        offscreen.window_handle = nullptr;
        offscreen.width = 64;
        offscreen.height = 64;
        // Inherited, not defaulted. A parity device on DeviceDesc's default
        // Standard convention would exercise a configuration the engine never
        // runs in - the depth gate printed `convention=standard` and gave this
        // away.
        offscreen.depth_convention = device->depth_convention();
        auto offscreen_rhi = engine::rhi::d3d12::create_rhi();
        auto offscreen_device = offscreen_rhi ? offscreen_rhi->create_device(offscreen) : nullptr;
        if (!offscreen_device) {
            // A FAIL, not a skip: this build compiled the D3D12 backend and the
            // windowed device already came up, so a null-window one failing is
            // the new contract mode being broken, not the environment.
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
                "Backend parity gate [d3d12]: offscreen device creation failed (FAIL)");
            if (fail_on_gate) {
                return false;
            }
        } else if (!run_backend_parity_gate(*offscreen_device, compiler, parity_path,
                       engine::shaders::ShaderTarget::Dxil, "d3d12", d3d12_lit)
            && fail_on_gate) {
            return false;
        } else if (!mesh_path.empty()
            && !run_parity_mesh_gate(*offscreen_device, compiler, mesh_path,
                engine::shaders::ShaderTarget::Dxil, "d3d12")
            && fail_on_gate) {
            return false;
        } else if (!texture_path.empty()
            && !run_parity_texture_gate(*offscreen_device, compiler, texture_path,
                engine::shaders::ShaderTarget::Dxil, "d3d12")
            && fail_on_gate) {
            return false;
        } else if (!depth_path.empty()
            && !run_parity_depth_gate(*offscreen_device, compiler, depth_path,
                engine::shaders::ShaderTarget::Dxil, "d3d12")
            && fail_on_gate) {
            return false;
        } else if (!run_parity_frames_gate(*offscreen_device, compiler, parity_path,
                       engine::shaders::ShaderTarget::Dxil, "d3d12")
            && fail_on_gate) {
            return false;
        }
    }
#endif

    // The comparison this whole pass exists for. Reported even when it passes,
    // because "the two backends agree" means nothing without both numbers next
    // to each other.
    engine::u32 vulkan_lit = 0;
#ifdef ENGINE_HAS_VULKAN
    if (!parity_path.empty()) {
        engine::rhi::DeviceDesc vk_desc{};
        vk_desc.window_handle = nullptr;
        vk_desc.width = 64;
        vk_desc.height = 64;
        vk_desc.depth_convention = device->depth_convention();
        auto vk_rhi = engine::rhi::vulkan::create_rhi();
        auto vk_device = vk_rhi ? vk_rhi->create_device(vk_desc) : nullptr;
        if (!vk_device) {
            engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
                "Backend parity gate [vulkan]: no Vulkan device (skip)");
        } else if (!run_backend_parity_gate(*vk_device, compiler, parity_path,
                       engine::shaders::ShaderTarget::Spirv, "vulkan", vulkan_lit)
            && fail_on_gate) {
            return false;
        } else if (!mesh_path.empty()
            && !run_parity_mesh_gate(*vk_device, compiler, mesh_path,
                engine::shaders::ShaderTarget::Spirv, "vulkan")
            && fail_on_gate) {
            return false;
        } else if (!texture_path.empty()
            && !run_parity_texture_gate(*vk_device, compiler, texture_path,
                engine::shaders::ShaderTarget::Spirv, "vulkan")
            && fail_on_gate) {
            return false;
        } else if (!depth_path.empty()
            && !run_parity_depth_gate(*vk_device, compiler, depth_path,
                engine::shaders::ShaderTarget::Spirv, "vulkan")
            && fail_on_gate) {
            return false;
        } else if (!run_parity_frames_gate(*vk_device, compiler, parity_path,
                       engine::shaders::ShaderTarget::Spirv, "vulkan")
            && fail_on_gate) {
            return false;
        } else if (!storage_tex_path.empty()
            // The same gate the D3D12 device ran, against the Vulkan one. It
            // asserts exact packed probe values, so this is compute, a storage
            // image and a storage buffer all checked against numbers that were
            // already true rather than newly invented.
            && !run_storage_texture_gate(*vk_device, compiler, storage_tex_path,
                engine::shaders::ShaderTarget::Spirv, "vulkan")
            && fail_on_gate) {
            return false;
        } else if (!msaa_path.empty()
            // Likewise for the resolve: this gate already asserts 2,016 lit
            // texels, 64 partial-coverage ones and that a mismatched pipeline
            // is diagnosed by name.
            && !run_msaa_gate(*vk_device, compiler, msaa_path,
                engine::shaders::ShaderTarget::Spirv, "vulkan")
            && fail_on_gate) {
            return false;
        } else if (!transparency_path.empty()
            // Renderer #16. Both backends translate BlendMode::Alpha from the
            // same description and nothing had ever compared them, which is
            // exactly the kind of gap the parity block exists to close.
            && !run_transparency_gate(*vk_device, compiler, transparency_path,
                engine::shaders::ShaderTarget::Spirv, "vulkan")
            && fail_on_gate) {
            return false;
        }
    }
#endif

    if (d3d12_lit > 0 && vulkan_lit > 0) {
        const engine::u32 spread =
            d3d12_lit > vulkan_lit ? d3d12_lit - vulkan_lit : vulkan_lit - d3d12_lit;
        // One diagonal's worth of tolerance. Both APIs specify that a shared
        // edge is covered exactly once, but not identically enough to demand
        // equality on a 45-degree line - so the byte-exact probe texels inside
        // the gate are the assertion, and this is the corroboration.
        const bool agree = spread <= 64;
        char message[160];
        std::snprintf(message, sizeof(message),
            "Backend agreement gate: d3d12_lit=%u vulkan_lit=%u spread=%u (%s)", d3d12_lit,
            vulkan_lit, spread, agree ? "pass" : "FAIL");
        engine::log(agree ? engine::LogLevel::Info : engine::LogLevel::Error,
            engine::LogChannel::Render, message);
        if (!agree && fail_on_gate) {
            return false;
        }
    }

    std::string srgb_gate_path;
    if (!resolve_content(loader, kSrgbGateShader, srgb_gate_path)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "srgb_gate.hlsl missing");
        if (fail_on_gate) {
            return false;
        }
    } else if (!run_color_space_gate(*device, compiler, srgb_gate_path) && fail_on_gate) {
        return false;
    }

    if (!run_exposure_gate() && fail_on_gate) {
        return false;
    }

    auto demo = std::make_unique<ForwardDemo>();
    vs_desc.file_path = shader_path;
    ps_desc.file_path = shader_path;
    {
        auto p = device->create_graphics_pipeline(
            make_forward_pipeline_desc(
                vs_bytecode.data, ps_bytecode.data, device->depth_convention()));
        if (!p) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
                "Forward pipeline creation failed");
            return false;
        }
        demo->adopt(&engine::renderer::FramePipelines::forward, std::move(p));
    }

    engine::shaders::ShaderCompileDesc shadow_vs = vs_desc;
    shadow_vs.file_path = shadow_path;
    engine::shaders::ShaderBytecode shadow_bytecode;
    if (!compiler.compile(shadow_vs, shadow_bytecode, error)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Shadow vertex shader compile failed");
        return false;
    }
    {
        auto p = device->create_graphics_pipeline(
            make_shadow_pipeline_desc(shadow_bytecode.data, device->depth_convention()));
        if (!p) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
                "Shadow pipeline creation failed");
            return false;
        }
        demo->adopt(&engine::renderer::FramePipelines::shadow, std::move(p));
    }

    // Eleven passes, one shape: compile vs_main/ps_main out of one .hlsl,
    // build the pipeline, bail with a named message. This was eleven
    // near-identical sixteen-line blocks - six of which already went through
    // compile_fullscreen_hlsl while five hand-rolled the same thing.
    const struct {
        engine::rhi::IGraphicsPipeline* engine::renderer::FramePipelines::*field;
        const std::string& path;
        const char* name;
        MakePipelineDesc make_desc;
    } fullscreen_builds[] = {
        {&engine::renderer::FramePipelines::tonemap, tonemap_path, "Tonemap",
         make_tonemap_pipeline_desc},
        {&engine::renderer::FramePipelines::sky, sky_path, "Sky",
         make_sky_pipeline_desc},
        {&engine::renderer::FramePipelines::bloom_downsample, bloom_down_path, "Bloom downsample",
         make_bloom_downsample_pipeline_desc},
        {&engine::renderer::FramePipelines::bloom_upsample, bloom_up_path, "Bloom upsample",
         make_bloom_upsample_pipeline_desc},
        {&engine::renderer::FramePipelines::fxaa, fxaa_path, "FXAA",
         make_fxaa_pipeline_desc},
        {&engine::renderer::FramePipelines::smaa_edge, smaa_edge_path, "SMAA edge",
         make_smaa_edge_pipeline_desc},
        {&engine::renderer::FramePipelines::smaa_weights, smaa_weights_path, "SMAA weights",
         make_smaa_weights_pipeline_desc},
        {&engine::renderer::FramePipelines::smaa_blend, smaa_blend_path, "SMAA blend",
         make_smaa_blend_pipeline_desc},
        {&engine::renderer::FramePipelines::motion, motion_path, "Motion vector",
         make_motion_pipeline_desc},
        {&engine::renderer::FramePipelines::taa, taa_path, "TAA",
         make_taa_pipeline_desc},
        {&engine::renderer::FramePipelines::tonemap_aces, tonemap_aces_path, "ACES tonemap",
         make_tonemap_aces_pipeline_desc},
    };

    for (const auto& entry : fullscreen_builds) {
        std::unique_ptr<engine::rhi::IGraphicsPipeline> p;
        if (!build_fullscreen_pipeline(*device, compiler, entry.path, entry.name,
                entry.make_desc, p)) {
            return false;
        }
        demo->adopt(entry.field, std::move(p));
    }

    if (!run_handle_gate(demo->meshes, *device, cube_data)) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Assets, "MeshHandle gate failed");
        if (fail_on_gate) {
            return false;
        }
    }
    if (!run_handle_unload_gate(demo->meshes, *device, cube_data)) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Assets,
            "MeshHandle unload gate failed");
        if (fail_on_gate) {
            return false;
        }
    }
    demo->husky = demo->meshes.store(*device, kHuskyMesh, husky_data);
    const auto* gpu_mesh = demo->meshes.get(demo->husky);
    if (!gpu_mesh) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            "Husky mesh store is empty");
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
        if (i == 0 && !run_albedo_gate(image, *demo->albedos[i]) && fail_on_gate) {
            return false;
        }
        if (i == 0 && !run_mip_gate(*demo->albedos[i], image.width) && fail_on_gate) {
            return false;
        }
    }

    const engine::f32 foot_y = husky_data.bounds.min.y;
    state.husky_foot_y = foot_y;
    constexpr engine::u32 kHuskyCount = 63;
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
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            "Ground mesh store is empty");
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
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Floor albedo creation failed");
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
    // Retuned for the sRGB output encode (Renderer #28). These were 4.8/4.4/3.8
    // and 0.16/0.17/0.21, chosen by eye against a pipeline that wrote linear
    // values to a UNORM target - so roughly half the light was being lost to the
    // missing display encode and the constants had absorbed the difference.
    demo->world.sun.color = {2.0f, 1.85f, 1.6f};
    demo->world.ambient = {0.085f, 0.09f, 0.11f};
    demo->world.points[0].position = {-0.55f, 0.38f, 0.45f};
    demo->world.points[0].color = {1.f, 0.45f, 0.18f};
    demo->world.points[0].radius = 1.8f;
    demo->world.points[0].intensity = 2.2f;

    if (!run_scene_world_gate(demo->world) && fail_on_gate) {
        return false;
    }
    if (!run_scene_capacity_gate() && fail_on_gate) {
        return false;
    }
    if (!run_scene_load_gate(loader) && fail_on_gate) {
        return false;
    }
    if (!run_pipeline_set_gate(demo->pipelines) && fail_on_gate) {
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
    if (!run_shadow_gate(demo->world, demo->meshes, demo->pipelines.shadow) && fail_on_gate) {
        return false;
    }
    if (!run_hdr_gate(demo->world, demo->pipelines.tonemap) && fail_on_gate) {
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
    // Applied here, before run_aa_gate, so the gate can assert the knob-aware
    // startup mode rather than only the factory default.
    // Exposure is demo state for the same reason the AA mode is: the engine has
    // no opinion on how bright this scene should be. exposure_from_ev clamps, so
    // an absurd knob value cannot put inf into scene_color.
    demo->exposure = exposure_from_ev(cv_exposure.as_float());

    // r.quality supplies both knobs; an explicit r.aa or r.shadow_size wins.
    const QualitySettings quality = resolve_quality_from_cvars(true);
    demo->aa_mode = quality.aa;
    state.shadow_map_size = quality.shadow_size;
    if (!run_aa_gate(*demo) && fail_on_gate) {
        return false;
    }
    if (!run_quality_preset_gate() && fail_on_gate) {
        return false;
    }
    if (!run_instance_capacity_gate() && fail_on_gate) {
        return false;
    }
    if (!run_instancing_gate() && fail_on_gate) {
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
#ifdef ENGINE_HAS_D3D12
    demo->shader_watcher = engine::shaders::dxc::create_hot_reloader();
    demo->shader_watcher->begin_watch(demo->shader_sources);
    if (!run_async_compile_gate(*demo->shader_watcher) && fail_on_gate) {
        return false;
    }
#endif
    // Left null without a backend. poll_shader_reload already returns early on
    // a null watcher, so hot reload simply does not happen - it is a dev
    // convenience, not something the demo needs to stand up.

    state.forward = std::move(demo);
    engine::log(engine::LogLevel::Info, engine::LogChannel::Render,
        "Forward pass ready (AA default Off / F5 FXAA+SMAA+TAA, F11 windowed/borderless, "
        "Tab/Start walk, Enter/Y follow/orbit/FPS, Space/A jump, pad sticks, motion vectors, "
        "Karis bloom, source cubemap sky, split-sum IBL, PBR GGX, 16-tap Vogel PCF, materials, "
        "renderer-owned frame, 512 instances, frustum skip, async DXC, glTF husky + mips, HDR, "
        "F4 AABBs, Space beep, Z/X, WASD look)");
    return true;
}

} // namespace

// The engine body. main() below is only the exception boundary around it.
int run_app(int argc, char** argv) {
    // Named once rather than #ifdef'd at each message: a line that says
    // "unknown backend" without saying which ones exist sends the reader to the
    // source to find out.
    constexpr const char* kBackends =
#if defined(ENGINE_HAS_D3D12) && defined(ENGINE_HAS_VULKAN)
        "d3d12, vulkan";
#elif defined(ENGINE_HAS_D3D12)
        "d3d12";
#elif defined(ENGINE_HAS_VULKAN)
        "vulkan";
#else
        "none";
#endif

    bool gates_mode = false;
    bool headless_gates = false;
    // Which backend the session runs on. Both spellings, because --rhi=vulkan
    // is what gets typed and --rhi vulkan is what gets scripted.
    //
    // D3D12 by default where it exists - it is the shipped player backend - but
    // whatever this build *has* otherwise. A fixed "d3d12" default made a
    // Vulkan-only configure refuse to start on its own single available
    // backend, correctly worded and useless: `--rhi d3d12 was not compiled into
    // this build. This build has: vulkan.`
    std::string_view rhi_name =
#if defined(ENGINE_HAS_D3D12)
        "d3d12";
#elif defined(ENGINE_HAS_VULKAN)
        "vulkan";
#else
        "none";
#endif
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--gates") {
            gates_mode = true;
        }
        if (arg == "--gates-cpu") {
            headless_gates = true;
        }
        if (arg == "--rhi" && i + 1 < argc) {
            rhi_name = argv[i + 1];
        } else if (arg.starts_with("--rhi=")) {
            rhi_name = arg.substr(6);
        }
    }

    // Before anything is created. --gates-cpu exists to run where there is no
    // platform backend and no RHI, and the branch below this one is where a
    // build without a platform logs Fatal and gives up - so a headless run has
    // to return before reaching it, or it never runs on Linux at all.
    if (headless_gates) {
        engine::apply_cvar_args(argc, argv);
        return sandbox::run_headless_gates(argc > 0 ? argv[0] : nullptr);
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
    engine::EngineModules modules{};

#ifdef ENGINE_HAS_WIN32_PLATFORM
    modules.platform = engine::platform::win32::create_platform();
#else
    engine::log(engine::LogLevel::Fatal, engine::LogChannel::Platform, "No platform backend");
    return 1;
#endif

    // Earliest point the executable directory is known, and before the banner
    // below so the banner is the log's first line.
    //
    // Not in gates mode: --gates is a test harness, not a session, and two gate
    // runs would push a real crash log out of both log.txt and log.prev.txt.
    if (!gates_mode && modules.platform) {
        const std::string log_dir =
            engine::default_log_directory(modules.platform->executable_directory());
        if (!engine::install_file_logger(log_dir)) {
            char warning[320];
            std::snprintf(warning, sizeof(warning),
                "Could not open a log file in %s - continuing on stderr only",
                log_dir.c_str());
            engine::log(engine::LogLevel::Warn, engine::LogChannel::General, warning);
        }
#ifdef ENGINE_HAS_WIN32_PLATFORM
        // After install_file_logger, not before: this logs where dumps will go,
        // and that line is worth more in the log than on a stderr nobody kept.
        // Beside the log for the same reason it is installed here - earliest
        // point the executable directory is known. Not under --gates, matching
        // Foundation #6: two gate runs must not push a real crash out.
        const std::string dump_dir = engine::platform::win32::install_crash_dumps(log_dir);
        if (!dump_dir.empty()) {
            engine::log(engine::LogLevel::Info, engine::LogChannel::General,
                std::string("Crash dumps will be written to ") + dump_dir);
        }
#endif
    }

    char start_message[64];
    std::snprintf(start_message, sizeof(start_message),
        gates_mode ? "%s starting (--gates)" : "%s starting", kAppName);
    engine::log(engine::LogLevel::Info, engine::LogChannel::General, start_message);

#ifdef ENGINE_HAS_XAUDIO2
    modules.audio = engine::audio::xaudio2::create_audio();
#endif

#ifdef ENGINE_HAS_PHYSICS_CPU
    modules.physics = engine::physics::cpu::create_physics();
#endif

    // --rhi picks the backend, and asking for one that is not in the build is
    // fatal rather than a fallback. A fallback would draw a perfectly good
    // frame with the other backend, and the entire value of having two is being
    // able to say which one drew the picture.
    if (rhi_name == "vulkan") {
#ifdef ENGINE_HAS_VULKAN
        modules.rhi = engine::rhi::vulkan::create_rhi();
#endif
    } else if (rhi_name == "d3d12") {
#ifdef ENGINE_HAS_D3D12
        modules.rhi = engine::rhi::d3d12::create_rhi();
#endif
    } else {
        char unknown[192];
        std::snprintf(unknown, sizeof(unknown),
            "--rhi %.40s is not a backend. This build has: %s.",
            std::string(rhi_name).c_str(), kBackends);
        engine::log(engine::LogLevel::Fatal, engine::LogChannel::Render, unknown);
        return 1;
    }
    if (!modules.rhi) {
        char missing[192];
        std::snprintf(missing, sizeof(missing),
            "--rhi %.40s was not compiled into this build. This build has: %s.",
            std::string(rhi_name).c_str(), kBackends);
        engine::log(engine::LogLevel::Fatal, engine::LogChannel::Render, missing);
        return 1;
    }

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
            state.game_camera.set_mode(
                engine::gameplay::next_camera_mode(state.game_camera.mode()));
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
                const engine::rhi::FrameRingStats ring = app.device()->frame_ring_stats();
                if (ring.capacity_bytes > 0) {
                    stats.ring_pct = 100.f * static_cast<engine::f32>(ring.peak_bytes)
                        / static_cast<engine::f32>(ring.capacity_bytes);
                }
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
    callbacks.on_extract = [&app, &state](
        engine::renderer::RenderSnapshot& snapshot, engine::Arena& arena) {
        if (!state.forward || !state.forward->pipelines.forward) {
            return;
        }
        auto& world = state.forward->world;
        const engine::u32 width = std::max(snapshot.width, 1u);
        const engine::u32 height = std::max(snapshot.height, 1u);
        const engine::f32 aspect
            = static_cast<engine::f32>(width) / static_cast<engine::f32>(height);
        // From the device, so the projection cannot disagree with the depth
        // clear and compare the backend is using.
        const bool reversed_z = app.device() != nullptr
            && app.device()->depth_convention() == engine::rhi::DepthConvention::Reversed;
        const bool use_game = state.walk_mode && state.player.spawned();
        if (use_game) {
            world.camera.view = state.game_camera.view();
            world.camera.projection = state.game_camera.projection(aspect, reversed_z);
        } else {
            world.camera.view = state.forward->camera.view();
            world.camera.projection = state.forward->camera.projection(aspect, reversed_z);
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
        if (snapshot.aa_mode == engine::renderer::aa::Mode::Taa && snapshot.pipelines.taa
            && snapshot.pipelines.tonemap_aces) {
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
    // Reversed-Z: near maps to 1, far to 0, which is where D32_FLOAT keeps its
    // precision. One value - the projection, the depth clear, every depth
    // compare, the shadow sampler and the shadow bias sign all derive from it,
    // and run_depth_convention_gate fails if any one of them does not.
    config.device.depth_convention = engine::rhi::DepthConvention::Reversed;

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

    // Deliberately not short-circuited: each gate must run and report even
    // when an earlier one fails.
    bool gates_ok = run_parser_fuzz_gate();
    gates_ok = run_mount_gate(*loader) && gates_ok;
    gates_ok = run_mount_containment_gate(*loader) && gates_ok;
    gates_ok = run_build_gate(app.content_layout()) && gates_ok;
    if (!run_gate_registry_gate()) {
        gates_ok = false;
    }
    if (!run_file_log_gate()) {
        gates_ok = false;
    }
    if (!run_minidump_gate()) {
        gates_ok = false;
    }
    if (!run_arena_gate()) {
        gates_ok = false;
    }
    if (!run_frame_timer_gate()) {
        gates_ok = false;
    }
    if (!run_math_guard_gate()) {
        gates_ok = false;
    }
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

// Any GPU backend, not D3D12's. Nothing in this block needs rhi-d3d12: the
// shader compiler lives in `shaders-dxc`, which the root CMakeLists links from
// both backend blocks precisely because DXC emits DXIL *and* SPIR-V, and every
// setup function below takes an IDevice. Written as `#ifdef ENGINE_HAS_D3D12`
// it made a Vulkan-only configure - ENGINE_RHI_D3D12=OFF, ENGINE_RHI_VULKAN=ON
// - build a working sandbox that logged "No GPU backend compiled in" and
// returned 1 without ever using the device it had just created.
#if defined(ENGINE_HAS_D3D12) || defined(ENGINE_HAS_VULKAN)
    const auto cache_dir
        = (std::filesystem::path(app.content_root()) / ".cache" / "shaders").string();
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

    // Two statements, not `a() && b() && gates_ok`: && short-circuits, so a
    // failing graph gate used to skip the swap gate entirely and a red run
    // under-reported. Every other gate in this sequence uses this form.
    if (!run_depth_convention_gate(app.device())) {
        gates_ok = false;
    }
    if (!run_graph_gate()) {
        gates_ok = false;
    }
    if (!run_compute_pass_gate()) {
        gates_ok = false;
    }
    if (!run_swap_gate()) {
        gates_ok = false;
    }
    if (auto* ring_device = app.device()) {
        gates_ok = run_frame_ring_budget_gate(*ring_device) && gates_ok;
    }
    if (!setup_render_graph(app, state)) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
            "Render graph setup failed — running without rendering");
        gates_ok = false;
    }
#else
    // Built with *no* GPU backend: both ENGINE_RHI_D3D12 and ENGINE_RHI_VULKAN
    // off, or any non-Windows configure, where rhi-d3d12, rhi-vulkan and
    // shaders-dxc are all absent. Everything from the shader compiler down
    // needs one, so there is nothing honest left to do. Same shape as the
    // missing-platform branch in main() - say so and stop, rather than report a
    // gate failure for what is a deliberate build configuration.
    (void)loader;
    engine::log(engine::LogLevel::Fatal, engine::LogChannel::Render,
        "No GPU backend compiled in - configure with ENGINE_RHI_D3D12=ON or "
        "ENGINE_RHI_VULKAN=ON on Windows");
    return 1;
#endif

    // After setup_render_graph: this one executes the real compiled graph, so it
    // has to run once the graph exists and the demo owns real resources.
    int exit_code = 0;
    if (gates_mode) {
        char done_message[64];
        std::snprintf(done_message, sizeof(done_message),
            gates_ok ? "%s gates passed" : "%s gates FAILED", kAppName);
        engine::log(gates_ok ? engine::LogLevel::Info : engine::LogLevel::Error,
            engine::LogChannel::General, done_message);
        exit_code = gates_ok ? 0 : 1;
    } else {
        app.run();
    }

    // Single exit so this always runs. `state` owns GPU resources and is
    // destroyed before `app` (declaration order), so the device is still alive
    // when they release - but the last submitted command list may still be
    // executing. Wait for it, or those releases race the GPU.
    if (auto* device = app.device()) {
        device->wait_idle();
    }
    return exit_code;
}

// The one exception boundary in the process.
//
// This engine leans on the throwing standard library - vector::resize driven
// by asset counts, make_unique, std::filesystem - and had no try/catch
// anywhere, so any escaped exception went straight to std::terminate: no log,
// no exit code, nothing for a player to send back. Catching here does not make
// the failure recoverable; it makes it *reportable*, which is the difference
// between a bug report and a shrug.
int main(int argc, char** argv) {
    try {
        return run_app(argc, argv);
    } catch (const std::bad_alloc&) {
        engine::log(engine::LogLevel::Fatal, engine::LogChannel::General,
            "Out of memory - shutting down");
        return 2;
    } catch (const std::exception& e) {
        char message[256];
        std::snprintf(message, sizeof(message), "Unhandled exception: %s", e.what());
        engine::log(engine::LogLevel::Fatal, engine::LogChannel::General, message);
        return 2;
    } catch (...) {
        engine::log(engine::LogLevel::Fatal, engine::LogChannel::General,
            "Unhandled non-standard exception");
        return 2;
    }
}
