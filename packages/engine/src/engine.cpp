#include <engine/engine.hpp>

#include <engine/assets/asset_loader.hpp>
#include <engine/audio/audio.hpp>
#include <engine/cvar_file.hpp>
#include <engine/physics/physics.hpp>
#include <engine/core/cvar.hpp>
#include <engine/core/log.hpp>
#include <engine/core/profile.hpp>
#include <engine/platform/input.hpp>
#include <engine/platform/platform.hpp>
#include <engine/platform/window.hpp>
#include <engine/rhi/device.hpp>
#include <engine/rhi/rhi.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace engine {

namespace {

// EngineConfig still holds the code-level defaults. A cvar only overrides a
// field when something actually set it, which is why every read below checks
// source() != CvarSource::Default.
Cvar cv_window_width{"window.width", 1280, "Window client width in pixels"};
Cvar cv_window_height{"window.height", 720, "Window client height in pixels"};
Cvar cv_window_mode{"window.mode", "windowed", "windowed | borderless | fullscreen"};
Cvar cv_vsync{"r.vsync", true, "Present with vsync"};

u32 positive_dimension_cvar(const Cvar& cvar, u32 fallback) {
    const i32 value = cvar.as_int();
    if (value <= 0) {
        log(LogLevel::Warn, LogChannel::Platform,
            std::string("Cvar '") + cvar.name() + "' must be positive; keeping the default");
        return fallback;
    }
    return static_cast<u32>(value);
}

bool looks_like_repo_root(const std::filesystem::path& path) {
    return std::filesystem::exists(path / "CMakeLists.txt")
        && std::filesystem::exists(path / "packages" / "sandbox" / "content" / "test.txt");
}

bool looks_like_install_root(const std::filesystem::path& path) {
    return std::filesystem::exists(path / "content" / "test.txt")
        && std::filesystem::exists(path / "debug");
}

std::string canonicalize(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return path.lexically_normal().string();
    }
    return canonical.string();
}

std::string walk_for_content_root(std::filesystem::path start) {
    for (int i = 0; i < 8 && !start.empty(); ++i) {
        if (looks_like_repo_root(start)) {
            return canonicalize(start);
        }
        const auto parent = start.parent_path();
        if (parent == start) {
            break;
        }
        start = parent;
    }
    return {};
}

std::string discover_content_root(platform::IPlatform& platform, std::string_view override_root) {
    if (!override_root.empty()) {
        return canonicalize(std::filesystem::path(override_root));
    }

    const std::filesystem::path exe_dir{platform.executable_directory()};
    if (looks_like_install_root(exe_dir)) {
        return canonicalize(exe_dir);
    }

    std::string found = walk_for_content_root(std::filesystem::current_path());
    if (!found.empty()) {
        return found;
    }

    found = walk_for_content_root(exe_dir);
    if (!found.empty()) {
        return found;
    }

    return canonicalize(std::filesystem::current_path());
}

} // namespace

std::string Engine::executable_directory() const {
    if (!modules_.platform) {
        return {};
    }
    return modules_.platform->executable_directory();
}

std::string Engine::executable_file_version() const {
    if (!modules_.platform) {
        return {};
    }
    return modules_.platform->executable_file_version();
}

bool Engine::executable_has_icon() const {
    return modules_.platform && modules_.platform->executable_has_icon();
}

const char* content_layout_name(ContentLayout layout) {
    switch (layout) {
    case ContentLayout::Repo:
        return "repo";
    case ContentLayout::Install:
        return "install";
    default:
        return "unknown";
    }
}

