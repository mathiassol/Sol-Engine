#pragma once

#include <engine/assets/asset_loader.hpp>
#include <engine/core/arena.hpp>
#include <engine/core/frame.hpp>
#include <engine/platform/platform.hpp>
#include <engine/platform/window.hpp>
#include <engine/renderer/render_graph.hpp>
#include <engine/rhi/rhi.hpp>

#include <functional>
#include <memory>
#include <optional>

namespace engine::platform {
class IWindow;
class IInput;
class IFileSystem;
}

namespace engine::rhi {
class IDevice;
}

namespace engine {

struct EngineCallbacks {
    std::function<void(const FrameContext&)> on_fixed_update;
    std::function<void(const FrameContext&)> on_update;
};

struct EngineConfig {
    platform::WindowDesc window{};
    rhi::DeviceDesc      device{};
    FrameTimerConfig     frame{};
    usize                frame_arena_bytes = 4 * 1024 * 1024;
};

struct EngineModules {
    std::unique_ptr<platform::IPlatform>  platform;
    std::unique_ptr<rhi::IRHI>            rhi;
    std::unique_ptr<assets::IAssetLoader> asset_loader;
};

class Engine {
public:
    explicit Engine(EngineModules modules);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool init(const EngineConfig& config);
    void set_callbacks(EngineCallbacks callbacks);
    void run();
    void shutdown();

    const FrameContext& frame_context() const { return frame_; }
    Arena& frame_arena() { return *frame_arena_; }
    renderer::RenderGraph& render_graph() { return render_graph_; }
    rhi::IDevice* device() { return device_.get(); }
    platform::IFileSystem* filesystem() { return filesystem_.get(); }
    platform::IInput* input() { return input_.get(); }

private:
    void poll_events();
    void apply_pending_resize();
    void fixed_update();
    void update();
    void render();

    EngineModules modules_;
    EngineCallbacks callbacks_;
    EngineConfig config_;

    std::unique_ptr<platform::IWindow>     window_;
    std::unique_ptr<platform::IInput>      input_;
    std::unique_ptr<platform::IFileSystem> filesystem_;
    std::unique_ptr<rhi::IDevice>          device_;

    FrameTimer frame_timer_;
    FrameContext frame_;
    std::optional<Arena> frame_arena_;

    renderer::RenderGraph render_graph_;
    bool running_ = false;

    u32 pending_resize_w_ = 0;
    u32 pending_resize_h_ = 0;
    bool pending_resize_ = false;
    u32 applied_resize_w_ = 0;
    u32 applied_resize_h_ = 0;
};

} // namespace engine
