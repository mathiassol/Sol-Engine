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
    bool sizing = false;
    bool vsync = true;
    WindowMode mode = WindowMode::Windowed;
    DWORD windowed_style = 0;
    WINDOWPLACEMENT windowed_placement{};
    std::string title;
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
    WindowMode mode() const override;
    bool set_mode(WindowMode mode) override;
    bool vsync() const override;
    void set_vsync(bool enabled) override;
    std::string_view title() const override;

    HWND hwnd() const;

private:
    void remember_windowed();
    void apply_windowed();
    void apply_cover_monitor();
    void refresh_client_size();

    WindowState* state_;
};

std::unique_ptr<Win32Window> create_win32_window(const WindowDesc& desc, WindowState& state_storage);

// Destroys the OS window and detaches its wndproc from `state`. Must run while
// `state` is still alive - the window outlives the Win32Window wrapper, which
// only borrows the state.
void destroy_win32_window(WindowState& state);

} // namespace engine::platform::win32