ContentMountPaths resolve_content_mounts(std::string_view content_root) {
    ContentMountPaths out{};
    const std::filesystem::path root{content_root};
    if (looks_like_install_root(root)) {
        out.layout = ContentLayout::Install;
        out.content = canonicalize(root / "content");
        const auto nested_shaders = root / "content" / "shaders";
        out.shaders = canonicalize(std::filesystem::exists(nested_shaders)
            ? nested_shaders
            : (root / "shaders"));
        out.debug = canonicalize(root / "debug");
        return out;
    }
    if (looks_like_repo_root(root)) {
        out.layout = ContentLayout::Repo;
        out.content = canonicalize(root / "packages" / "sandbox" / "content");
        out.shaders = canonicalize(root / "packages" / "sandbox" / "content" / "shaders");
        out.debug = canonicalize(root / "packages" / "debug-draw" / "content");
        return out;
    }
    return out;
}

Engine::Engine(EngineModules modules)
    : modules_(std::move(modules)) {}

Engine::~Engine() {
    shutdown();
}

bool Engine::init(const EngineConfig& config) {
    config_ = config;
    frame_timer_ = FrameTimer(config_.frame);

    if (!modules_.platform) {
        log(LogLevel::Error, LogChannel::Platform, "Engine: no platform module");
        return false;
    }

    // Filesystem and content root come first: neither needs a window, and
    // config.cfg has to be read before the window is sized.
    filesystem_ = modules_.platform->create_filesystem();
    content_root_ = discover_content_root(*modules_.platform, config_.content_root);
    content_layout_ = resolve_content_mounts(content_root_).layout;
    log(LogLevel::Info, LogChannel::General,
        std::string("Content root: ") + content_root_ + " ("
            + content_layout_name(content_layout_) + ")");

    CvarApplyStats file_stats{};
    if (filesystem_) {
        const std::string cvar_path =
            (std::filesystem::path(content_root_) / kCvarFileName).string();
        file_stats = apply_cvar_file(*filesystem_, cvar_path);
    }

    auto window_desc = config.window;
    if (cv_window_width.source() != CvarSource::Default) {
        window_desc.width = positive_dimension_cvar(cv_window_width, window_desc.width);
    }
    if (cv_window_height.source() != CvarSource::Default) {
        window_desc.height = positive_dimension_cvar(cv_window_height, window_desc.height);
    }
    if (cv_window_mode.source() != CvarSource::Default) {
        platform::WindowMode mode = window_desc.mode;
        if (platform::parse_window_mode(cv_window_mode.as_string(), mode)) {
            window_desc.mode = mode;
        } else {
            log(LogLevel::Warn, LogChannel::Platform,
                std::string("Cvar 'window.mode' expects ") + cv_window_mode.help());
        }
    }
    if (cv_vsync.source() != CvarSource::Default) {
        window_desc.vsync = cv_vsync.as_bool();
    }

    usize cli_count = 0;
    for (usize i = 0; i < cvar_count(); ++i) {
        const Cvar* cvar = cvar_at(i);
        if (cvar && cvar->source() == CvarSource::CommandLine) {
            ++cli_count;
        }
    }
    char cvar_message[192];
    std::snprintf(cvar_message, sizeof(cvar_message),
        "Cvars: file=%llu cli=%llu window=%ux%u %s vsync=%s",
        static_cast<unsigned long long>(file_stats.applied),
        static_cast<unsigned long long>(cli_count),
        window_desc.width, window_desc.height,
        platform::window_mode_name(window_desc.mode),
        window_desc.vsync ? "on" : "off");
    log(LogLevel::Info, LogChannel::General, cvar_message);

    window_ = modules_.platform->create_window(window_desc);
    if (!window_) {
        log(LogLevel::Error, LogChannel::Platform, "Engine: failed to create window");
        return false;
    }

    input_ = modules_.platform->create_input(*window_);

    frame_arena_.emplace(config_.frame_arena_bytes);

    if (modules_.rhi) {
        auto device_desc = config.device;
        device_desc.window_handle = window_->native_handle();
        device_desc.width  = window_->width();
        device_desc.height = window_->height();
        device_desc.present_interval = window_->vsync() ? 1u : 0u;
        device_ = modules_.rhi->create_device(device_desc);
        if (!device_) {
            log(LogLevel::Error, LogChannel::Render, "Engine: failed to create GPU device");
            return false;
        }

        applied_resize_w_ = window_->width();
        applied_resize_h_ = window_->height();
    }

    log(LogLevel::Info, LogChannel::General, "Engine initialized");
    running_ = true;
    return true;
}

