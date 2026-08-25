#pragma once

#include <engine/core/types.hpp>

#include <algorithm>
#include <cmath>

namespace engine::platform {

enum class Key : u16 {
    Unknown = 0,
    Escape,
    Space,
    Enter,
    Tab,
    W, A, S, D, Q, E,
    Z, X,
    F3, F4, F5, F11,
    Shift, Control, Alt,
    Count,
};

enum class MouseButton : u8 { Left, Right, Middle, Count };

enum class GamepadButton : u8 {
    A,
    B,
    X,
    Y,
    LeftShoulder,
    RightShoulder,
    Back,
    Start,
    LeftStick,
    RightStick,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count,
};

inline constexpr u32 kMaxGamepads = 4;
inline constexpr f32 kStickDeadzone = 0.24f;
inline constexpr f32 kTriggerDeadzone = 0.1f;

inline usize key_index(Key key) {
    return static_cast<usize>(key);
}

inline usize gamepad_button_index(GamepadButton button) {
    return static_cast<usize>(button);
}

struct GamepadState {
    static constexpr usize kButtonCount = static_cast<usize>(GamepadButton::Count);

    bool connected = false;
    f32 left_x = 0.f;
    f32 left_y = 0.f;
    f32 right_x = 0.f;
    f32 right_y = 0.f;
    f32 left_trigger = 0.f;
    f32 right_trigger = 0.f;
    bool buttons_down[kButtonCount]{};
    bool buttons_pressed[kButtonCount]{};
    bool buttons_released[kButtonCount]{};
};

inline void apply_stick_deadzone(f32& x, f32& y, f32 zone = kStickDeadzone) {
    const f32 mag = std::sqrt(x * x + y * y);
    if (mag <= zone || mag <= 1.e-8f) {
        x = 0.f;
        y = 0.f;
        return;
    }
    const f32 scale = (mag >= 1.f) ? (1.f / mag) : ((mag - zone) / ((1.f - zone) * mag));
    x *= scale;
    y *= scale;
    x = std::clamp(x, -1.f, 1.f);
    y = std::clamp(y, -1.f, 1.f);
}

inline f32 apply_trigger_deadzone(f32 value, f32 zone = kTriggerDeadzone) {
    if (value <= zone) {
        return 0.f;
    }
    const f32 t = (value - zone) / (1.f - zone);
    return t > 1.f ? 1.f : t;
}

inline void gamepad_begin_frame(GamepadState& pad) {
    for (usize i = 0; i < GamepadState::kButtonCount; ++i) {
        pad.buttons_pressed[i] = false;
        pad.buttons_released[i] = false;
    }
}

inline void gamepad_set_button(GamepadState& pad, GamepadButton button, bool down) {
    const usize i = gamepad_button_index(button);
    if (i >= GamepadState::kButtonCount) {
        return;
    }
    const bool was_down = pad.buttons_down[i];
    pad.buttons_down[i] = down;
    if (down && !was_down) {
        pad.buttons_pressed[i] = true;
    }
    if (!down && was_down) {
        pad.buttons_released[i] = true;
    }
}

inline void gamepad_clear(GamepadState& pad) {
    pad = {};
}

struct InputState {
    static constexpr usize kKeyCount = static_cast<usize>(Key::Count);
    static constexpr usize kMouseButtonCount = static_cast<usize>(MouseButton::Count);

    bool keys_down[kKeyCount]{};
    bool keys_pressed[kKeyCount]{};
    bool keys_released[kKeyCount]{};

    f32 mouse_x = 0.f;
    f32 mouse_y = 0.f;
    f32 mouse_dx = 0.f;
    f32 mouse_dy = 0.f;

    bool mouse_down[kMouseButtonCount]{};
    bool mouse_pressed[kMouseButtonCount]{};
    bool mouse_released[kMouseButtonCount]{};

    GamepadState gamepads[kMaxGamepads]{};
};

class IInput {
public:
    virtual ~IInput() = default;

    virtual void update() = 0;
    virtual void set_focused(bool focused) = 0;
    virtual bool focused() const = 0;
    virtual const InputState& state() const = 0;

    bool key_down(Key key) const {
        usize i = key_index(key);
        return i < InputState::kKeyCount && state().keys_down[i];
    }
    bool key_pressed(Key key) const {
        usize i = key_index(key);
        return i < InputState::kKeyCount && state().keys_pressed[i];
    }
    bool key_released(Key key) const {
        usize i = key_index(key);
        return i < InputState::kKeyCount && state().keys_released[i];
    }

    bool button_down(GamepadButton button, u32 pad = 0) const {
        const usize i = gamepad_button_index(button);
        return pad < kMaxGamepads && i < GamepadState::kButtonCount
            && state().gamepads[pad].buttons_down[i];
    }
    bool button_pressed(GamepadButton button, u32 pad = 0) const {
        const usize i = gamepad_button_index(button);
        return pad < kMaxGamepads && i < GamepadState::kButtonCount
            && state().gamepads[pad].buttons_pressed[i];
    }
    bool button_released(GamepadButton button, u32 pad = 0) const {
        const usize i = gamepad_button_index(button);
        return pad < kMaxGamepads && i < GamepadState::kButtonCount
            && state().gamepads[pad].buttons_released[i];
    }
};

} // namespace engine::platform
