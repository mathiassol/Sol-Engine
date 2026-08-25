#pragma once

#include <engine/core/types.hpp>

#include <string>
#include <string_view>

namespace engine::platform {

enum class WindowMode : u8 {
    Windowed,
    Borderless,  // cover the current monitor, no exclusive DXGI
    Fullscreen,  // same HWND path as Borderless; exclusive DXGI is not this item
};

inline const char* window_mode_name(WindowMode mode) {
    switch (mode) {
    case WindowMode::Borderless:
        return "borderless";
    case WindowMode::Fullscreen:
        return "fullscreen";
    default:
        return "windowed";
    }
}

struct WindowDesc {
    std::string_view title = "Engine";
    u32 width  = 1280;
    u32 height = 720;
    bool resizable = true;
    WindowMode mode = WindowMode::Windowed;
    bool vsync = true;
};

struct WindowEvent {
    enum class Type : u8 {
        Close,
        Resize,
        Focus,
        Unfocus,
    };

    Type type;
    u32 width  = 0;
    u32 height = 0;
};

class IWindow {
public:
    virtual ~IWindow() = default;

    virtual bool poll_event(WindowEvent& out) = 0;
    virtual void* native_handle() const = 0;
    virtual u32 width() const = 0;
    virtual u32 height() const = 0;
    virtual f32 dpi_scale() const = 0;
    virtual WindowMode mode() const = 0;
    virtual bool set_mode(WindowMode mode) = 0;
    virtual bool vsync() const = 0;
    virtual void set_vsync(bool enabled) = 0;
    virtual std::string_view title() const = 0;
};

} // namespace engine::platform
