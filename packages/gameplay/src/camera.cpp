#include <engine/gameplay/camera.hpp>

#include <engine/math/constants.hpp>

#include <algorithm>
#include <cmath>

namespace engine::gameplay {
namespace {

constexpr f32 kLookSensitivity = 0.003f;

math::Vec3 yaw_pitch_forward(f32 yaw, f32 pitch) {
    const f32 cy = std::cos(yaw);
    const f32 sy = std::sin(yaw);
    const f32 cp = std::cos(pitch);
    const f32 sp = std::sin(pitch);
    return {sy * cp, sp, cy * cp};
}

} // namespace

CameraMode next_camera_mode(CameraMode mode) {
    switch (mode) {
    case CameraMode::Follow:
        return CameraMode::Orbit;
    case CameraMode::Orbit:
        return CameraMode::Fps;
    case CameraMode::Fps:
        return CameraMode::Follow;
    }
    return CameraMode::Follow;
}

const char* camera_mode_name(CameraMode mode) {
    switch (mode) {
    case CameraMode::Follow:
        return "follow";
    case CameraMode::Orbit:
        return "orbit";
    case CameraMode::Fps:
        return "fps";
    }
    return "follow";
}

void GameCamera::set_mode(CameraMode mode) {
    mode_ = mode;
}

void GameCamera::clamp_pitch() {
    pitch = std::clamp(pitch, desc.pitch_min, desc.pitch_max);
}

void GameCamera::add_look(f32 mouse_dx, f32 mouse_dy) {
    yaw += mouse_dx * kLookSensitivity;
    pitch += mouse_dy * kLookSensitivity;
    clamp_pitch();
}

void GameCamera::add_look_velocity(f32 yaw_rate, f32 pitch_rate, f32 dt) {
    yaw += yaw_rate * dt;
    pitch += pitch_rate * dt;
    clamp_pitch();
}

math::Vec3 GameCamera::forward() const {
    return yaw_pitch_forward(yaw, pitch);
}

math::Vec3 GameCamera::horizontal_forward() const {
    return {std::sin(yaw), 0.f, std::cos(yaw)};
}

void GameCamera::update(math::Vec3 target) {
    clamp_pitch();
    const math::Vec3 chest = target + math::Vec3{0.f, desc.look_height, 0.f};
    const f32 sy = std::sin(yaw);
    const f32 cy = std::cos(yaw);

    switch (mode_) {
    case CameraMode::Follow: {
        const f32 d = std::max(desc.follow_distance, 0.01f);
        position_ = {
            target.x - sy * d,
            target.y + desc.follow_height,
            target.z - cy * d,
        };
        look_at_ = chest;
        break;
    }
    case CameraMode::Orbit: {
        const f32 d = std::max(desc.orbit_distance, 0.01f);
        const f32 cp = std::cos(pitch);
        const f32 sp = std::sin(pitch);
        position_ = chest + math::Vec3{
            -sy * cp * d,
            sp * d,
            -cy * cp * d,
        };
        look_at_ = chest;
        break;
    }
    case CameraMode::Fps: {
        position_ = target + math::Vec3{0.f, desc.eye_height, 0.f};
        look_at_ = position_ + yaw_pitch_forward(yaw, pitch);
        break;
    }
    }
}

math::Mat4 GameCamera::view() const {
    return math::Mat4::look_at(position_, look_at_, {0.f, 1.f, 0.f});
}

math::Mat4 GameCamera::projection(f32 aspect, bool reversed_z) const {
    const f32 a = aspect > 0.f ? aspect : 1.f;
    const f32 fov = math::radians(desc.fov_y_deg);
    return reversed_z
        ? math::Mat4::perspective_reversed_z(fov, a, desc.near_z, desc.far_z)
        : math::Mat4::perspective(fov, a, desc.near_z, desc.far_z);
}

} // namespace engine::gameplay
