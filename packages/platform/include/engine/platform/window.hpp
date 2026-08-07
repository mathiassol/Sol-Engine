#pragma once

#include <engine/core/types.hpp>

#include <string_view>

namespace engine::platform {

struct WindowDesc {
    std::string_view title = "Engine";
    u32 width  = 1280;
    u32 height = 720;
    bool resizable = true;
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
};

} // namespace engine::platform
