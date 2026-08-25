#pragma once

#include <engine/core/types.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/vec3.hpp>

namespace engine::gameplay {

enum class CameraMode : u8 { Follow, Orbit, Fps };

struct GameCameraDesc {
    f32 follow_distance = 3.5f;
    f32 follow_height = 1.4f;
    f32 look_height = 0.4f;
    f32 orbit_distance = 3.5f;
    f32 eye_height = 0.5f;
    f32 pitch_min = -1.4f;
    f32 pitch_max = 1.4f;
    f32 fov_y_deg = 60.f;
    f32 near_z = 0.1f;
    f32 far_z = 100.f;
};

CameraMode next_camera_mode(CameraMode mode);
const char* camera_mode_name(CameraMode mode);

class GameCamera {
public:
    f32 yaw = 0.f;
    f32 pitch = 0.15f;
    GameCameraDesc desc{};

    void set_mode(CameraMode mode);
    CameraMode mode() const { return mode_; }

    void add_look(f32 mouse_dx, f32 mouse_dy);
    void add_look_velocity(f32 yaw_rate, f32 pitch_rate, f32 dt);
    void update(math::Vec3 target);

    math::Vec3 position() const { return position_; }
    math::Vec3 look_at() const { return look_at_; }
    math::Vec3 forward() const;
    math::Vec3 horizontal_forward() const;
    math::Mat4 view() const;
    math::Mat4 projection(f32 aspect) const;

private:
    void clamp_pitch();

    CameraMode mode_ = CameraMode::Follow;
    math::Vec3 position_{};
    math::Vec3 look_at_{};
};

} // namespace engine::gameplay
