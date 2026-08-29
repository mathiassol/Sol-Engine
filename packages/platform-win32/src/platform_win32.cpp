#include <engine/platform/win32/platform_win32.hpp>
#include <engine/platform/win32/window_win32.hpp>

#include <engine/core/log.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>
#include <xinput.h>

namespace engine::platform::win32 {

namespace {

Key vk_to_key(int vk) {
    switch (vk) {
    case VK_ESCAPE:   return Key::Escape;
    case VK_SPACE:    return Key::Space;
    case VK_RETURN:   return Key::Enter;
    case VK_TAB:      return Key::Tab;
    case 'W': return Key::W;
    case 'A': return Key::A;
    case 'S': return Key::S;
    case 'D': return Key::D;
    case 'Q': return Key::Q;
    case 'E': return Key::E;
    case 'Z': return Key::Z;
    case 'X': return Key::X;
    case VK_F3: return Key::F3;
    case VK_F4: return Key::F4;
    case VK_F5: return Key::F5;
    case VK_F11: return Key::F11;
    case VK_SHIFT:   return Key::Shift;
    case VK_CONTROL: return Key::Control;
    case VK_MENU:    return Key::Alt;
    default: return Key::Unknown;
    }
}

constexpr int kTrackedVirtualKeys[] = {
    VK_ESCAPE, VK_SPACE, VK_RETURN, VK_TAB,
    'W', 'A', 'S', 'D', 'Q', 'E', 'Z', 'X',
    VK_F3, VK_F4, VK_F5, VK_F11, VK_SHIFT, VK_CONTROL, VK_MENU,
};

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

        if (!focused_) {
            for (usize i = 0; i < InputState::kKeyCount; ++i) {
                current_.keys_down[i] = false;
            }
            for (usize i = 0; i < InputState::kMouseButtonCount; ++i) {
                current_.mouse_down[i] = false;
            }
            current_.mouse_dx = 0.f;
            current_.mouse_dy = 0.f;
            for (u32 p = 0; p < kMaxGamepads; ++p) {
                gamepad_clear(current_.gamepads[p]);
            }
            return;
        }

        for (int vk : kTrackedVirtualKeys) {
            Key key = vk_to_key(vk);
            if (key == Key::Unknown) continue;

            usize idx = key_index(key);
            if (idx >= InputState::kKeyCount) continue;
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
        poll_gamepads();
    }

    void set_focused(bool focused) override {
        if (focused_ == focused) {
            return;
        }
        focused_ = focused;
        if (!focused) {
            for (usize i = 0; i < InputState::kKeyCount; ++i) {
                current_.keys_down[i] = false;
                current_.keys_pressed[i] = false;
                current_.keys_released[i] = false;
            }
            for (usize i = 0; i < InputState::kMouseButtonCount; ++i) {
                current_.mouse_down[i] = false;
                current_.mouse_pressed[i] = false;
                current_.mouse_released[i] = false;
            }
            current_.mouse_dx = 0.f;
            current_.mouse_dy = 0.f;
            for (u32 p = 0; p < kMaxGamepads; ++p) {
                gamepad_clear(current_.gamepads[p]);
            }
            return;
        }

        POINT pt{};
        ::GetCursorPos(&pt);
        ::ScreenToClient(hwnd_, &pt);
        current_.mouse_x = static_cast<f32>(pt.x);
        current_.mouse_y = static_cast<f32>(pt.y);
        current_.mouse_dx = 0.f;
        current_.mouse_dy = 0.f;
    }

    bool focused() const override { return focused_; }

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

    static f32 thumb_axis(SHORT value) {
        f32 n = static_cast<f32>(value) / 32767.f;
        if (n > 1.f) {
            n = 1.f;
        }
        if (n < -1.f) {
            n = -1.f;
        }
        return n;
    }

