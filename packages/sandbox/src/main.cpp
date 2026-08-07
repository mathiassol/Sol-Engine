#include <engine/engine.hpp>
#include <engine/core/log.hpp>
#include <engine/assets/filesystem/asset_loader_filesystem.hpp>
#include <engine/assets/gpu/mesh_upload.hpp>
#include <engine/assets/obj/mesh_loader_obj.hpp>
#include <engine/math/constants.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/vec3.hpp>
#include <engine/platform/input.hpp>
#include <engine/rhi/commands.hpp>
#include <engine/rhi/device.hpp>
#include <engine/rhi/resources.hpp>
#include <engine/debug/frame_stats.hpp>
#include <engine/debug/stats_overlay.hpp>
#include <engine/shaders/dxc/shader_compiler_dxc.hpp>
#include <engine/shaders/dxc/shader_hot_reload_dxc.hpp>
#include <engine/shaders/shader_hot_reload.hpp>

#ifdef ENGINE_HAS_WIN32_PLATFORM
#include <engine/platform/win32/platform_win32.hpp>
#endif

#ifdef ENGINE_HAS_D3D12
#include <engine/rhi/d3d12/rhi_d3d12.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace {

constexpr const char* kForwardShader = "packages/sandbox/content/shaders/forward.hlsl";
constexpr const char* kCubeMesh = "packages/sandbox/content/meshes/cube.obj";
constexpr const char* kOverlayShader = "packages/debug-draw/content/shaders/overlay.hlsl";

struct FrameConstants {
    engine::math::Mat4 view_proj{};
};

struct FlyCamera {
    engine::math::Vec3 position{0.f, 0.5f, -4.f};
    engine::f32 yaw   = 0.f;
    engine::f32 pitch = 0.f;

    void update(const engine::platform::InputState& input, engine::f32 dt) {
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
        if (input.keys_down[static_cast<engine::usize>(Key::W)]) position = position + forward * speed;
        if (input.keys_down[static_cast<engine::usize>(Key::S)]) position = position - forward * speed;
        if (input.keys_down[static_cast<engine::usize>(Key::A)]) position = position - right * speed;
        if (input.keys_down[static_cast<engine::usize>(Key::D)]) position = position + right * speed;
        if (input.keys_down[static_cast<engine::usize>(Key::E)]) position = position + up * speed;
        if (input.keys_down[static_cast<engine::usize>(Key::Q)]) position = position - up * speed;
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
    engine::assets::gpu::GpuMesh mesh;
    std::unique_ptr<engine::rhi::IBuffer> constant_buffer;
    std::unique_ptr<engine::shaders::IShaderHotReloader> shader_watcher;
    engine::shaders::WatchedShaderPair shader_sources{};
    FlyCamera camera;
    engine::rhi::IDevice* device = nullptr;
};

void poll_shader_reload(ForwardDemo& demo) {
    if (!demo.shader_watcher || !demo.device) return;

    engine::shaders::ShaderBytecode vs_bytecode;
    engine::shaders::ShaderBytecode ps_bytecode;
    std::string error;
    const auto status = demo.shader_watcher->poll(vs_bytecode, ps_bytecode, error);
    if (status != engine::shaders::ShaderReloadStatus::Reloaded) {
        return;
    }

    demo.device->wait_idle();
    auto pipeline = demo.device->create_forward_pipeline(vs_bytecode.data, ps_bytecode.data);
    if (!pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Shader hot-reload pipeline creation failed");
        return;
    }

    demo.pipeline = std::move(pipeline);
    engine::log(engine::LogLevel::Info, engine::LogChannel::Render, "Shader hot-reload applied");
}

ForwardDemo* g_forward = nullptr;

engine::debug::FrameStatsTracker g_frame_stats;
engine::debug::StatsOverlay g_stats_overlay;

bool setup_stats_overlay(engine::Engine& app) {
    auto* device = app.device();
    if (!device) return false;

    auto compiler = engine::shaders::dxc::create_compiler();
    if (!g_stats_overlay.init(*device, *compiler, kOverlayShader)) {
        return false;
    }

    app.render_graph().add_pass({"stats_overlay", [](engine::rhi::ICommandList& cmd) {
        if (!g_stats_overlay.visible() || !g_forward) return;

        engine::rhi::RenderPassInfo pass{};
        pass.clear_color_target = false;
        pass.clear_depth = false;
        cmd.begin_render_pass(pass);
        g_stats_overlay.draw(cmd, g_forward->device->width(), g_forward->device->height());
        cmd.end_render_pass();
    }});

    engine::log(engine::LogLevel::Info, engine::LogChannel::Render,
        "Stats overlay ready (F3 to toggle)");
    return true;
}

bool setup_forward_demo(engine::Engine& app) {
    auto* device = app.device();
    if (!device) return false;

    auto mesh_loader = engine::assets::obj::create_mesh_loader();
    engine::assets::MeshData mesh_data;
    if (!mesh_loader->load(kCubeMesh, mesh_data)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets, "Failed to load cube mesh");
        return false;
    }

