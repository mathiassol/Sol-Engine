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
    case WM_SIZE:
        state->width  = static_cast<u32>(LOWORD(lparam));
        state->height = static_cast<u32>(HIWORD(lparam));
        state->events.push_back({WindowEvent::Type::Resize, state->width, state->height});
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
    int len = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
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

HWND Win32Window::hwnd() const { return state_->hwnd; }

std::unique_ptr<Win32Window> create_win32_window(const WindowDesc& desc, WindowState& state_storage) {
    HINSTANCE instance = ::GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc   = engine_wnd_proc;
    wc.hInstance     = instance;
    wc.lpszClassName = L"EngineWindow";
    ::RegisterClassW(&wc);

    DWORD style = desc.resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU);
    RECT rect{0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height)};
    ::AdjustWindowRect(&rect, style, FALSE);

    state_storage.width  = desc.width;
    state_storage.height = desc.height;

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

    return std::make_unique<Win32Window>(&state_storage);
}

} // namespace engine::platform::win32
