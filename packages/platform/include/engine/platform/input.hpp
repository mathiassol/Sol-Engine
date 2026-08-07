#pragma once

#include <engine/core/types.hpp>

namespace engine::platform {

enum class Key : u16 {
    Unknown = 0,
    Escape,
    Space,
    Enter,
    Tab,
    W, A, S, D, Q, E,
    F3,
    Shift, Control, Alt,
    Count,
};

enum class MouseButton : u8 { Left, Right, Middle, Count };

inline usize key_index(Key key) {
    return static_cast<usize>(key);
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
};

class IInput {
public:
    virtual ~IInput() = default;

    virtual void update() = 0;
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
};

} // namespace engine::platform