    auto compiler = engine::shaders::dxc::create_compiler();
    engine::shaders::ShaderCompileDesc vs_desc{};
    vs_desc.file_path = kForwardShader;
    vs_desc.entry_point = "vs_main";
    vs_desc.target_profile = "vs_5_0";

    engine::shaders::ShaderCompileDesc ps_desc = vs_desc;
    ps_desc.entry_point = "ps_main";
    ps_desc.target_profile = "ps_5_0";

    engine::shaders::ShaderBytecode vs_bytecode;
    engine::shaders::ShaderBytecode ps_bytecode;
    std::string error;
    if (!compiler->compile(vs_desc, vs_bytecode, error)) return false;
    if (!compiler->compile(ps_desc, ps_bytecode, error)) return false;

    auto demo = std::make_unique<ForwardDemo>();
    demo->device = device;
    demo->pipeline = device->create_forward_pipeline(vs_bytecode.data, ps_bytecode.data);
    if (!demo->pipeline) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Forward pipeline creation failed");
        return false;
    }

    demo->mesh = engine::assets::gpu::upload_mesh(*device, mesh_data);

    engine::rhi::BufferDesc cb_desc{};
    cb_desc.size  = 256;
    cb_desc.usage = engine::rhi::BufferUsage::Uniform;
    demo->constant_buffer = device->create_buffer(cb_desc);
    if (!demo->constant_buffer) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, "Constant buffer creation failed");
        return false;
    }

    demo->shader_sources.vertex = vs_desc;
    demo->shader_sources.pixel  = ps_desc;
    demo->shader_watcher = engine::shaders::dxc::create_hot_reloader();
    demo->shader_watcher->begin_watch(demo->shader_sources);

    app.render_graph().add_pass({"forward", [](engine::rhi::ICommandList& cmd) {
        if (!g_forward) return;

        const engine::f32 aspect = static_cast<engine::f32>(g_forward->device->width())
            / static_cast<engine::f32>(std::max(g_forward->device->height(), 1u));
        FrameConstants constants{};
        constants.view_proj = g_forward->camera.projection(aspect) * g_forward->camera.view();
        g_forward->device->write_buffer(*g_forward->constant_buffer, 0, &constants, sizeof(constants));

        engine::rhi::RenderPassInfo pass{};
        pass.clear_color = {0.08f, 0.08f, 0.12f, 1.f};
        cmd.begin_render_pass(pass);
        cmd.set_pipeline(*g_forward->pipeline);
        cmd.set_constant_buffer(0, *g_forward->constant_buffer);
        cmd.set_vertex_buffer(0, *g_forward->mesh.vertex_buffer, sizeof(engine::assets::VertexPN));
        cmd.set_index_buffer(*g_forward->mesh.index_buffer);
        cmd.draw_indexed(g_forward->mesh.index_count);
        cmd.end_render_pass();
    }});

    g_forward = demo.release();
    engine::log(engine::LogLevel::Info, engine::LogChannel::Render,
        "Forward pass ready (WASD + right-mouse look, shader hot-reload enabled)");
    return true;
}

} // namespace

int main() {
    engine::log(engine::LogLevel::Info, engine::LogChannel::General, "Sandbox starting");

    engine::EngineModules modules{};

#ifdef ENGINE_HAS_WIN32_PLATFORM
    modules.platform = engine::platform::win32::create_platform();
#else
    engine::log(engine::LogLevel::Fatal, engine::LogChannel::Platform, "No platform backend");
    return 1;
#endif

#ifdef ENGINE_HAS_D3D12
    modules.rhi = engine::rhi::d3d12::create_rhi();
#endif

    engine::Engine app(std::move(modules));

    engine::EngineCallbacks callbacks{};
    callbacks.on_update = [&app](const engine::FrameContext& frame) {
        g_frame_stats.update(frame.delta);

        if (app.input() && app.input()->key_pressed(engine::platform::Key::F3)) {
            g_stats_overlay.set_visible(!g_stats_overlay.visible());
        }

        if (g_stats_overlay.visible()) {
            g_stats_overlay.update(g_frame_stats.stats());
        }

        if (g_forward) {
            poll_shader_reload(*g_forward);
            if (app.input()) {
                g_forward->camera.update(app.input()->state(), frame.delta);
            }
        }
    };
    app.set_callbacks(callbacks);

    engine::EngineConfig config{};
    config.window.title = "Engine Sandbox";
    config.window.width  = 1280;
    config.window.height = 720;
    config.device.preferred_api = engine::rhi::GraphicsAPI::D3D12;

    if (!app.init(config)) {
        return 1;
    }

    if (app.filesystem()) {
        auto loader = engine::assets::filesystem::create_asset_loader(*app.filesystem());
        std::vector<engine::u8> bytes;
        if (loader->load_bytes("packages/sandbox/content/test.txt", bytes)) {
            engine::log(engine::LogLevel::Info, engine::LogChannel::Assets,
                "Loaded content file via assets-filesystem");
        }
    }

    if (!setup_forward_demo(app)) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
            "Forward pass setup failed — running without rendering");
    }

    if (!setup_stats_overlay(app)) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
            "Stats overlay setup failed");
    }

    app.run();

    delete g_forward;
    g_forward = nullptr;
    return 0;
}
