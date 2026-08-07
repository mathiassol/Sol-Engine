#pragma once

#include <engine/core/types.hpp>
#include <engine/platform/window.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <deque>
#include <memory>
#include <string>

namespace engine::platform::win32 {

struct WindowState {
    HWND hwnd = nullptr;
    u32 width = 0;
    u32 height = 0;
    f32 dpi_scale = 1.f;
    bool close_requested = false;
    std::deque<WindowEvent> events;
};

class Win32Window final : public IWindow {
public:
    explicit Win32Window(WindowState* state);

    bool poll_event(WindowEvent& out) override;
    void* native_handle() const override;
    u32 width() const override;
    u32 height() const override;
    f32 dpi_scale() const override;

    HWND hwnd() const;

private:
    WindowState* state_;
};

std::unique_ptr<Win32Window> create_win32_window(const WindowDesc& desc, WindowState& state_storage);

} // namespace engine::platform::win32
