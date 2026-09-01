#include <engine/platform/win32/window_win32.hpp>

#include <string>

namespace engine::platform::win32 {

namespace {

LRESULT CALLBACK engine_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<WindowState*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<WindowState*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->hwnd = hwnd;
        return TRUE;
    }

    if (!state) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    switch (msg) {
    case WM_CLOSE:
        state->close_requested = true;
        state->events.push_back({WindowEvent::Type::Close});
        return 0;
    case WM_ENTERSIZEMOVE:
        state->sizing = true;
        return 0;
    case WM_EXITSIZEMOVE:
        state->sizing = false;
        if (state->width > 0 && state->height > 0) {
            state->events.push_back({WindowEvent::Type::Resize, state->width, state->height});
        }
        return 0;
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED) {
            return 0;
        }
        state->width  = static_cast<u32>(LOWORD(lparam));
        state->height = static_cast<u32>(HIWORD(lparam));
        if (state->width == 0 || state->height == 0) {
            return 0;
        }
        if (!state->sizing) {
            state->events.push_back({WindowEvent::Type::Resize, state->width, state->height});
        }
        return 0;
    case WM_SETFOCUS:
        state->events.push_back({WindowEvent::Type::Focus});
        return 0;
    case WM_KILLFOCUS:
        state->events.push_back({WindowEvent::Type::Unfocus});
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) return {};
    int len = ::MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), len);
    return wide;
}

} // namespace

Win32Window::Win32Window(WindowState* state) : state_(state) {}

bool Win32Window::poll_event(WindowEvent& out) {
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            out = {WindowEvent::Type::Close};
            return true;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    if (!state_->events.empty()) {
        out = state_->events.front();
        state_->events.pop_front();
        return true;
    }

    return false;
}

void* Win32Window::native_handle() const { return state_->hwnd; }
u32 Win32Window::width() const { return state_->width; }
u32 Win32Window::height() const { return state_->height; }
f32 Win32Window::dpi_scale() const { return state_->dpi_scale; }
WindowMode Win32Window::mode() const { return state_->mode; }
bool Win32Window::vsync() const { return state_->vsync; }
void Win32Window::set_vsync(bool enabled) { state_->vsync = enabled; }
std::string_view Win32Window::title() const { return state_->title; }

HWND Win32Window::hwnd() const { return state_->hwnd; }

void Win32Window::refresh_client_size() {
    RECT client{};
    ::GetClientRect(state_->hwnd, &client);
    const u32 width = static_cast<u32>(client.right - client.left);
    const u32 height = static_cast<u32>(client.bottom - client.top);
    if (width > 0 && height > 0) {
        state_->width = width;
        state_->height = height;
    }
    const UINT dpi = ::GetDpiForWindow(state_->hwnd);
    state_->dpi_scale = static_cast<f32>(dpi) / 96.f;
}

void Win32Window::remember_windowed() {
    state_->windowed_style = static_cast<DWORD>(::GetWindowLongW(state_->hwnd, GWL_STYLE));
    state_->windowed_placement.length = sizeof(WINDOWPLACEMENT);
    ::GetWindowPlacement(state_->hwnd, &state_->windowed_placement);
}

void Win32Window::apply_windowed() {
    ::SetWindowLongW(state_->hwnd, GWL_STYLE,
        static_cast<LONG>(state_->windowed_style | WS_VISIBLE));
    if (state_->windowed_placement.length == sizeof(WINDOWPLACEMENT)) {
        ::SetWindowPlacement(state_->hwnd, &state_->windowed_placement);
    }
    ::SetWindowPos(state_->hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    refresh_client_size();
}

void Win32Window::apply_cover_monitor() {
    HMONITOR monitor = ::MonitorFromWindow(state_->hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(monitor, &info)) {
        return;
    }
    ::SetWindowLongW(state_->hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    ::SetWindowPos(state_->hwnd, HWND_TOP,
        info.rcMonitor.left, info.rcMonitor.top,
        info.rcMonitor.right - info.rcMonitor.left,
        info.rcMonitor.bottom - info.rcMonitor.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    refresh_client_size();
}

bool Win32Window::set_mode(WindowMode mode) {
    if (!state_ || !state_->hwnd) {
        return false;
    }
    if (state_->mode == mode) {
        return true;
    }

    const bool covering = mode == WindowMode::Borderless || mode == WindowMode::Fullscreen;
    const bool was_covering = state_->mode == WindowMode::Borderless
        || state_->mode == WindowMode::Fullscreen;

    if (covering && !was_covering) {
        remember_windowed();
        apply_cover_monitor();
    } else if (!covering && was_covering) {
        apply_windowed();
    } else if (covering) {
        apply_cover_monitor();
    }

    state_->mode = mode;
    if (state_->width > 0 && state_->height > 0) {
        state_->events.push_back({WindowEvent::Type::Resize, state_->width, state_->height});
    }
    return true;
}

std::unique_ptr<Win32Window> create_win32_window(
    const WindowDesc& desc, WindowState& state_storage) {
    HINSTANCE instance = ::GetModuleHandleW(nullptr);

    // Runtime .rc uses IDI_APP_ICON 101 (packages/game/resources/resource.h).
    constexpr WORD kAppIconId = 101;
    HICON icon = ::LoadIconW(instance, MAKEINTRESOURCEW(kAppIconId));
    if (!icon) {
        icon = ::LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    }

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = engine_wnd_proc;
    wc.hInstance     = instance;
    wc.hIcon         = icon;
    wc.hCursor       = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIconSm       = icon;
    wc.lpszClassName = L"EngineWindow";
    ::RegisterClassExW(&wc);

    DWORD style = desc.resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU);
    RECT rect{0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height)};
    ::AdjustWindowRect(&rect, style, FALSE);

    state_storage.width  = desc.width;
    state_storage.height = desc.height;
    state_storage.title  = std::string(desc.title);

    std::wstring title = utf8_to_wide(desc.title);
    HWND hwnd = ::CreateWindowExW(
        0, wc.lpszClassName,
        title.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, &state_storage);

    if (!hwnd) return nullptr;

    UINT dpi = ::GetDpiForWindow(hwnd);
    state_storage.dpi_scale = static_cast<f32>(dpi) / 96.f;
    state_storage.hwnd = hwnd;

    ::ShowWindow(hwnd, SW_SHOW);
    ::UpdateWindow(hwnd);

    state_storage.vsync = desc.vsync;
    state_storage.mode = WindowMode::Windowed;
    state_storage.windowed_style = style;
    state_storage.windowed_placement.length = sizeof(WINDOWPLACEMENT);
    ::GetWindowPlacement(hwnd, &state_storage.windowed_placement);

    auto window = std::make_unique<Win32Window>(&state_storage);
    if (desc.mode != WindowMode::Windowed) {
        window->set_mode(desc.mode);
    }
    return window;
}

void destroy_win32_window(WindowState& state) {
    if (!state.hwnd) {
        return;
    }
    HWND hwnd = state.hwnd;
    state.hwnd = nullptr;

    // Clear the back-pointer *before* destroying. DestroyWindow dispatches
    // WM_DESTROY / WM_NCDESTROY synchronously, and the message pump uses
    // PeekMessage(hWnd = nullptr), which drains every window on the thread -
    // so a wndproc that still found `state` here would push onto a deque that
    // is about to be freed. With it null the wndproc falls through to
    // DefWindowProc.
    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    ::DestroyWindow(hwnd);
}

} // namespace engine::platform::win32
