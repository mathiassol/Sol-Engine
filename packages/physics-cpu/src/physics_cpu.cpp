#include <engine/physics/cpu/physics_cpu.hpp>

#include <engine/core/log.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::physics::cpu {
namespace {

constexpr u32 kNull = ~0u;
constexpr u32 kMaxNodes = kMaxBodies * 2;
constexpr u32 kMaxContacts = 512;
constexpr u32 kMaxTriggerPairs = 512;
constexpr u32 kVelocityIterations = 8;
constexpr u32 kPositionIterations = 3;
constexpr f32 kLinearDamping = 0.1f;
constexpr f32 kSlop = 0.005f;
constexpr f32 kBaumgarte = 0.2f;
constexpr f32 kBounceThreshold = 1.f;
constexpr f32 kFrictionEpsilon = 1.e-6f;
constexpr u32 kClosestIters = 8;
constexpr f32 kSegEps = 1.e-8f;

f32 shape_radius(const ShapeDesc& shape) {
    return std::max(shape.radius, 0.f);
}

f32 capsule_half_height(const ShapeDesc& shape) {
    return std::max(shape.half_height, 0.f);
}

void capsule_ends(const ShapeDesc& shape, math::Vec3 position, math::Vec3& a, math::Vec3& b) {
    const f32 h = capsule_half_height(shape);
    a = {position.x, position.y + h, position.z};
    b = {position.x, position.y - h, position.z};
}

math::Aabb tight_bounds(const ShapeDesc& shape, math::Vec3 position) {
    if (shape.type == ShapeType::Sphere) {
        const f32 r = shape_radius(shape);
        return math::Aabb::from_center_half(position, {r, r, r});
    }
    if (shape.type == ShapeType::Capsule) {
        const f32 r = shape_radius(shape);
        const f32 hy = capsule_half_height(shape) + r;
        return math::Aabb::from_center_half(position, {r, hy, r});
    }
    const math::Vec3 he{
        std::max(shape.half_extents.x, 0.f),
        std::max(shape.half_extents.y, 0.f),
        std::max(shape.half_extents.z, 0.f),
    };
    return math::Aabb::from_center_half(position, he);
}

math::Vec3 closest_on_segment(math::Vec3 p, math::Vec3 a, math::Vec3 b) {
    const math::Vec3 ab = b - a;
    const f32 denom = ab.dot(ab);
    if (denom <= kSegEps) {
        return a;
    }
    const f32 t = std::clamp((p - a).dot(ab) / denom, 0.f, 1.f);
    return a + ab * t;
}

// Ericson 5.1.9 — closest points of two line segments.
void closest_pts_segments(math::Vec3 p1, math::Vec3 q1, math::Vec3 p2, math::Vec3 q2,
    math::Vec3& c1, math::Vec3& c2) {
    const math::Vec3 d1 = q1 - p1;
    const math::Vec3 d2 = q2 - p2;
    const math::Vec3 r = p1 - p2;
    const f32 a = d1.dot(d1);
    const f32 e = d2.dot(d2);
    const f32 f = d2.dot(r);
    f32 s = 0.f;
    f32 t = 0.f;

    if (a <= kSegEps && e <= kSegEps) {
        c1 = p1;
        c2 = p2;
        return;
    }
    if (a <= kSegEps) {
        t = std::clamp(f / e, 0.f, 1.f);
    } else {
        const f32 c = d1.dot(r);
        if (e <= kSegEps) {
            s = std::clamp(-c / a, 0.f, 1.f);
        } else {
            const f32 b = d1.dot(d2);
            const f32 denom = a * e - b * b;
            if (denom > kSegEps) {
                s = std::clamp((b * f - c * e) / denom, 0.f, 1.f);
            }
            t = (b * s + f) / e;
            if (t < 0.f) {
                t = 0.f;
                s = std::clamp(-c / a, 0.f, 1.f);
            } else if (t > 1.f) {
                t = 1.f;
                s = std::clamp((b - c) / a, 0.f, 1.f);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

void closest_segment_aabb(math::Vec3 a, math::Vec3 b, const math::Aabb& box, math::Vec3& on_seg,
    math::Vec3& on_box) {
    on_seg = a;
    for (u32 i = 0; i < kClosestIters; ++i) {
        on_box = box.closest_point(on_seg);
        on_seg = closest_on_segment(on_box, a, b);
    }
    on_box = box.closest_point(on_seg);
}

bool sphere_overlaps_aabb(math::Vec3 center, f32 radius, const math::Aabb& box) {
    const math::Vec3 delta = center - box.closest_point(center);
    return delta.dot(delta) <= radius * radius;
}

bool capsule_overlaps_aabb(const ShapeDesc& shape, math::Vec3 position, const math::Aabb& box) {
    math::Vec3 a{};
    math::Vec3 b{};
    capsule_ends(shape, position, a, b);
    math::Vec3 on_seg{};
    math::Vec3 on_box{};
    closest_segment_aabb(a, b, box, on_seg, on_box);
    const math::Vec3 delta = on_seg - on_box;
    const f32 r = shape_radius(shape);
    return delta.dot(delta) <= r * r;
}

bool capsule_overlaps_sphere(const ShapeDesc& shape, math::Vec3 position, math::Vec3 center,
    f32 radius) {
    math::Vec3 a{};
    math::Vec3 b{};
    capsule_ends(shape, position, a, b);
    const math::Vec3 p = closest_on_segment(center, a, b);
    const math::Vec3 delta = center - p;
    const f32 sum = shape_radius(shape) + std::max(radius, 0.f);
    return delta.dot(delta) <= sum * sum;
}

bool capsules_overlap(const ShapeDesc& a, math::Vec3 pa, const ShapeDesc& b, math::Vec3 pb) {
    math::Vec3 a0{};
    math::Vec3 a1{};
    math::Vec3 b0{};
    math::Vec3 b1{};
    capsule_ends(a, pa, a0, a1);
    capsule_ends(b, pb, b0, b1);
    math::Vec3 ca{};
    math::Vec3 cb{};
    closest_pts_segments(a0, a1, b0, b1, ca, cb);
    const math::Vec3 delta = cb - ca;
    const f32 sum = shape_radius(a) + shape_radius(b);
    return delta.dot(delta) <= sum * sum;
}

bool shapes_overlap_aabb(const ShapeDesc& shape, math::Vec3 position, const math::Aabb& box) {
    if (shape.type == ShapeType::Sphere) {
        return sphere_overlaps_aabb(position, shape_radius(shape), box);
    }
    if (shape.type == ShapeType::Capsule) {
        return capsule_overlaps_aabb(shape, position, box);
    }
    return tight_bounds(shape, position).overlaps(box);
}

bool shapes_overlap_sphere(const ShapeDesc& shape, math::Vec3 position, math::Vec3 center,
    f32 radius) {
    const f32 r = std::max(radius, 0.f);
    if (shape.type == ShapeType::Sphere) {
        const math::Vec3 delta = position - center;
        const f32 sum = shape_radius(shape) + r;
        return delta.dot(delta) <= sum * sum;
    }
    if (shape.type == ShapeType::Capsule) {
        return capsule_overlaps_sphere(shape, position, center, r);
    }
    return sphere_overlaps_aabb(center, r, tight_bounds(shape, position));
}

bool shapes_overlap_capsule(const ShapeDesc& shape, math::Vec3 position, math::Vec3 cap_pos,
    f32 radius, f32 half_height) {
    ShapeDesc query{};
    query.type = ShapeType::Capsule;
    query.radius = std::max(radius, 0.f);
    query.half_height = std::max(half_height, 0.f);
    if (shape.type == ShapeType::Sphere) {
        return capsule_overlaps_sphere(query, cap_pos, position, shape_radius(shape));
    }
    if (shape.type == ShapeType::Capsule) {
        return capsules_overlap(shape, position, query, cap_pos);
    }
    return capsule_overlaps_aabb(query, cap_pos, tight_bounds(shape, position));
}

bool shapes_overlap(const ShapeDesc& a, math::Vec3 pa, const ShapeDesc& b, math::Vec3 pb) {
    if (b.type == ShapeType::Sphere) {
        return shapes_overlap_sphere(a, pa, pb, shape_radius(b));
    }
    if (b.type == ShapeType::Capsule) {
        return shapes_overlap_capsule(a, pa, pb, shape_radius(b), capsule_half_height(b));
    }
    return shapes_overlap_aabb(a, pa, tight_bounds(b, pb));
}

constexpr f32 kRayParallel = 1.e-12f;

bool ray_aabb_interval(math::Vec3 o, math::Vec3 d, const math::Aabb& box, f32& tmin, f32& tmax,
    int& enter_axis, f32& enter_sign) {
    tmin = -1.e30f;
    tmax = 1.e30f;
    enter_axis = 0;
    enter_sign = -1.f;

    auto slab = [&](f32 orig, f32 dir, f32 mn, f32 mx, int axis) {
        if (std::abs(dir) <= kRayParallel) {
            return orig >= mn && orig <= mx;
        }
        const f32 inv = 1.f / dir;
        f32 t1 = (mn - orig) * inv;
        f32 t2 = (mx - orig) * inv;
        f32 sign = -1.f;
        if (t1 > t2) {
            std::swap(t1, t2);
            sign = 1.f;
        }
        if (t1 > tmin) {
            tmin = t1;
            enter_axis = axis;
            enter_sign = sign;
        }
        tmax = std::min(tmax, t2);
        return tmin <= tmax;
    };

    return slab(o.x, d.x, box.min.x, box.max.x, 0) && slab(o.y, d.y, box.min.y, box.max.y, 1)
        && slab(o.z, d.z, box.min.z, box.max.z, 2);
}

bool ray_hits_aabb_bounds(math::Vec3 o, math::Vec3 d, f32 max_t, const math::Aabb& box) {
    f32 tmin = 0.f;
    f32 tmax = 0.f;
    int axis = 0;
    f32 sign = 0.f;
    if (!ray_aabb_interval(o, d, box, tmin, tmax, axis, sign)) {
        return false;
    }
    return tmax >= 0.f && tmin <= max_t;
}

math::Vec3 aabb_enter_normal(int axis, f32 sign) {
    if (axis == 0) {
        return {sign, 0.f, 0.f};
    }
    if (axis == 1) {
        return {0.f, sign, 0.f};
    }
    return {0.f, 0.f, sign};
}

bool ray_aabb_hit(math::Vec3 o, math::Vec3 d, f32 max_t, const math::Aabb& box, f32& t,
    math::Vec3& normal) {
    f32 tmin = 0.f;
    f32 tmax = 0.f;
    int axis = 0;
    f32 sign = 0.f;
    if (!ray_aabb_interval(o, d, box, tmin, tmax, axis, sign)) {
        return false;
    }
    if (tmin < 0.f || tmin > max_t || tmin > tmax) {
        return false;
    }
    t = tmin;
    normal = aabb_enter_normal(axis, sign);
    return true;
}

bool ray_sphere_hit(math::Vec3 o, math::Vec3 d, f32 max_t, math::Vec3 center, f32 radius, f32& t,
    math::Vec3& normal) {
    const f32 r = std::max(radius, 0.f);
    const math::Vec3 m = o - center;
    const f32 b = m.dot(d);
    const f32 c = m.dot(m) - r * r;
    if (c > 0.f && b > 0.f) {
        return false;
    }
    const f32 disc = b * b - c;
    if (disc < 0.f) {
        return false;
    }
    const f32 t0 = -b - std::sqrt(disc);
    if (t0 < 0.f || t0 > max_t) {
        return false;
    }
    t = t0;
    const math::Vec3 p = o + d * t0;
    const math::Vec3 n = p - center;
    const f32 len2 = n.dot(n);
    normal = len2 > kSegEps ? n * (1.f / std::sqrt(len2)) : math::Vec3{-d.x, -d.y, -d.z};
    return true;
}

bool ray_capsule_hit(math::Vec3 o, math::Vec3 d, f32 max_t, const ShapeDesc& shape,
    math::Vec3 position, f32& t, math::Vec3& normal) {
    math::Vec3 a{};
    math::Vec3 b{};
    capsule_ends(shape, position, a, b);
    const f32 r = shape_radius(shape);
    const f32 y0 = std::min(a.y, b.y);
    const f32 y1 = std::max(a.y, b.y);

    f32 best = max_t + 1.f;
    math::Vec3 best_n{};
    bool hit = false;

    auto consider = [&](f32 cand, math::Vec3 n) {
        if (cand < 0.f || cand > max_t || cand >= best) {
            return;
        }
        best = cand;
        best_n = n;
        hit = true;
    };

    f32 ts = 0.f;
    math::Vec3 ns{};
    if (ray_sphere_hit(o, d, max_t, a, r, ts, ns)) {
        consider(ts, ns);
    }
    if (ray_sphere_hit(o, d, max_t, b, r, ts, ns)) {
        consider(ts, ns);
    }

    const f32 ox = o.x - position.x;
    const f32 oz = o.z - position.z;
    const f32 A = d.x * d.x + d.z * d.z;
    if (A > kRayParallel) {
        const f32 B = 2.f * (ox * d.x + oz * d.z);
        const f32 C = ox * ox + oz * oz - r * r;
        const f32 disc = B * B - 4.f * A * C;
        if (disc >= 0.f) {
            const f32 tc = (-B - std::sqrt(disc)) / (2.f * A);
            const f32 y = o.y + d.y * tc;
            if (tc >= 0.f && tc <= max_t && y >= y0 && y <= y1) {
                math::Vec3 n{ox + d.x * tc, 0.f, oz + d.z * tc};
                const f32 len2 = n.dot(n);
                n = len2 > kSegEps ? n * (1.f / std::sqrt(len2)) : math::Vec3{-d.x, 0.f, -d.z};
                consider(tc, n);
            }
        }
    }

    if (!hit) {
        return false;
    }
    t = best;
    normal = best_n;
    return true;
}

bool ray_shape_hit(const ShapeDesc& shape, math::Vec3 position, const math::Aabb& tight,
    math::Vec3 o, math::Vec3 d, f32 max_t, f32& t, math::Vec3& normal) {
    if (shape.type == ShapeType::Sphere) {
        return ray_sphere_hit(o, d, max_t, position, shape_radius(shape), t, normal);
    }
    if (shape.type == ShapeType::Capsule) {
        return ray_capsule_hit(o, d, max_t, shape, position, t, normal);
    }
    return ray_aabb_hit(o, d, max_t, tight, t, normal);
}

f32 inv_mass_of(MotionType motion, f32 mass) {
    if (motion != MotionType::Dynamic || mass <= 0.f) {
        return 0.f;
    }
    return 1.f / mass;
}

struct Node {
    math::Aabb bounds{};
    u32 parent = kNull;
    u32 child1 = kNull;
    u32 child2 = kNull;
    i32 height = -1;
    u32 body = 0;
};

bool is_leaf(const Node& node) {
    return node.child1 == kNull;
}

struct StoredBody {
    ShapeDesc shape{};
    math::Vec3 position{};
    math::Vec3 velocity{};
    math::Aabb tight{};
    math::Aabb fat{};
    u32 layer = kDefaultLayer;
    u32 mask = kAllLayers;
    bool sensor = false;
    u32 user_data = 0;
    MotionType motion = MotionType::Static;
    f32 inv_mass = 0.f;
    f32 restitution = 0.f;
    f32 friction = 0.4f;
    u32 generation = 0;
    u32 leaf = kNull;
    bool live = false;
};

struct Contact {
    u32 a = 0;
    u32 b = 0;
    math::Vec3 normal{};
    f32 penetration = 0.f;
    f32 inv_mass_sum = 0.f;
    f32 restitution = 0.f;
    f32 friction = 0.f;
    f32 acc_n = 0.f;
    f32 acc_t = 0.f;
    f32 vn0 = 0.f;
};

bool contact_aabb_aabb(const StoredBody& a, const StoredBody& b, Contact& out) {
    const math::Aabb& A = a.tight;
    const math::Aabb& B = b.tight;
    const f32 dx = std::min(A.max.x - B.min.x, B.max.x - A.min.x);
    const f32 dy = std::min(A.max.y - B.min.y, B.max.y - A.min.y);
    const f32 dz = std::min(A.max.z - B.min.z, B.max.z - A.min.z);
    if (dx <= 0.f || dy <= 0.f || dz <= 0.f) {
        return false;
    }

    math::Vec3 n{};
    f32 pen = dx;
    n = (a.position.x <= b.position.x) ? math::Vec3{1.f, 0.f, 0.f} : math::Vec3{-1.f, 0.f, 0.f};
    if (dy < pen) {
        pen = dy;
        n = (a.position.y <= b.position.y) ? math::Vec3{0.f, 1.f, 0.f} : math::Vec3{0.f, -1.f, 0.f};
    }
    if (dz < pen) {
        pen = dz;
        n = (a.position.z <= b.position.z) ? math::Vec3{0.f, 0.f, 1.f} : math::Vec3{0.f, 0.f, -1.f};
    }
    out.normal = n;
    out.penetration = pen;
    return true;
}

bool contact_sphere_sphere(const StoredBody& a, const StoredBody& b, Contact& out) {
    const math::Vec3 delta = b.position - a.position;
    const f32 ra = std::max(a.shape.radius, 0.f);
    const f32 rb = std::max(b.shape.radius, 0.f);
    const f32 dist2 = delta.dot(delta);
    const f32 r = ra + rb;
    if (dist2 > r * r) {
        return false;
    }
    const f32 dist = std::sqrt(dist2);
    if (dist > 1.e-8f) {
        out.normal = delta * (1.f / dist);
        out.penetration = r - dist;
    } else {
        out.normal = {0.f, 1.f, 0.f};
        out.penetration = r;
    }
    return true;
}

bool contact_sphere_aabb(const StoredBody& sphere, const StoredBody& box, bool sphere_is_a,
    Contact& out) {
    const f32 radius = std::max(sphere.shape.radius, 0.f);
    const math::Vec3 closest = box.tight.closest_point(sphere.position);
    math::Vec3 delta = sphere.position - closest;
    const f32 dist2 = delta.dot(delta);

    math::Vec3 from_box_to_sphere{};
    f32 pen = 0.f;
    if (dist2 > 1.e-12f) {
        const f32 dist = std::sqrt(dist2);
        from_box_to_sphere = delta * (1.f / dist);
        pen = radius - dist;
        if (pen < 0.f) {
            return false;
        }
    } else {
        const math::Vec3 c = box.tight.center();
        const math::Vec3 he = box.tight.half_extents();
        const math::Vec3 local = sphere.position - c;
        const f32 ex = he.x - std::abs(local.x);
        const f32 ey = he.y - std::abs(local.y);
        const f32 ez = he.z - std::abs(local.z);
        if (ey <= ex && ey <= ez) {
            from_box_to_sphere = {0.f, (local.y >= 0.f) ? 1.f : -1.f, 0.f};
            pen = radius + ey;
        } else if (ex <= ez) {
            from_box_to_sphere = {(local.x >= 0.f) ? 1.f : -1.f, 0.f, 0.f};
            pen = radius + ex;
        } else {
            from_box_to_sphere = {0.f, 0.f, (local.z >= 0.f) ? 1.f : -1.f};
            pen = radius + ez;
        }
    }

    // Normal is from A to B.
    out.normal = sphere_is_a ? -from_box_to_sphere : from_box_to_sphere;
    out.penetration = pen;
    return true;
}

StoredBody sphere_at(const StoredBody& src, math::Vec3 position) {
    StoredBody out = src;
    out.position = position;
    out.shape.type = ShapeType::Sphere;
    return out;
}

bool contact_capsule_sphere(const StoredBody& cap, const StoredBody& sph, bool cap_is_a,
    Contact& out) {
    math::Vec3 a{};
    math::Vec3 b{};
    capsule_ends(cap.shape, cap.position, a, b);
    const StoredBody at = sphere_at(cap, closest_on_segment(sph.position, a, b));
    return cap_is_a ? contact_sphere_sphere(at, sph, out) : contact_sphere_sphere(sph, at, out);
}

bool contact_capsule_capsule(const StoredBody& a, const StoredBody& b, Contact& out) {
    math::Vec3 a0{};
    math::Vec3 a1{};
    math::Vec3 b0{};
    math::Vec3 b1{};
    capsule_ends(a.shape, a.position, a0, a1);
    capsule_ends(b.shape, b.position, b0, b1);
    math::Vec3 ca{};
    math::Vec3 cb{};
    closest_pts_segments(a0, a1, b0, b1, ca, cb);
    return contact_sphere_sphere(sphere_at(a, ca), sphere_at(b, cb), out);
}

bool contact_capsule_aabb(const StoredBody& cap, const StoredBody& box, bool cap_is_a,
    Contact& out) {
    math::Vec3 a{};
    math::Vec3 b{};
    capsule_ends(cap.shape, cap.position, a, b);
    math::Vec3 on_seg{};
    math::Vec3 on_box{};
    closest_segment_aabb(a, b, box.tight, on_seg, on_box);
    return contact_sphere_aabb(sphere_at(cap, on_seg), box, cap_is_a, out);
}

bool generate_contact(const StoredBody& a, const StoredBody& b, Contact& out) {
    const bool a_cap = a.shape.type == ShapeType::Capsule;
    const bool b_cap = b.shape.type == ShapeType::Capsule;
    const bool a_sphere = a.shape.type == ShapeType::Sphere;
    const bool b_sphere = b.shape.type == ShapeType::Sphere;
    if (a_cap && b_cap) {
        return contact_capsule_capsule(a, b, out);
    }
    if (a_cap && b_sphere) {
        return contact_capsule_sphere(a, b, true, out);
    }
    if (a_sphere && b_cap) {
        return contact_capsule_sphere(b, a, false, out);
    }
    if (a_cap) {
        return contact_capsule_aabb(a, b, true, out);
    }
    if (b_cap) {
        return contact_capsule_aabb(b, a, false, out);
    }
    if (a_sphere && b_sphere) {
        return contact_sphere_sphere(a, b, out);
    }
    if (a_sphere) {
        return contact_sphere_aabb(a, b, true, out);
    }
    if (b_sphere) {
        return contact_sphere_aabb(b, a, false, out);
    }
    return contact_aabb_aabb(a, b, out);
}

class CpuPhysics final : public IPhysics {
public:
    CpuPhysics() {
        free_node_ = 0;
        for (u32 i = 0; i < kMaxNodes; ++i) {
            nodes_[i].height = -1;
            nodes_[i].child1 = i + 1;
            nodes_[i].parent = kNull;
            nodes_[i].child2 = kNull;
            nodes_[i].body = 0;
        }
        nodes_[kMaxNodes - 1].child1 = kNull;
        log(LogLevel::Info, LogChannel::Physics, "CPU physics ready");
    }

    BodyHandle create_body(const BodyDesc& desc) override {
        u32 slot = 0;
        for (u32 i = 1; i <= kMaxBodies; ++i) {
            if (!bodies_[i].live) {
                slot = i;
                break;
            }
        }
        if (slot == 0) {
            log(LogLevel::Error, LogChannel::Physics, "Physics: body pool full");
            return {};
        }

        const u32 leaf = alloc_node();
        if (leaf == kNull) {
            log(LogLevel::Error, LogChannel::Physics, "Physics: tree node pool full");
            return {};
        }

        StoredBody& body = bodies_[slot];
        body.shape = desc.shape;
        body.position = desc.position;
        body.velocity = {};
        body.layer = desc.layer;
        body.mask = desc.mask;
        body.sensor = desc.sensor;
        body.user_data = desc.user_data;
        body.motion = desc.motion;
        body.inv_mass = inv_mass_of(desc.motion, desc.mass);
        body.restitution = std::max(desc.restitution, 0.f);
        body.friction = std::max(desc.friction, 0.f);
        body.generation = body.generation == 0 ? 1 : body.generation + 1;
        body.tight = tight_bounds(body.shape, body.position);
        body.fat = body.tight.expanded(kFatMargin);
        body.leaf = leaf;
        body.live = true;
        ++live_count_;

        Node& node = nodes_[leaf];
        node.bounds = body.fat;
        node.parent = kNull;
        node.child1 = kNull;
        node.child2 = kNull;
        node.height = 0;
        node.body = slot;
        insert_leaf(leaf);

        return {slot, body.generation};
    }

    void destroy_body(BodyHandle handle) override {
        StoredBody* body = get(handle);
        if (!body) {
            return;
        }
        remove_leaf(body->leaf);
        free_node(body->leaf);
        body->leaf = kNull;
        body->live = false;
        body->velocity = {};
        body->generation += 1;
        --live_count_;
    }

    bool set_position(BodyHandle handle, math::Vec3 position) override {
        StoredBody* body = get(handle);
        if (!body) {
            return false;
        }
        body->position = position;
        sync_bounds(*body);
        return true;
    }

    bool set_linear_velocity(BodyHandle handle, math::Vec3 velocity) override {
        StoredBody* body = get(handle);
        if (!body || body->motion == MotionType::Static) {
            return false;
        }
        body->velocity = velocity;
        return true;
    }

    math::Vec3 position(BodyHandle handle) const override {
        const StoredBody* body = get(handle);
        return body ? body->position : math::Vec3{};
    }

    math::Vec3 linear_velocity(BodyHandle handle) const override {
        const StoredBody* body = get(handle);
        return body ? body->velocity : math::Vec3{};
    }

    void set_gravity(math::Vec3 gravity) override { gravity_ = gravity; }
    math::Vec3 gravity() const override { return gravity_; }

    void step(f32 dt) override {
        if (dt <= 0.f) {
            return;
        }

        const f32 damp = 1.f / (1.f + kLinearDamping * dt);
        for (u32 i = 1; i <= kMaxBodies; ++i) {
            StoredBody& body = bodies_[i];
            if (!body.live) {
                continue;
            }
            if (body.motion == MotionType::Dynamic) {
                body.velocity += gravity_ * dt;
                body.velocity = body.velocity * damp;
            }
        }

        collect_contacts();
        for (u32 iter = 0; iter < kVelocityIterations; ++iter) {
            for (u32 c = 0; c < contact_count_; ++c) {
                solve_velocity(contacts_[c]);
            }
        }

        for (u32 i = 1; i <= kMaxBodies; ++i) {
            StoredBody& body = bodies_[i];
            if (!body.live || body.motion == MotionType::Static) {
                continue;
            }
            body.position += body.velocity * dt;
            sync_bounds(body);
        }

        collect_contacts();
        for (u32 iter = 0; iter < kPositionIterations; ++iter) {
            for (u32 c = 0; c < contact_count_; ++c) {
                solve_position(contacts_[c]);
            }
        }
        for (u32 i = 1; i <= kMaxBodies; ++i) {
            StoredBody& body = bodies_[i];
            if (body.live && body.motion != MotionType::Static) {
                sync_bounds(body);
            }
        }
        collect_triggers();
    }

    u32 overlap_aabb(const math::Aabb& box, u32 mask, std::span<BodyHandle> out) const override {
        u32 count = 0;
        query(box, mask, out, count, [&](const StoredBody& body) {
            return shapes_overlap_aabb(body.shape, body.position, box);
        });
        return count;
    }

    u32 overlap_sphere(math::Vec3 center, f32 radius, u32 mask,
        std::span<BodyHandle> out) const override {
        const f32 r = std::max(radius, 0.f);
        const math::Aabb box = math::Aabb::from_center_half(center, {r, r, r});
        u32 count = 0;
        query(box, mask, out, count, [&](const StoredBody& body) {
            return shapes_overlap_sphere(body.shape, body.position, center, r);
        });
        return count;
    }

    u32 overlap_capsule(math::Vec3 position, f32 radius, f32 half_height, u32 mask,
        std::span<BodyHandle> out) const override {
        const f32 r = std::max(radius, 0.f);
        const f32 h = std::max(half_height, 0.f);
        const math::Aabb box = math::Aabb::from_center_half(position, {r, h + r, r});
        u32 count = 0;
        query(box, mask, out, count, [&](const StoredBody& body) {
            return shapes_overlap_capsule(body.shape, body.position, position, r, h);
        });
        return count;
    }

    u32 trigger_events(std::span<TriggerEvent> out) const override {
        const u32 n = std::min(static_cast<u32>(out.size()), event_count_);
        for (u32 i = 0; i < n; ++i) {
            out[i] = events_[i];
        }
        return n;
    }

    bool raycast(math::Vec3 origin, math::Vec3 direction, f32 max_distance, u32 mask,
        RaycastHit& out, BodyHandle ignore) const override {
        const f32 dir_len = direction.length();
        if (root_ == kNull || max_distance <= 0.f || dir_len <= kRayParallel) {
            return false;
        }
        const math::Vec3 d = direction * (1.f / dir_len);
        f32 max_t = max_distance;
        bool hit = false;
        RaycastHit best{};

        std::array<u32, 256> stack{};
        u32 sp = 0;
        stack[sp++] = root_;
        while (sp > 0) {
            const u32 index = stack[--sp];
            const Node& node = nodes_[index];
            if (!ray_hits_aabb_bounds(origin, d, max_t, node.bounds)) {
                continue;
            }
            if (is_leaf(node)) {
                const StoredBody& body = bodies_[node.body];
                if (!body.live || (body.layer & mask) == 0) {
                    continue;
                }
                if (ignore.valid() && ignore.id == node.body && ignore.generation == body.generation) {
                    continue;
                }
                f32 t = 0.f;
                math::Vec3 n{};
                if (!ray_shape_hit(body.shape, body.position, body.tight, origin, d, max_t, t, n)) {
                    continue;
                }
                max_t = t;
                best.body = {node.body, body.generation};
                best.point = origin + d * t;
                best.normal = n;
                best.fraction = max_distance > 0.f ? t / max_distance : 0.f;
                hit = true;
                continue;
            }
            if (sp + 2 <= stack.size()) {
                stack[sp++] = node.child1;
                stack[sp++] = node.child2;
            }
        }

        if (hit) {
            out = best;
        }
        return hit;
    }

    u32 body_count() const override { return live_count_; }
    std::string_view name() const override { return "cpu"; }

private:
    const StoredBody* get(BodyHandle handle) const {
        if (!handle.valid() || handle.id > kMaxBodies) {
            return nullptr;
        }
        const StoredBody& body = bodies_[handle.id];
        if (!body.live || body.generation != handle.generation) {
            return nullptr;
        }
        return &body;
    }

    StoredBody* get(BodyHandle handle) {
        return const_cast<StoredBody*>(static_cast<const CpuPhysics*>(this)->get(handle));
    }

    void sync_bounds(StoredBody& body) {
        body.tight = tight_bounds(body.shape, body.position);
        if (body.fat.contains(body.tight)) {
            return;
        }
        remove_leaf(body.leaf);
        body.fat = body.tight.expanded(kFatMargin);
        nodes_[body.leaf].bounds = body.fat;
        nodes_[body.leaf].parent = kNull;
        nodes_[body.leaf].child1 = kNull;
        nodes_[body.leaf].child2 = kNull;
        nodes_[body.leaf].height = 0;
        insert_leaf(body.leaf);
    }

    template <typename Narrow>
    void query(const math::Aabb& box, u32 mask, std::span<BodyHandle> out, u32& count,
        Narrow&& narrow) const {
        if (root_ == kNull || out.empty()) {
            return;
        }
        std::array<u32, 256> stack{};
        u32 sp = 0;
        stack[sp++] = root_;
        while (sp > 0 && count < out.size()) {
            const u32 index = stack[--sp];
            const Node& node = nodes_[index];
            if (!node.bounds.overlaps(box)) {
                continue;
            }
            if (is_leaf(node)) {
                const StoredBody& body = bodies_[node.body];
                if (!body.live || (body.layer & mask) == 0) {
                    continue;
                }
                if (!narrow(body)) {
                    continue;
                }
                out[count++] = {node.body, body.generation};
                continue;
            }
            if (sp + 2 <= stack.size()) {
                stack[sp++] = node.child1;
                stack[sp++] = node.child2;
            }
        }
    }

    void query_ids(const math::Aabb& box, std::span<u32> out, u32& count) const {
        if (root_ == kNull || out.empty()) {
            return;
        }
        std::array<u32, 256> stack{};
        u32 sp = 0;
        stack[sp++] = root_;
        while (sp > 0 && count < out.size()) {
            const u32 index = stack[--sp];
            const Node& node = nodes_[index];
            if (!node.bounds.overlaps(box)) {
                continue;
            }
            if (is_leaf(node)) {
                if (bodies_[node.body].live) {
                    out[count++] = node.body;
                }
                continue;
            }
            if (sp + 2 <= stack.size()) {
                stack[sp++] = node.child1;
                stack[sp++] = node.child2;
            }
        }
    }

    void collect_contacts() {
        contact_count_ = 0;
        std::array<u32, kMaxBodies> hits{};
        for (u32 i = 1; i <= kMaxBodies; ++i) {
            StoredBody& a = bodies_[i];
            if (!a.live || a.motion == MotionType::Static || a.sensor) {
                continue;
            }
            u32 hit_count = 0;
            query_ids(a.fat, hits, hit_count);
            for (u32 h = 0; h < hit_count && contact_count_ < kMaxContacts; ++h) {
                const u32 j = hits[h];
                if (j == i) {
                    continue;
                }
                StoredBody& b = bodies_[j];
                if (!b.live || b.sensor) {
                    continue;
                }
                if (b.motion != MotionType::Static && j < i) {
                    continue;
                }
                if ((a.layer & b.mask) == 0 || (b.layer & a.mask) == 0) {
                    continue;
                }
                if (a.inv_mass == 0.f && b.inv_mass == 0.f) {
                    continue;
                }
                Contact contact{};
                if (!generate_contact(a, b, contact)) {
                    continue;
                }
                contact.a = i;
                contact.b = j;
                contact.inv_mass_sum = a.inv_mass + b.inv_mass;
                if (contact.inv_mass_sum <= 0.f) {
                    continue;
                }
                contact.restitution = std::min(a.restitution, b.restitution);
                contact.friction = std::sqrt(std::max(a.friction * b.friction, 0.f));
                const math::Vec3 rel = b.velocity - a.velocity;
                contact.vn0 = rel.dot(contact.normal);
                contacts_[contact_count_++] = contact;
            }
        }
    }

    struct TriggerPair {
        BodyHandle a{};
        BodyHandle b{};
    };

    static bool pair_less(const TriggerPair& x, const TriggerPair& y) {
        if (x.a.id != y.a.id) {
            return x.a.id < y.a.id;
        }
        if (x.a.generation != y.a.generation) {
            return x.a.generation < y.a.generation;
        }
        if (x.b.id != y.b.id) {
            return x.b.id < y.b.id;
        }
        return x.b.generation < y.b.generation;
    }

    static bool pair_equal(const TriggerPair& x, const TriggerPair& y) {
        return x.a == y.a && x.b == y.b;
    }

    TriggerPair make_trigger_pair(u32 i, u32 j) const {
        const u32 lo = i < j ? i : j;
        const u32 hi = i < j ? j : i;
        return {{lo, bodies_[lo].generation}, {hi, bodies_[hi].generation}};
    }

    void add_event(TriggerEventType type, const TriggerPair& pair) {
        if (event_count_ >= kMaxTriggerEvents) {
            return;
        }
        events_[event_count_++] = {pair.a, pair.b, type};
    }

    void collect_triggers() {
        event_count_ = 0;
        u32 curr_count = 0;
        std::array<u32, kMaxBodies> hits{};
        for (u32 i = 1; i <= kMaxBodies; ++i) {
            StoredBody& a = bodies_[i];
            if (!a.live || a.motion == MotionType::Static) {
                continue;
            }
            u32 hit_count = 0;
            query_ids(a.fat, hits, hit_count);
            for (u32 h = 0; h < hit_count && curr_count < kMaxTriggerPairs; ++h) {
                const u32 j = hits[h];
                if (j == i) {
                    continue;
                }
                StoredBody& b = bodies_[j];
                if (!b.live || (!a.sensor && !b.sensor)) {
                    continue;
                }
                if (b.motion != MotionType::Static && j < i) {
                    continue;
                }
                if ((a.layer & b.mask) == 0 || (b.layer & a.mask) == 0) {
                    continue;
                }
                if (!shapes_overlap(a.shape, a.position, b.shape, b.position)) {
                    continue;
                }
                curr_pairs_[curr_count++] = make_trigger_pair(i, j);
            }
        }

        std::sort(curr_pairs_.begin(), curr_pairs_.begin() + curr_count, pair_less);
        u32 unique = 0;
        for (u32 i = 0; i < curr_count; ++i) {
            if (unique == 0 || !pair_equal(curr_pairs_[unique - 1], curr_pairs_[i])) {
                curr_pairs_[unique++] = curr_pairs_[i];
            }
        }
        curr_count = unique;

        u32 ia = 0;
        u32 ib = 0;
        while (ia < prev_count_ || ib < curr_count) {
            if (ia < prev_count_ && ib < curr_count && pair_equal(prev_pairs_[ia], curr_pairs_[ib])) {
                ++ia;
                ++ib;
                continue;
            }
            if (ib >= curr_count || (ia < prev_count_ && pair_less(prev_pairs_[ia], curr_pairs_[ib]))) {
                add_event(TriggerEventType::Exit, prev_pairs_[ia]);
                ++ia;
                continue;
            }
            add_event(TriggerEventType::Enter, curr_pairs_[ib]);
            ++ib;
        }

        prev_pairs_ = curr_pairs_;
        prev_count_ = curr_count;
    }

    void solve_velocity(Contact& contact) {
        StoredBody& a = bodies_[contact.a];
        StoredBody& b = bodies_[contact.b];
        const math::Vec3 n = contact.normal;
        const math::Vec3 rel = b.velocity - a.velocity;
        f32 vn = rel.dot(n);

        f32 e = contact.restitution;
        if (contact.vn0 > -kBounceThreshold) {
            e = 0.f;
        }
        const f32 want = vn + e * std::min(contact.vn0, 0.f);
        f32 lambda = -want / contact.inv_mass_sum;
        const f32 acc_old = contact.acc_n;
        contact.acc_n = std::max(acc_old + lambda, 0.f);
        lambda = contact.acc_n - acc_old;
        const math::Vec3 impulse = n * lambda;
        a.velocity -= impulse * a.inv_mass;
        b.velocity += impulse * b.inv_mass;

        const math::Vec3 rel_t = b.velocity - a.velocity;
        const math::Vec3 tangent_vec = rel_t - n * rel_t.dot(n);
        const f32 vt2 = tangent_vec.dot(tangent_vec);
        if (vt2 < kFrictionEpsilon) {
            return;
        }
        const math::Vec3 t = tangent_vec * (1.f / std::sqrt(vt2));
        f32 lambda_t = -rel_t.dot(t) / contact.inv_mass_sum;
        const f32 max_t = contact.friction * contact.acc_n;
        const f32 acc_t_old = contact.acc_t;
        contact.acc_t = std::clamp(acc_t_old + lambda_t, -max_t, max_t);
        lambda_t = contact.acc_t - acc_t_old;
        const math::Vec3 friction = t * lambda_t;
        a.velocity -= friction * a.inv_mass;
        b.velocity += friction * b.inv_mass;
    }

    void solve_position(Contact& contact) {
        StoredBody& a = bodies_[contact.a];
        StoredBody& b = bodies_[contact.b];
        Contact fresh{};
        if (!generate_contact(a, b, fresh)) {
            return;
        }
        const f32 mag = std::max(fresh.penetration - kSlop, 0.f) * kBaumgarte;
        if (mag <= 0.f || contact.inv_mass_sum <= 0.f) {
            return;
        }
        const math::Vec3 corr = fresh.normal * (mag / contact.inv_mass_sum);
        a.position -= corr * a.inv_mass;
        b.position += corr * b.inv_mass;
        a.tight = tight_bounds(a.shape, a.position);
        b.tight = tight_bounds(b.shape, b.position);
    }

    u32 alloc_node() {
        if (free_node_ == kNull) {
            return kNull;
        }
        const u32 index = free_node_;
        free_node_ = nodes_[index].child1;
        nodes_[index] = Node{};
        nodes_[index].parent = kNull;
        nodes_[index].child1 = kNull;
        nodes_[index].child2 = kNull;
        nodes_[index].height = -1;
        return index;
    }

    void free_node(u32 index) {
        nodes_[index] = Node{};
        nodes_[index].height = -1;
        nodes_[index].child1 = free_node_;
        free_node_ = index;
    }

    void insert_leaf(u32 leaf) {
        if (root_ == kNull) {
            root_ = leaf;
            nodes_[leaf].parent = kNull;
            return;
        }

        const math::Aabb leaf_box = nodes_[leaf].bounds;
        u32 index = root_;
        while (!is_leaf(nodes_[index])) {
            const u32 child1 = nodes_[index].child1;
            const u32 child2 = nodes_[index].child2;
            const f32 area = nodes_[index].bounds.surface_area();
            const math::Aabb combined = nodes_[index].bounds.united(leaf_box);
            const f32 combined_area = combined.surface_area();
            const f32 cost = 2.f * combined_area;
            const f32 inherit = 2.f * (combined_area - area);

            auto descent_cost = [&](u32 child) {
                const math::Aabb box = leaf_box.united(nodes_[child].bounds);
                if (is_leaf(nodes_[child])) {
                    return box.surface_area() + inherit;
                }
                const f32 old_area = nodes_[child].bounds.surface_area();
                return (box.surface_area() - old_area) + inherit;
            };
            const f32 cost1 = descent_cost(child1);
            const f32 cost2 = descent_cost(child2);
            if (cost < cost1 && cost < cost2) {
                break;
            }
            index = cost1 < cost2 ? child1 : child2;
        }

        const u32 sibling = index;
        const u32 old_parent = nodes_[sibling].parent;
        const u32 new_parent = alloc_node();
        Node& parent = nodes_[new_parent];
        parent.parent = old_parent;
        parent.bounds = leaf_box.united(nodes_[sibling].bounds);
        parent.height = nodes_[sibling].height + 1;
        parent.child1 = sibling;
        parent.child2 = leaf;
        parent.body = 0;
        nodes_[sibling].parent = new_parent;
        nodes_[leaf].parent = new_parent;

        if (old_parent != kNull) {
            if (nodes_[old_parent].child1 == sibling) {
                nodes_[old_parent].child1 = new_parent;
            } else {
                nodes_[old_parent].child2 = new_parent;
            }
        } else {
            root_ = new_parent;
        }

        u32 walk = nodes_[leaf].parent;
        while (walk != kNull) {
            walk = balance(walk);
            const u32 c1 = nodes_[walk].child1;
            const u32 c2 = nodes_[walk].child2;
            nodes_[walk].bounds = nodes_[c1].bounds.united(nodes_[c2].bounds);
            nodes_[walk].height = 1 + std::max(nodes_[c1].height, nodes_[c2].height);
            walk = nodes_[walk].parent;
        }
    }

    void remove_leaf(u32 leaf) {
        if (leaf == root_) {
            root_ = kNull;
            return;
        }
        const u32 parent = nodes_[leaf].parent;
        const u32 grand = nodes_[parent].parent;
        const u32 sibling = nodes_[parent].child1 == leaf ? nodes_[parent].child2
                                                          : nodes_[parent].child1;
        if (grand != kNull) {
            if (nodes_[grand].child1 == parent) {
                nodes_[grand].child1 = sibling;
            } else {
                nodes_[grand].child2 = sibling;
            }
            nodes_[sibling].parent = grand;
            free_node(parent);
            u32 walk = grand;
            while (walk != kNull) {
                walk = balance(walk);
                const u32 c1 = nodes_[walk].child1;
                const u32 c2 = nodes_[walk].child2;
                nodes_[walk].bounds = nodes_[c1].bounds.united(nodes_[c2].bounds);
                nodes_[walk].height = 1 + std::max(nodes_[c1].height, nodes_[c2].height);
                walk = nodes_[walk].parent;
            }
        } else {
            root_ = sibling;
            nodes_[sibling].parent = kNull;
            free_node(parent);
        }
    }

    u32 balance(u32 iA) {
        Node& A = nodes_[iA];
        if (is_leaf(A) || A.height < 2) {
            return iA;
        }
        const u32 iB = A.child1;
        const u32 iC = A.child2;
        const i32 bal = nodes_[iC].height - nodes_[iB].height;

        if (bal > 1) {
            const u32 iF = nodes_[iC].child1;
            const u32 iG = nodes_[iC].child2;
            nodes_[iC].child1 = iA;
            nodes_[iC].parent = A.parent;
            A.parent = iC;
            if (nodes_[iC].parent != kNull) {
                if (nodes_[nodes_[iC].parent].child1 == iA) {
                    nodes_[nodes_[iC].parent].child1 = iC;
                } else {
                    nodes_[nodes_[iC].parent].child2 = iC;
                }
            } else {
                root_ = iC;
            }
            if (nodes_[iF].height > nodes_[iG].height) {
                nodes_[iC].child2 = iF;
                A.child2 = iG;
                nodes_[iG].parent = iA;
                A.bounds = nodes_[iB].bounds.united(nodes_[iG].bounds);
                nodes_[iC].bounds = A.bounds.united(nodes_[iF].bounds);
                A.height = 1 + std::max(nodes_[iB].height, nodes_[iG].height);
                nodes_[iC].height = 1 + std::max(A.height, nodes_[iF].height);
            } else {
                nodes_[iC].child2 = iG;
                A.child2 = iF;
                nodes_[iF].parent = iA;
                A.bounds = nodes_[iB].bounds.united(nodes_[iF].bounds);
                nodes_[iC].bounds = A.bounds.united(nodes_[iG].bounds);
                A.height = 1 + std::max(nodes_[iB].height, nodes_[iF].height);
                nodes_[iC].height = 1 + std::max(A.height, nodes_[iG].height);
            }
            return iC;
        }

        if (bal < -1) {
            const u32 iD = nodes_[iB].child1;
            const u32 iE = nodes_[iB].child2;
            nodes_[iB].child1 = iA;
            nodes_[iB].parent = A.parent;
            A.parent = iB;
            if (nodes_[iB].parent != kNull) {
                if (nodes_[nodes_[iB].parent].child1 == iA) {
                    nodes_[nodes_[iB].parent].child1 = iB;
                } else {
                    nodes_[nodes_[iB].parent].child2 = iB;
                }
            } else {
                root_ = iB;
            }
            if (nodes_[iD].height > nodes_[iE].height) {
                nodes_[iB].child2 = iD;
                A.child1 = iE;
                nodes_[iE].parent = iA;
                A.bounds = nodes_[iC].bounds.united(nodes_[iE].bounds);
                nodes_[iB].bounds = A.bounds.united(nodes_[iD].bounds);
                A.height = 1 + std::max(nodes_[iC].height, nodes_[iE].height);
                nodes_[iB].height = 1 + std::max(A.height, nodes_[iD].height);
            } else {
                nodes_[iB].child2 = iE;
                A.child1 = iD;
                nodes_[iD].parent = iA;
                A.bounds = nodes_[iC].bounds.united(nodes_[iD].bounds);
                nodes_[iB].bounds = A.bounds.united(nodes_[iE].bounds);
                A.height = 1 + std::max(nodes_[iC].height, nodes_[iD].height);
                nodes_[iB].height = 1 + std::max(A.height, nodes_[iE].height);
            }
            return iB;
        }
        return iA;
    }

    std::array<StoredBody, kMaxBodies + 1> bodies_{};
    std::array<Node, kMaxNodes> nodes_{};
    std::array<Contact, kMaxContacts> contacts_{};
    std::array<TriggerPair, kMaxTriggerPairs> prev_pairs_{};
    std::array<TriggerPair, kMaxTriggerPairs> curr_pairs_{};
    std::array<TriggerEvent, kMaxTriggerEvents> events_{};
    u32 contact_count_ = 0;
    u32 prev_count_ = 0;
    u32 event_count_ = 0;
    u32 root_ = kNull;
    u32 free_node_ = 0;
    u32 live_count_ = 0;
    math::Vec3 gravity_ = kDefaultGravity;
};

} // namespace

std::unique_ptr<IPhysics> create_physics() {
    return std::make_unique<CpuPhysics>();
}

} // namespace engine::physics::cpu