    void poll_gamepads() {
        for (u32 p = 0; p < kMaxGamepads; ++p) {
            GamepadState& pad = current_.gamepads[p];
            gamepad_begin_frame(pad);
            XINPUT_STATE xs{};
            if (::XInputGetState(p, &xs) != ERROR_SUCCESS) {
                if (pad.connected) {
                    gamepad_set_button(pad, GamepadButton::A, false);
                    gamepad_set_button(pad, GamepadButton::B, false);
                    gamepad_set_button(pad, GamepadButton::X, false);
                    gamepad_set_button(pad, GamepadButton::Y, false);
                    gamepad_set_button(pad, GamepadButton::LeftShoulder, false);
                    gamepad_set_button(pad, GamepadButton::RightShoulder, false);
                    gamepad_set_button(pad, GamepadButton::Back, false);
                    gamepad_set_button(pad, GamepadButton::Start, false);
                    gamepad_set_button(pad, GamepadButton::LeftStick, false);
                    gamepad_set_button(pad, GamepadButton::RightStick, false);
                    gamepad_set_button(pad, GamepadButton::DpadUp, false);
                    gamepad_set_button(pad, GamepadButton::DpadDown, false);
                    gamepad_set_button(pad, GamepadButton::DpadLeft, false);
                    gamepad_set_button(pad, GamepadButton::DpadRight, false);
                }
                pad.connected = false;
                pad.left_x = pad.left_y = 0.f;
                pad.right_x = pad.right_y = 0.f;
                pad.left_trigger = pad.right_trigger = 0.f;
                continue;
            }

            pad.connected = true;
            f32 lx = thumb_axis(xs.Gamepad.sThumbLX);
            f32 ly = thumb_axis(xs.Gamepad.sThumbLY);
            apply_stick_deadzone(lx, ly);
            pad.left_x = lx;
            pad.left_y = ly;
            f32 rx = thumb_axis(xs.Gamepad.sThumbRX);
            f32 ry = thumb_axis(xs.Gamepad.sThumbRY);
            apply_stick_deadzone(rx, ry);
            pad.right_x = rx;
            pad.right_y = ry;
            pad.left_trigger = apply_trigger_deadzone(
                static_cast<f32>(xs.Gamepad.bLeftTrigger) / 255.f);
            pad.right_trigger = apply_trigger_deadzone(
                static_cast<f32>(xs.Gamepad.bRightTrigger) / 255.f);

            const WORD w = xs.Gamepad.wButtons;
            gamepad_set_button(pad, GamepadButton::A, (w & XINPUT_GAMEPAD_A) != 0);
            gamepad_set_button(pad, GamepadButton::B, (w & XINPUT_GAMEPAD_B) != 0);
            gamepad_set_button(pad, GamepadButton::X, (w & XINPUT_GAMEPAD_X) != 0);
            gamepad_set_button(pad, GamepadButton::Y, (w & XINPUT_GAMEPAD_Y) != 0);
            gamepad_set_button(pad, GamepadButton::LeftShoulder,
                (w & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
            gamepad_set_button(pad, GamepadButton::RightShoulder,
                (w & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
            gamepad_set_button(pad, GamepadButton::Back, (w & XINPUT_GAMEPAD_BACK) != 0);
            gamepad_set_button(pad, GamepadButton::Start, (w & XINPUT_GAMEPAD_START) != 0);
            gamepad_set_button(pad, GamepadButton::LeftStick,
                (w & XINPUT_GAMEPAD_LEFT_THUMB) != 0);
            gamepad_set_button(pad, GamepadButton::RightStick,
                (w & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
            gamepad_set_button(pad, GamepadButton::DpadUp, (w & XINPUT_GAMEPAD_DPAD_UP) != 0);
            gamepad_set_button(pad, GamepadButton::DpadDown, (w & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
            gamepad_set_button(pad, GamepadButton::DpadLeft, (w & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
            gamepad_set_button(pad, GamepadButton::DpadRight,
                (w & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);
        }
    }

    HWND hwnd_;
    InputState current_{};
    bool focused_ = true;
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

    // The OS window outlives the Win32Window wrapper - the wrapper only
    // borrows window_state_, which this class owns. Without this the HWND
    // stayed alive and registered after the engine shut down, and its wndproc
    // kept writing into window_state_ as it was being freed.
    ~Win32Platform() override { destroy_win32_window(window_state_); }

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

    std::string executable_directory() const override {
        wchar_t buffer[MAX_PATH]{};
        const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return {};
        }
        return std::filesystem::path(buffer).parent_path().string();
    }

    std::string executable_file_version() const override {
        wchar_t path[MAX_PATH]{};
        const DWORD length = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return {};
        }
        DWORD dummy = 0;
        const DWORD size = ::GetFileVersionInfoSizeW(path, &dummy);
        if (size == 0) {
            return {};
        }
        std::vector<u8> buffer(size);
        if (!::GetFileVersionInfoW(path, 0, size, buffer.data())) {
            return {};
        }
        VS_FIXEDFILEINFO* info = nullptr;
        UINT info_size = 0;
        if (!::VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&info), &info_size)
            || info == nullptr || info->dwSignature != 0xFEEF04BDu) {
            return {};
        }
        char text[32];
        std::snprintf(text, sizeof(text), "%u.%u.%u",
            HIWORD(info->dwFileVersionMS),
            LOWORD(info->dwFileVersionMS),
            HIWORD(info->dwFileVersionLS));
        return text;
    }

    bool executable_has_icon() const override {
        return ::FindResourceW(
                   ::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(14))
            != nullptr;
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
