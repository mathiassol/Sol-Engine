#include <engine/gameplay/character.hpp>

#include <engine/math/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace engine::gameplay {
namespace {

constexpr f32 kEps = 1.e-5f;
constexpr f32 kGroundStick = 0.12f;
constexpr i32 kSlideIters = 3;

f32 xz_length(math::Vec3 v) {
    return std::sqrt(v.x * v.x + v.z * v.z);
}

math::Vec3 xz_only(math::Vec3 v) {
    return {v.x, 0.f, v.z};
}

math::Vec3 xz_normalized(math::Vec3 v) {
    const f32 len = xz_length(v);
    if (len <= kEps) {
        return {};
    }
    return {v.x / len, 0.f, v.z / len};
}

} // namespace

bool is_walkable_ground(math::Vec3 normal, f32 slope_limit_deg) {
    const f32 limit = std::clamp(slope_limit_deg, 0.f, 90.f);
    return normal.y >= std::cos(math::radians(limit));
}

CharacterController::~CharacterController() {
    destroy();
}

CharacterController::CharacterController(CharacterController&& other) noexcept {
    *this = std::move(other);
}

CharacterController& CharacterController::operator=(CharacterController&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    destroy();
    world_ = other.world_;
    body_ = other.body_;
    desc_ = other.desc_;
    grounded_ = other.grounded_;
    vertical_velocity_ = other.vertical_velocity_;
    other.world_ = nullptr;
    other.body_ = {};
    other.grounded_ = false;
    other.vertical_velocity_ = 0.f;
    return *this;
}

bool CharacterController::spawn(physics::IPhysics& world, math::Vec3 position,
    const CharacterDesc& desc) {
    destroy();
    physics::BodyDesc body{};
    body.shape.type = physics::ShapeType::Capsule;
    body.shape.radius = std::max(desc.radius, 0.f);
    body.shape.half_height = std::max(desc.half_height, 0.f);
    body.position = position;
    body.motion = physics::MotionType::Kinematic;
    body.mask = desc.mask;
    const physics::BodyHandle handle = world.create_body(body);
    if (!handle.valid()) {
        return false;
    }
    world_ = &world;
    body_ = handle;
    desc_ = desc;
    desc_.radius = body.shape.radius;
    desc_.half_height = body.shape.half_height;
    desc_.skin = std::max(desc.skin, 0.f);
    desc_.step_offset = std::max(desc.step_offset, 0.f);
    grounded_ = false;
    vertical_velocity_ = 0.f;
    return true;
}

void CharacterController::destroy() {
    if (world_ && body_.valid()) {
        world_->destroy_body(body_);
    }
    world_ = nullptr;
    body_ = {};
    grounded_ = false;
    vertical_velocity_ = 0.f;
}

math::Vec3 CharacterController::position() const {
    if (!spawned()) {
        return {};
    }
    return world_->position(body_);
}

bool CharacterController::walkable(math::Vec3 normal) const {
    return is_walkable_ground(normal, desc_.slope_limit_deg);
}

void CharacterController::commit(math::Vec3 position) {
    world_->set_position(body_, position);
    world_->set_linear_velocity(body_, {});
}

bool CharacterController::closest_horizontal_hit(math::Vec3 position, math::Vec3 dir,
    f32 max_distance, physics::RaycastHit& out) const {
    if (max_distance <= kEps) {
        return false;
    }
    const f32 h = desc_.half_height;
    const f32 feet = position.y - h - desc_.radius + desc_.skin * 2.f;
    const std::array<math::Vec3, 4> origins{
        math::Vec3{position.x, feet, position.z},
        math::Vec3{position.x, position.y - h, position.z},
        position,
        math::Vec3{position.x, position.y + h, position.z},
    };
    bool any = false;
    f32 best_t = max_distance;
    physics::RaycastHit best{};
    for (const math::Vec3& origin : origins) {
        physics::RaycastHit hit{};
        if (!world_->raycast(origin, dir, max_distance, desc_.mask, hit, body_)) {
            continue;
        }
        if (walkable(hit.normal)) {
            continue;
        }
        const f32 t = hit.fraction * max_distance;
        if (t < best_t) {
            best_t = t;
            best = hit;
            any = true;
        }
    }
    if (any) {
        out = best;
    }
    return any;
}

