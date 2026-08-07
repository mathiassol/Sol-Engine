#include <engine/platform/win32/platform_win32.hpp>
#include <engine/platform/win32/window_win32.hpp>

#include <engine/core/log.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace engine::platform::win32 {

namespace {

Key vk_to_key(int vk) {
    switch (vk) {
    case VK_ESCAPE:   return Key::Escape;
    case VK_SPACE:    return Key::Space;
    case VK_RETURN:   return Key::Enter;
    case VK_TAB:      return Key::Tab;
    case 'W': case 'w': return Key::W;
    case 'A': case 'a': return Key::A;
    case 'S': case 's': return Key::S;
    case 'D': case 'd': return Key::D;
    case 'Q': case 'q': return Key::Q;
    case 'E': case 'e': return Key::E;
    case VK_F3: return Key::F3;
    case VK_SHIFT:   return Key::Shift;
    case VK_CONTROL: return Key::Control;
    case VK_MENU:    return Key::Alt;
    default: return Key::Unknown;
    }
}

void enable_dpi_awareness() {
    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
        using SetDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto fn = reinterpret_cast<SetDpiAwarenessContextFn>(
            ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (fn) {
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }
}

class Win32Input final : public IInput {
public:
    explicit Win32Input(HWND hwnd) : hwnd_(hwnd) {}

    void update() override {
        for (usize i = 0; i < InputState::kKeyCount; ++i) {
            current_.keys_pressed[i]  = false;
            current_.keys_released[i] = false;
        }
        for (usize i = 0; i < InputState::kMouseButtonCount; ++i) {
            current_.mouse_pressed[i]  = false;
            current_.mouse_released[i] = false;
        }

        for (int vk = 0; vk < 256; ++vk) {
            Key key = vk_to_key(vk);
            if (key == Key::Unknown) continue;

            usize idx = key_index(key);
            bool down = (::GetAsyncKeyState(vk) & 0x8000) != 0;
            bool was_down = current_.keys_down[idx];

            current_.keys_down[idx] = down;
            if (down && !was_down) current_.keys_pressed[idx] = true;
            if (!down && was_down) current_.keys_released[idx] = true;
        }

        POINT pt{};
        ::GetCursorPos(&pt);
        ::ScreenToClient(hwnd_, &pt);

        f32 new_x = static_cast<f32>(pt.x);
        f32 new_y = static_cast<f32>(pt.y);
        current_.mouse_dx = new_x - current_.mouse_x;
        current_.mouse_dy = new_y - current_.mouse_y;
        current_.mouse_x = new_x;
        current_.mouse_y = new_y;

        update_mouse_button(0, VK_LBUTTON, MouseButton::Left);
        update_mouse_button(1, VK_RBUTTON, MouseButton::Right);
        update_mouse_button(2, VK_MBUTTON, MouseButton::Middle);
    }

    const InputState& state() const override { return current_; }

private:
    void update_mouse_button(usize idx, int vk, MouseButton button) {
        usize b = static_cast<usize>(button);
        bool down = (::GetAsyncKeyState(vk) & 0x8000) != 0;
        bool was_down = current_.mouse_down[b];

        current_.mouse_down[b] = down;
        if (down && !was_down) current_.mouse_pressed[b] = true;
        if (!down && was_down) current_.mouse_released[b] = true;
        (void)idx;
    }

    HWND hwnd_;
    InputState current_{};
};

class Win32FileSystem final : public IFileSystem {
public:
    std::string resolve(std::string_view path) const override {
        return std::filesystem::weakly_canonical(std::filesystem::path(path)).string();
    }

    bool exists(std::string_view path) const override {
        return std::filesystem::exists(std::filesystem::path(path));
    }

    bool read_file(std::string_view path, std::vector<u8>& out) override {
        std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
        if (!file) return false;

        auto size = file.tellg();
        if (size < 0) return false;

        out.resize(static_cast<size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(out.data()), size);
        return file.good();
    }

    bool write_file(std::string_view path, std::span<const u8> data) override {
        std::filesystem::path resolved = resolve(path);
        std::error_code ec;
        std::filesystem::create_directories(resolved.parent_path(), ec);

        std::ofstream file(resolved, std::ios::binary | std::ios::trunc);
        if (!file) return false;

        file.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
        return file.good();
    }
};

class Win32Platform final : public IPlatform {
public:
    Win32Platform() { enable_dpi_awareness(); }

    std::unique_ptr<IWindow> create_window(const WindowDesc& desc) override {
        return create_win32_window(desc, window_state_);
    }

    std::unique_ptr<IInput> create_input(IWindow& window) override {
        HWND hwnd = static_cast<HWND>(window.native_handle());
        return std::make_unique<Win32Input>(hwnd);
    }

    std::unique_ptr<IFileSystem> create_filesystem() override {
        return std::make_unique<Win32FileSystem>();
    }

    std::string_view name() const override { return "win32"; }

private:
    WindowState window_state_{};
};

} // namespace

std::unique_ptr<IPlatform> create_platform() {
    return std::make_unique<Win32Platform>();
}

} // namespace engine::platform::win32
