#include <engine/engine.hpp>

#include <engine/assets/asset_loader.hpp>
#include <engine/core/log.hpp>
#include <engine/core/profile.hpp>
#include <engine/platform/input.hpp>
#include <engine/platform/platform.hpp>
#include <engine/platform/window.hpp>
#include <engine/rhi/device.hpp>
#include <engine/rhi/rhi.hpp>

namespace engine {

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

    window_ = modules_.platform->create_window(config.window);
    if (!window_) {
        log(LogLevel::Error, LogChannel::Platform, "Engine: failed to create window");
        return false;
    }

    input_ = modules_.platform->create_input(*window_);
    filesystem_ = modules_.platform->create_filesystem();

    frame_arena_.emplace(config_.frame_arena_bytes);

    if (modules_.rhi) {
        auto device_desc = config.device;
        device_desc.window_handle = window_->native_handle();
        device_desc.width  = window_->width();
        device_desc.height = window_->height();
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

    pending_resize_ = false;

    if (pending_resize_w_ == 0 || pending_resize_h_ == 0) {
        return;
    }
    if (pending_resize_w_ == applied_resize_w_ && pending_resize_h_ == applied_resize_h_) {
        return;
    }

    if (!device_->resize(pending_resize_w_, pending_resize_h_)) {
        pending_resize_ = true;
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
            break;
        case platform::WindowEvent::Type::Focus:
        case platform::WindowEvent::Type::Unfocus:
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
    }
}

void Engine::update() {
    ENGINE_PROFILE_SCOPE("update");
    if (callbacks_.on_update) {
        callbacks_.on_update(frame_);
    }
}

void Engine::render() {
    ENGINE_PROFILE_SCOPE("render");
    if (device_) {
        render_graph_.execute(*device_);
    }
}

void Engine::shutdown() {
    if (!running_ && !window_) return;

    device_.reset();
    input_.reset();
    filesystem_.reset();
    window_.reset();
    running_ = false;

    log(LogLevel::Info, LogChannel::General, "Engine shut down");
}

} // namespace engine