void Engine::set_callbacks(EngineCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
}

void Engine::run() {
    while (running_ && window_) {
        profiler_begin_frame();
        ENGINE_PROFILE_SCOPE("frame");

        frame_ = frame_timer_.begin_frame();
        frame_arena_->reset();

        poll_events();

        if (input_) {
            input_->update();
            if (input_->key_pressed(platform::Key::Escape)) {
                running_ = false;
            }
        }

        fixed_update();
        update();
        apply_pending_resize();
        render();
    }
}

void Engine::apply_pending_resize() {
    if (!pending_resize_ || !device_) {
        return;
    }

    if (resize_events_this_frame_) {
        resize_events_this_frame_ = false;
        return;
    }

    pending_resize_ = false;

    if (pending_resize_w_ == 0 || pending_resize_h_ == 0) {
        return;
    }
    if (pending_resize_w_ == applied_resize_w_ && pending_resize_h_ == applied_resize_h_) {
        return;
    }

    if (!device_->resize(pending_resize_w_, pending_resize_h_)) {
        applied_resize_w_ = device_->width();
        applied_resize_h_ = device_->height();
        return;
    }
    applied_resize_w_ = pending_resize_w_;
    applied_resize_h_ = pending_resize_h_;
}

void Engine::poll_events() {
    ENGINE_PROFILE_SCOPE("poll_events");

    platform::WindowEvent event{};
    while (window_->poll_event(event)) {
        switch (event.type) {
        case platform::WindowEvent::Type::Close:
            running_ = false;
            break;
        case platform::WindowEvent::Type::Resize:
            pending_resize_w_ = event.width;
            pending_resize_h_ = event.height;
            pending_resize_ = true;
            resize_events_this_frame_ = true;
            break;
        case platform::WindowEvent::Type::Focus:
            if (input_) {
                input_->set_focused(true);
            }
            break;
        case platform::WindowEvent::Type::Unfocus:
            if (input_) {
                input_->set_focused(false);
            }
            break;
        }
    }
}

void Engine::fixed_update() {
    ENGINE_PROFILE_SCOPE("fixed_update");
    while (frame_timer_.consume_fixed_step()) {
        if (callbacks_.on_fixed_update) {
            callbacks_.on_fixed_update(frame_);
        }
        if (modules_.physics) {
            modules_.physics->step(frame_.fixed_delta);
        }
    }
}

void Engine::update() {
    ENGINE_PROFILE_SCOPE("update");
    if (callbacks_.on_update) {
        callbacks_.on_update(frame_);
    }
    if (modules_.audio) {
        modules_.audio->tick();
    }
}

void Engine::render() {
    ENGINE_PROFILE_SCOPE("render");
    if (!device_) {
        return;
    }
    device_->set_present_interval(window_ && window_->vsync() ? 1u : 0u);
    if (device_->swapchain_color().width() == 0 || device_->swapchain_depth().width() == 0) {
        return;
    }

    renderer::RenderSnapshot snapshot{};
    snapshot.width = device_->width();
    snapshot.height = device_->height();
    {
        ENGINE_PROFILE_SCOPE("extract");
        if (callbacks_.on_extract) {
            callbacks_.on_extract(snapshot, *frame_arena_);
        }
    }
    {
        ENGINE_PROFILE_SCOPE("execute");
        render_graph_.execute(*device_, snapshot);
    }
}

void Engine::shutdown() {
    if (!running_ && !window_) return;

    if (device_) {
        device_->wait_idle();
    }
    render_graph_.clear();
    device_.reset();
    modules_.audio.reset();
    modules_.physics.reset();
    input_.reset();
    filesystem_.reset();
    window_.reset();
    running_ = false;

    log(LogLevel::Info, LogChannel::General, "Engine shut down");
}

} // namespace engine
