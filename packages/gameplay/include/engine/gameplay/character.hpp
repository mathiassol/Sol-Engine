#pragma once

#include <engine/core/types.hpp>
#include <engine/math/vec3.hpp>
#include <engine/physics/physics.hpp>

namespace engine::gameplay {

struct CharacterDesc {
    f32 radius = 0.28f;
    f32 half_height = 0.4f;
    f32 walk_speed = 4.f;
    f32 jump_speed = 5.5f;
    f32 step_offset = 0.35f;
    f32 slope_limit_deg = 45.f;
    f32 skin = 0.02f;
    u32 mask = physics::kAllLayers;
};

bool is_walkable_ground(math::Vec3 normal, f32 slope_limit_deg);

class CharacterController {
public:
    CharacterController() = default;
    ~CharacterController();

    CharacterController(const CharacterController&) = delete;
    CharacterController& operator=(const CharacterController&) = delete;
    CharacterController(CharacterController&& other) noexcept;
    CharacterController& operator=(CharacterController&& other) noexcept;

    bool spawn(physics::IPhysics& world, math::Vec3 position, const CharacterDesc& desc = {});
    void destroy();

    void move(math::Vec3 wish_dir, bool jump, f32 dt);

    bool spawned() const { return world_ != nullptr && body_.valid(); }
    bool grounded() const { return grounded_; }
    math::Vec3 position() const;
    f32 rest_offset() const { return desc_.half_height + desc_.radius; }
    physics::BodyHandle body() const { return body_; }
    const CharacterDesc& desc() const { return desc_; }

private:
    void commit(math::Vec3 position);
    bool closest_horizontal_hit(math::Vec3 position, math::Vec3 dir, f32 max_distance,
        physics::RaycastHit& out) const;
    void move_horizontal(math::Vec3& position, math::Vec3 remaining, bool allow_step);
    bool try_step(math::Vec3& position, math::Vec3 remaining);
    void move_vertical(math::Vec3& position, f32 dt, bool was_grounded);
    bool walkable(math::Vec3 normal) const;

    physics::IPhysics* world_ = nullptr;
    physics::BodyHandle body_{};
    CharacterDesc desc_{};
    bool grounded_ = false;
    f32 vertical_velocity_ = 0.f;
};

} // namespace engine::gameplay
