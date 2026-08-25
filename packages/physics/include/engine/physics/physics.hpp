#pragma once

#include <engine/core/types.hpp>
#include <engine/math/aabb.hpp>
#include <engine/math/vec3.hpp>

#include <span>
#include <string_view>

namespace engine::physics {

// Overlap queries + translation-only rigid bodies (AABB, sphere, capsule),
// sensor enter/exit, and closest-hit raycasts. Angular/OBB later.
// Swap backends by linking a different physics-* package.

inline constexpr u32 kDefaultLayer = 1u;
inline constexpr u32 kAllLayers = ~0u;
inline constexpr u32 kMaxBodies = 256;
inline constexpr u32 kMaxTriggerEvents = 256;
inline constexpr f32 kFatMargin = 0.15f;
inline constexpr math::Vec3 kDefaultGravity{0.f, -9.81f, 0.f};

struct BodyHandle {
    u32 id = 0;
    u32 generation = 0;

    bool valid() const { return id != 0 && generation != 0; }
    bool operator==(BodyHandle other) const {
        return id == other.id && generation == other.generation;
    }
};

enum class TriggerEventType : u8 { Enter, Exit };

struct TriggerEvent {
    BodyHandle a{};
    BodyHandle b{};
    TriggerEventType type = TriggerEventType::Enter;
};

struct RaycastHit {
    BodyHandle body{};
    math::Vec3 point{};
    math::Vec3 normal{};
    f32 fraction = 0.f;
};

enum class ShapeType : u8 { Aabb, Sphere, Capsule };

enum class MotionType : u8 { Static, Dynamic, Kinematic };

struct ShapeDesc {
    ShapeType type = ShapeType::Aabb;
    math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
    f32 radius = 0.5f;
    f32 half_height = 0.5f; // capsule cylinder half-height (Jolt); ignored otherwise
};

struct BodyDesc {
    ShapeDesc shape{};
    math::Vec3 position{};
    u32 layer = kDefaultLayer;
    u32 mask = kAllLayers;
    bool sensor = false;
    u32 user_data = 0;
    MotionType motion = MotionType::Static;
    f32 mass = 1.f;
    f32 restitution = 0.f;
    f32 friction = 0.4f;
};

class IPhysics {
public:
    virtual ~IPhysics() = default;

    virtual BodyHandle create_body(const BodyDesc& desc) = 0;
    virtual void destroy_body(BodyHandle handle) = 0;
    virtual bool set_position(BodyHandle handle, math::Vec3 position) = 0;
    virtual bool set_linear_velocity(BodyHandle handle, math::Vec3 velocity) = 0;
    virtual math::Vec3 position(BodyHandle handle) const = 0;
    virtual math::Vec3 linear_velocity(BodyHandle handle) const = 0;
    virtual void set_gravity(math::Vec3 gravity) = 0;
    virtual math::Vec3 gravity() const = 0;
    virtual void step(f32 dt) = 0;
    virtual u32 overlap_aabb(const math::Aabb& box, u32 mask, std::span<BodyHandle> out) const = 0;
    virtual u32 overlap_sphere(math::Vec3 center, f32 radius, u32 mask,
        std::span<BodyHandle> out) const = 0;
    virtual u32 overlap_capsule(math::Vec3 position, f32 radius, f32 half_height, u32 mask,
        std::span<BodyHandle> out) const = 0;
    virtual u32 trigger_events(std::span<TriggerEvent> out) const = 0;
    virtual bool raycast(math::Vec3 origin, math::Vec3 direction, f32 max_distance, u32 mask,
        RaycastHit& out, BodyHandle ignore = {}) const = 0;
    virtual u32 body_count() const = 0;
    virtual std::string_view name() const = 0;
};

} // namespace engine::physics
