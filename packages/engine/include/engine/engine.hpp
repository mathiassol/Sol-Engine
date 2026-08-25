#pragma once

#include <engine/assets/asset_loader.hpp>
#include <engine/audio/audio.hpp>
#include <engine/physics/physics.hpp>
#include <engine/core/arena.hpp>
#include <engine/core/frame.hpp>
#include <engine/platform/platform.hpp>
#include <engine/platform/window.hpp>
#include <engine/renderer/render_graph.hpp>
#include <engine/renderer/render_snapshot.hpp>
#include <engine/rhi/rhi.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace engine::platform {
class IWindow;
class IInput;
class IFileSystem;
}

namespace engine::rhi {
class IDevice;
}

namespace engine::audio {
class IAudio;
}

namespace engine::physics {
class IPhysics;
}

namespace engine {

struct EngineCallbacks {
    std::function<void(const FrameContext&)> on_fixed_update;
    std::function<void(const FrameContext&)> on_update;
    std::function<void(renderer::RenderSnapshot&, Arena&)> on_extract;
};

// Repo: packages/sandbox/content (developer tree).
// Install: <exe_dir>/content + <exe_dir>/debug (player layout next to game.exe).
enum class ContentLayout : u8 { Unknown, Repo, Install };

struct ContentMountPaths {
    ContentLayout layout = ContentLayout::Unknown;
    std::string content;
    std::string shaders;
    std::string debug;
};

const char* content_layout_name(ContentLayout layout);
ContentMountPaths resolve_content_mounts(std::string_view content_root);

struct EngineConfig {
    platform::WindowDesc window{};
    rhi::DeviceDesc      device{};
    FrameTimerConfig     frame{};
    usize                frame_arena_bytes = 4 * 1024 * 1024;
    std::string          content_root;
};

struct EngineModules {
    std::unique_ptr<platform::IPlatform>  platform;
    std::unique_ptr<rhi::IRHI>            rhi;
    std::unique_ptr<assets::IAssetLoader> asset_loader;
    std::unique_ptr<audio::IAudio>        audio;
    std::unique_ptr<physics::IPhysics>    physics;
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
    platform::IWindow* window() { return window_.get(); }
    platform::IFileSystem* filesystem() { return filesystem_.get(); }
    platform::IInput* input() { return input_.get(); }
    audio::IAudio* audio() { return modules_.audio.get(); }
    physics::IPhysics* physics() { return modules_.physics.get(); }
    const std::string& content_root() const { return content_root_; }
    ContentLayout content_layout() const { return content_layout_; }
    std::string executable_directory() const;
    std::string executable_file_version() const;
    bool executable_has_icon() const;

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
    std::string                            content_root_;
    ContentLayout                          content_layout_ = ContentLayout::Unknown;

    FrameTimer frame_timer_;
    FrameContext frame_;
    std::optional<Arena> frame_arena_;

    // GPU transients live here. shutdown() clears this before device_.reset().
    renderer::RenderGraph render_graph_;
    bool running_ = false;

    u32 pending_resize_w_ = 0;
    u32 pending_resize_h_ = 0;
    bool pending_resize_ = false;
    bool resize_events_this_frame_ = false;
    u32 applied_resize_w_ = 0;
    u32 applied_resize_h_ = 0;
};

} // namespace engine