void CharacterController::move_horizontal(math::Vec3& position, math::Vec3 remaining,
    bool allow_step) {
    remaining = xz_only(remaining);
    for (i32 iter = 0; iter < kSlideIters; ++iter) {
        const f32 len = xz_length(remaining);
        if (len <= kEps) {
            break;
        }
        const math::Vec3 dir = xz_normalized(remaining);
        const f32 max_d = len + desc_.radius + desc_.skin;
        physics::RaycastHit hit{};
        if (!closest_horizontal_hit(position, dir, max_d, hit)) {
            position.x += remaining.x;
            position.z += remaining.z;
            break;
        }
        const f32 t = hit.fraction * max_d;
        const f32 travel = t - desc_.radius - desc_.skin;
        if (travel >= len - kEps) {
            position.x += remaining.x;
            position.z += remaining.z;
            break;
        }
        if (travel > 0.f) {
            position.x += dir.x * travel;
            position.z += dir.z * travel;
            remaining = remaining - dir * travel;
        } else {
            const f32 push = -travel;
            position.x += hit.normal.x * push;
            position.z += hit.normal.z * push;
        }
        if (allow_step && try_step(position, remaining)) {
            break;
        }
        const f32 into = remaining.dot(hit.normal);
        if (into >= 0.f) {
            break;
        }
        remaining = remaining - hit.normal * into;
        remaining.y = 0.f;
    }
}

bool CharacterController::try_step(math::Vec3& position, math::Vec3 remaining) {
    if (desc_.step_offset <= kEps) {
        return false;
    }
    remaining = xz_only(remaining);
    if (xz_length(remaining) <= kEps) {
        return false;
    }
    physics::RaycastHit ceiling{};
    const f32 up_dist = desc_.step_offset + desc_.radius + desc_.skin;
    if (world_->raycast({position.x, position.y + desc_.half_height, position.z}, {0.f, 1.f, 0.f},
            up_dist, desc_.mask, ceiling, body_)) {
        return false;
    }
    math::Vec3 lifted = position;
    lifted.y += desc_.step_offset;
    const math::Vec3 before = lifted;
    const math::Vec3 dir = xz_normalized(remaining);
    const f32 step_dist = std::max(xz_length(remaining), desc_.radius + desc_.skin + 0.05f);
    move_horizontal(lifted, dir * step_dist, false);
    if (xz_length(lifted - before) <= kEps) {
        return false;
    }
    physics::RaycastHit ground{};
    const f32 down = desc_.step_offset + rest_offset() + desc_.skin;
    if (!world_->raycast(lifted, {0.f, -1.f, 0.f}, down, desc_.mask, ground, body_)
        || !walkable(ground.normal)) {
        return false;
    }
    const f32 rest = ground.point.y + rest_offset();
    if (rest > position.y + desc_.step_offset + desc_.skin) {
        return false;
    }
    if (rest <= position.y + desc_.skin) {
        return false;
    }
    lifted.y = rest;
    position = lifted;
    return true;
}

void CharacterController::move_vertical(math::Vec3& position, f32 dt, bool was_grounded) {
    if (vertical_velocity_ > 0.f) {
        const f32 up = vertical_velocity_ * dt + desc_.radius + desc_.skin;
        physics::RaycastHit ceiling{};
        const math::Vec3 origin{position.x, position.y + desc_.half_height, position.z};
        if (world_->raycast(origin, {0.f, 1.f, 0.f}, up, desc_.mask, ceiling, body_)) {
            const f32 t = ceiling.fraction * up;
            position.y += std::max(t - desc_.radius - desc_.skin, 0.f);
            vertical_velocity_ = 0.f;
            grounded_ = false;
            return;
        }
        position.y += vertical_velocity_ * dt;
        grounded_ = false;
        return;
    }

    const f32 extra = was_grounded ? kGroundStick : (desc_.skin * 2.f);
    const f32 fall = std::max(-vertical_velocity_ * dt, 0.f);
    const f32 probe = rest_offset() + extra + fall;
    physics::RaycastHit ground{};
    if (world_->raycast(position, {0.f, -1.f, 0.f}, probe, desc_.mask, ground, body_)
        && walkable(ground.normal)) {
        const f32 rest = ground.point.y + rest_offset();
        const f32 gap = position.y - rest;
        if (gap <= extra) {
            position.y = rest;
            vertical_velocity_ = 0.f;
            grounded_ = true;
            return;
        }
    }
    position.y += vertical_velocity_ * dt;
    grounded_ = false;
}

void CharacterController::move(math::Vec3 wish_dir, bool jump, f32 dt) {
    if (!spawned() || dt <= 0.f) {
        return;
    }

    math::Vec3 position = world_->position(body_);
    const bool was_grounded = grounded_;

    if (jump && grounded_) {
        vertical_velocity_ = desc_.jump_speed;
        grounded_ = false;
    }
    if (!grounded_) {
        vertical_velocity_ += world_->gravity().y * dt;
        vertical_velocity_ = std::max(vertical_velocity_, -50.f);
    } else {
        vertical_velocity_ = 0.f;
    }

    const f32 mag = std::min(xz_length(wish_dir), 1.f);
    const math::Vec3 wish = xz_normalized(wish_dir) * (desc_.walk_speed * dt * mag);
    move_horizontal(position, wish, true);
    move_vertical(position, dt, was_grounded);
    commit(position);
}

} // namespace engine::gameplay
