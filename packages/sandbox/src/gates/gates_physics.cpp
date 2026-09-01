#include "../sandbox_common.hpp"

// Physics and character gates.
//
// Moved out of main.cpp, which held all 72 and was 26% of the engine
// (analizeMax A4). What a gate *is* has not changed - see CLAUDE.md. Helpers
// private to these gates are `static` here; only what main.cpp also uses lives
// in sandbox_common.

namespace sandbox {

bool run_physics_gate(engine::physics::IPhysics* physics) {
    using engine::physics::BodyDesc;
    using engine::physics::BodyHandle;
    using engine::physics::ShapeType;
    using engine::physics::kAllLayers;

    const bool backend_ok = physics != nullptr && physics->name() == "cpu";
    bool aabb_ok = false;
    bool sphere_ok = false;
    bool mask_ok = false;
    bool move_ok = false;
    bool gen_ok = false;

    if (physics) {
        BodyDesc a{};
        a.shape.type = ShapeType::Aabb;
        a.shape.half_extents = {0.5f, 0.5f, 0.5f};
        a.position = {0.f, 0.f, 0.f};
        a.layer = 1u;

        BodyDesc b = a;
        b.position = {10.f, 0.f, 0.f};
        b.layer = 2u;

        const BodyHandle ha = physics->create_body(a);
        const BodyHandle hb = physics->create_body(b);
        BodyHandle hits[8]{};

        const auto near_origin = engine::math::Aabb::from_center_half({0.f, 0.f, 0.f},
            {1.f, 1.f, 1.f});
        const engine::u32 n_aabb = physics->overlap_aabb(near_origin, kAllLayers, hits);
        aabb_ok = ha.valid() && n_aabb == 1 && hits[0] == ha;

        const engine::u32 n_sphere = physics->overlap_sphere({0.f, 0.f, 0.f}, 0.75f, kAllLayers,
            hits);
        sphere_ok = n_sphere == 1 && hits[0] == ha;

        const engine::u32 n_mask_miss = physics->overlap_aabb(near_origin, 2u, hits);
        const engine::u32 n_mask_hit = physics->overlap_aabb(near_origin, 1u, hits);
        mask_ok = n_mask_miss == 0 && n_mask_hit == 1 && hits[0] == ha;

        physics->set_position(hb, {0.25f, 0.f, 0.f});
        const engine::u32 n_move = physics->overlap_aabb(near_origin, kAllLayers, hits);
        move_ok = n_move == 2;

        physics->destroy_body(hb);
        const engine::u32 n_after_destroy = physics->overlap_aabb(near_origin, kAllLayers, hits);
        const BodyHandle hb2 = physics->create_body(b);
        const auto far_box = engine::math::Aabb::from_center_half({10.f, 0.f, 0.f},
            {1.f, 1.f, 1.f});
        const engine::u32 n_far = physics->overlap_aabb(far_box, kAllLayers, hits);
        gen_ok = n_after_destroy == 1 && hb2.valid() && hb2 != hb && n_far == 1 && hits[0] == hb2;

        physics->destroy_body(ha);
        physics->destroy_body(hb2);
    }

    const bool passed = backend_ok && aabb_ok && sphere_ok && mask_ok && move_ok && gen_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Physics gate: aabb=%s sphere=%s mask=%s move=%s gen=%s backend=cpu (%s)",
        aabb_ok ? "yes" : "no", sphere_ok ? "yes" : "no", mask_ok ? "yes" : "no",
        move_ok ? "yes" : "no", gen_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Physics, message);
    return passed;
}

bool run_physics_body_gate(engine::physics::IPhysics* physics) {
    using engine::physics::BodyDesc;
    using engine::physics::MotionType;
    using engine::physics::ShapeType;

    bool gravity_ok = false;
    bool floor_ok = false;
    bool sphere_ok = false;
    bool box_ok = false;
    bool rest_ok = false;

    if (physics) {
        physics->set_gravity({0.f, -9.81f, 0.f});
        const engine::f32 dt = 1.f / 60.f;

        BodyDesc falling{};
        falling.shape.type = ShapeType::Sphere;
        falling.shape.radius = 0.25f;
        falling.position = {0.f, 10.f, 0.f};
        falling.motion = MotionType::Dynamic;
        falling.mass = 1.f;
        falling.restitution = 0.f;
        const auto drop = physics->create_body(falling);
        for (int i = 0; i < 60; ++i) {
            physics->step(dt);
        }
        const engine::f32 y_drop = physics->position(drop).y;
        gravity_ok = drop.valid() && y_drop > 4.5f && y_drop < 5.6f
            && physics->linear_velocity(drop).y < -8.f;
        physics->destroy_body(drop);

        BodyDesc floor{};
        floor.shape.type = ShapeType::Aabb;
        floor.shape.half_extents = {4.f, 0.5f, 4.f};
        floor.position = {0.f, -0.5f, 0.f};
        floor.motion = MotionType::Static;
        const auto floor_h = physics->create_body(floor);

        BodyDesc ball = falling;
        ball.position = {0.f, 3.f, 0.f};
        ball.shape.radius = 0.5f;
        const auto ball_h = physics->create_body(ball);

        BodyDesc box{};
        box.shape.type = ShapeType::Aabb;
        box.shape.half_extents = {0.5f, 0.5f, 0.5f};
        box.position = {2.f, 3.f, 0.f};
        box.motion = MotionType::Dynamic;
        box.mass = 1.f;
        box.restitution = 0.f;
        const auto box_h = physics->create_body(box);

        for (int i = 0; i < 180; ++i) {
            physics->step(dt);
        }

        const engine::math::Vec3 floor_p = physics->position(floor_h);
        floor_ok = floor_h.valid() && std::abs(floor_p.y + 0.5f) < 0.001f
            && std::abs(physics->linear_velocity(floor_h).y) < 0.001f;

        const engine::f32 ball_y = physics->position(ball_h).y;
        const engine::f32 ball_vy = physics->linear_velocity(ball_h).y;
        sphere_ok = ball_y > 0.48f && ball_y < 0.58f && std::abs(ball_vy) < 0.25f;

        const engine::f32 box_y = physics->position(box_h).y;
        const engine::f32 box_vy = physics->linear_velocity(box_h).y;
        box_ok = box_y > 0.48f && box_y < 0.58f && std::abs(box_vy) < 0.25f;
        rest_ok = sphere_ok && box_ok;

        physics->destroy_body(ball_h);
        physics->destroy_body(box_h);
        physics->destroy_body(floor_h);
        physics->set_gravity(engine::physics::kDefaultGravity);
    }

    const bool passed = gravity_ok && floor_ok && sphere_ok && box_ok && rest_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Physics body gate: gravity=%s rest=%s floor=%s sphere=%s box=%s (%s)",
        gravity_ok ? "yes" : "no", rest_ok ? "yes" : "no", floor_ok ? "yes" : "no",
        sphere_ok ? "yes" : "no", box_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Physics, message);
    return passed;
}

bool run_physics_capsule_gate(engine::physics::IPhysics* physics) {
    using engine::physics::BodyDesc;
    using engine::physics::BodyHandle;
    using engine::physics::MotionType;
    using engine::physics::ShapeType;
    using engine::physics::kAllLayers;

    bool overlap_ok = false;
    bool rest_ok = false;
    bool floor_ok = false;
    bool not_aabb_ok = false;

    if (physics) {
        physics->set_gravity({0.f, -9.81f, 0.f});
        const engine::f32 dt = 1.f / 60.f;
        const engine::f32 radius = 0.3f;
        const engine::f32 half_height = 0.5f;

        BodyDesc cube{};
        cube.shape.type = ShapeType::Aabb;
        cube.shape.half_extents = {0.5f, 0.5f, 0.5f};
        cube.position = {0.f, 0.f, 0.f};
        cube.motion = MotionType::Static;
        const auto cube_h = physics->create_body(cube);

        BodyHandle hits[8]{};
        const engine::u32 n_hit = physics->overlap_capsule({0.55f, 0.f, 0.f}, radius, half_height,
            kAllLayers, hits);
        const bool hit_face = n_hit == 1 && hits[0] == cube_h;
        const engine::u32 n_corner = physics->overlap_capsule({0.75f, 0.f, 0.75f}, radius,
            half_height, kAllLayers, hits);
        const engine::u32 n_far = physics->overlap_capsule({10.f, 0.f, 0.f}, radius, half_height,
            kAllLayers, hits);
        overlap_ok = cube_h.valid() && hit_face && n_far == 0;
        not_aabb_ok = n_corner == 0;
        physics->destroy_body(cube_h);

        BodyDesc floor{};
        floor.shape.type = ShapeType::Aabb;
        floor.shape.half_extents = {4.f, 0.5f, 4.f};
        floor.position = {0.f, -0.5f, 0.f};
        floor.motion = MotionType::Static;
        const auto floor_h = physics->create_body(floor);

        BodyDesc cap{};
        cap.shape.type = ShapeType::Capsule;
        cap.shape.radius = radius;
        cap.shape.half_height = half_height;
        cap.position = {0.f, 3.f, 0.f};
        cap.motion = MotionType::Dynamic;
        cap.mass = 1.f;
        cap.restitution = 0.f;
        const auto cap_h = physics->create_body(cap);

        for (int i = 0; i < 180; ++i) {
            physics->step(dt);
        }

        const engine::math::Vec3 floor_p = physics->position(floor_h);
        floor_ok = floor_h.valid() && std::abs(floor_p.y + 0.5f) < 0.001f
            && std::abs(physics->linear_velocity(floor_h).y) < 0.001f;

        const engine::f32 y = physics->position(cap_h).y;
        const engine::f32 vy = physics->linear_velocity(cap_h).y;
        rest_ok = cap_h.valid() && y > 0.78f && y < 0.88f && std::abs(vy) < 0.25f;

        physics->destroy_body(cap_h);
        physics->destroy_body(floor_h);
        physics->set_gravity(engine::physics::kDefaultGravity);
    }

    const bool passed = overlap_ok && rest_ok && floor_ok && not_aabb_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Physics capsule gate: overlap=%s rest=%s floor=%s not_aabb=%s (%s)",
        overlap_ok ? "yes" : "no", rest_ok ? "yes" : "no", floor_ok ? "yes" : "no",
        not_aabb_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Physics, message);
    return passed;
}

bool run_physics_trigger_gate(engine::physics::IPhysics* physics) {
    using engine::physics::BodyDesc;
    using engine::physics::BodyHandle;
    using engine::physics::MotionType;
    using engine::physics::ShapeType;
    using engine::physics::TriggerEvent;
    using engine::physics::TriggerEventType;
    using engine::physics::kAllLayers;
    using engine::physics::kDefaultGravity;

    bool enter_ok = false;
    bool stay_ok = false;
    bool exit_ok = false;
    bool mask_ok = false;
    bool solid_ok = false;

    const auto involves = [](const TriggerEvent& e, BodyHandle x, BodyHandle y) {
        return (e.a == x && e.b == y) || (e.a == y && e.b == x);
    };

    if (physics) {
        physics->set_gravity({0.f, 0.f, 0.f});
        const engine::f32 dt = 1.f / 60.f;

        BodyDesc volume{};
        volume.shape.type = ShapeType::Aabb;
        volume.shape.half_extents = {1.f, 1.f, 1.f};
        volume.position = {0.f, 0.f, 0.f};
        volume.sensor = true;
        volume.motion = MotionType::Static;
        volume.layer = 1u;
        volume.mask = kAllLayers;
        const auto vol_h = physics->create_body(volume);

        BodyDesc guest{};
        guest.shape.type = ShapeType::Sphere;
        guest.shape.radius = 0.25f;
        guest.position = {0.f, 0.f, 0.f};
        guest.motion = MotionType::Kinematic;
        guest.layer = 2u;
        guest.mask = kAllLayers;
        const auto guest_h = physics->create_body(guest);

        physics->step(dt);
        TriggerEvent ev[8]{};
        engine::u32 n = physics->trigger_events(ev);
        enter_ok = vol_h.valid() && guest_h.valid() && n == 1
            && ev[0].type == TriggerEventType::Enter && involves(ev[0], vol_h, guest_h);

        physics->step(dt);
        n = physics->trigger_events(ev);
        stay_ok = n == 0;

        physics->set_position(guest_h, {10.f, 0.f, 0.f});
        physics->step(dt);
        n = physics->trigger_events(ev);
        exit_ok = n == 1 && ev[0].type == TriggerEventType::Exit && involves(ev[0], vol_h, guest_h);

        physics->destroy_body(guest_h);
        physics->destroy_body(vol_h);

        const auto vol2 = physics->create_body(volume);
        BodyDesc masked = guest;
        masked.mask = 2u;
        const auto masked_h = physics->create_body(masked);
        physics->step(dt);
        n = physics->trigger_events(ev);
        mask_ok = n == 0;
        physics->destroy_body(masked_h);
        physics->destroy_body(vol2);

        BodyDesc solid_a{};
        solid_a.shape.type = ShapeType::Aabb;
        solid_a.shape.half_extents = {0.5f, 0.5f, 0.5f};
        solid_a.position = {0.f, 0.f, 0.f};
        solid_a.motion = MotionType::Static;
        const auto sa = physics->create_body(solid_a);
        BodyDesc solid_b = solid_a;
        solid_b.motion = MotionType::Kinematic;
        const auto sb = physics->create_body(solid_b);
        physics->step(dt);
        n = physics->trigger_events(ev);
        solid_ok = n == 0;

        physics->destroy_body(sa);
        physics->destroy_body(sb);
        physics->set_gravity(kDefaultGravity);
    }

    const bool passed = enter_ok && stay_ok && exit_ok && mask_ok && solid_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Physics trigger gate: enter=%s stay=%s exit=%s mask=%s solid=%s (%s)",
        enter_ok ? "yes" : "no", stay_ok ? "yes" : "no", exit_ok ? "yes" : "no",
        mask_ok ? "yes" : "no", solid_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Physics, message);
    return passed;
}

bool run_physics_raycast_gate(engine::physics::IPhysics* physics) {
    using engine::physics::BodyDesc;
    using engine::physics::MotionType;
    using engine::physics::RaycastHit;
    using engine::physics::ShapeType;
    using engine::physics::kAllLayers;

    bool aabb_ok = false;
    bool sphere_ok = false;
    bool capsule_ok = false;
    bool closest_ok = false;
    bool mask_ok = false;
    bool miss_ok = false;

    if (physics) {
        const engine::math::Vec3 origin{0.f, 0.f, 0.f};
        const engine::math::Vec3 dir{1.f, 0.f, 0.f};
        const engine::f32 max_d = 10.f;

        BodyDesc box{};
        box.shape.type = ShapeType::Aabb;
        box.shape.half_extents = {0.5f, 0.5f, 0.5f};
        box.position = {2.f, 0.f, 0.f};
        box.motion = MotionType::Static;
        box.layer = 1u;
        const auto box_h = physics->create_body(box);

        RaycastHit hit{};
        aabb_ok = physics->raycast(origin, dir, max_d, kAllLayers, hit) && hit.body == box_h
            && std::abs(hit.point.x - 1.5f) < 0.01f && std::abs(hit.normal.x + 1.f) < 0.01f
            && std::abs(hit.fraction - 0.15f) < 0.005f;
        physics->destroy_body(box_h);

        BodyDesc ball{};
        ball.shape.type = ShapeType::Sphere;
        ball.shape.radius = 0.5f;
        ball.position = {2.f, 0.f, 0.f};
        ball.motion = MotionType::Static;
        const auto ball_h = physics->create_body(ball);
        sphere_ok = physics->raycast(origin, dir, max_d, kAllLayers, hit) && hit.body == ball_h
            && std::abs(hit.point.x - 1.5f) < 0.01f && std::abs(hit.normal.x + 1.f) < 0.01f;
        physics->destroy_body(ball_h);

        BodyDesc cap{};
        cap.shape.type = ShapeType::Capsule;
        cap.shape.radius = 0.3f;
        cap.shape.half_height = 0.5f;
        cap.position = {2.f, 0.f, 0.f};
        cap.motion = MotionType::Static;
        const auto cap_h = physics->create_body(cap);
        capsule_ok = physics->raycast(origin, dir, max_d, kAllLayers, hit) && hit.body == cap_h
            && std::abs(hit.point.x - 1.7f) < 0.01f && std::abs(hit.normal.x + 1.f) < 0.01f
            && std::abs(hit.point.y) < 0.01f;
        physics->destroy_body(cap_h);

        const auto near_h = physics->create_body(box);
        BodyDesc far = box;
        far.position = {5.f, 0.f, 0.f};
        const auto far_h = physics->create_body(far);
        closest_ok = physics->raycast(origin, dir, max_d, kAllLayers, hit) && hit.body == near_h;

        mask_ok = !physics->raycast(origin, dir, max_d, 2u, hit);
        miss_ok = !physics->raycast(origin, {-1.f, 0.f, 0.f}, max_d, kAllLayers, hit);

        physics->destroy_body(near_h);
        physics->destroy_body(far_h);
    }

    const bool passed = aabb_ok && sphere_ok && capsule_ok && closest_ok && mask_ok && miss_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Physics raycast gate: aabb=%s sphere=%s capsule=%s closest=%s mask=%s miss=%s (%s)",
        aabb_ok ? "yes" : "no", sphere_ok ? "yes" : "no", capsule_ok ? "yes" : "no",
        closest_ok ? "yes" : "no", mask_ok ? "yes" : "no", miss_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Physics, message);
    return passed;
}

bool run_character_gate(engine::physics::IPhysics* physics) {
    using engine::gameplay::CharacterController;
    using engine::gameplay::CharacterDesc;
    using engine::gameplay::is_walkable_ground;
    using engine::physics::BodyDesc;
    using engine::physics::MotionType;
    using engine::physics::ShapeType;

    bool grounded_ok = false;
    bool walk_ok = false;
    bool jump_ok = false;
    bool step_ok = false;
    bool slope_ok = false;

    if (physics) {
        physics->set_gravity({0.f, -9.81f, 0.f});
        const engine::f32 dt = 1.f / 60.f;

        BodyDesc floor{};
        floor.shape.type = ShapeType::Aabb;
        floor.shape.half_extents = {6.f, 0.5f, 6.f};
        floor.position = {0.f, -0.5f, 0.f};
        floor.motion = MotionType::Static;
        const auto floor_h = physics->create_body(floor);

        CharacterDesc desc{};
        desc.radius = 0.25f;
        desc.half_height = 0.4f;
        desc.walk_speed = 4.f;
        desc.jump_speed = 6.f;
        desc.step_offset = 0.35f;
        desc.slope_limit_deg = 45.f;
        const engine::f32 rest = desc.half_height + desc.radius;

        CharacterController cc;
        cc.spawn(*physics, {0.f, rest, 0.f}, desc);
        for (int i = 0; i < 8; ++i) {
            cc.move({}, false, dt);
        }
        grounded_ok = floor_h.valid() && cc.grounded()
            && std::abs(cc.position().y - rest) < 0.05f;

        const engine::f32 z0 = cc.position().z;
        for (int i = 0; i < 30; ++i) {
            cc.move({0.f, 0.f, 1.f}, false, dt);
        }
        walk_ok = cc.grounded() && cc.position().z > z0 + 0.5f
            && std::abs(cc.position().y - rest) < 0.08f;

        cc.destroy();
        cc.spawn(*physics, {0.f, rest, 0.f}, desc);
        for (int i = 0; i < 8; ++i) {
            cc.move({}, false, dt);
        }
        cc.move({}, true, dt);
        bool left_ground = false;
        bool rose = false;
        for (int i = 0; i < 8; ++i) {
            cc.move({}, false, dt);
            if (!cc.grounded()) {
                left_ground = true;
            }
            if (cc.position().y > rest + 0.15f) {
                rose = true;
            }
        }
        bool landed = false;
        for (int i = 0; i < 90; ++i) {
            cc.move({}, false, dt);
            if (cc.grounded() && std::abs(cc.position().y - rest) < 0.08f) {
                landed = true;
                break;
            }
        }
        jump_ok = left_ground && rose && landed;

        cc.destroy();
        BodyDesc step{};
        step.shape.type = ShapeType::Aabb;
        step.shape.half_extents = {0.5f, 0.12f, 0.5f};
        step.position = {0.f, 0.12f, 1.5f};
        step.motion = MotionType::Static;
        const auto step_h = physics->create_body(step);
        cc.spawn(*physics, {0.f, rest, 0.f}, desc);
        for (int i = 0; i < 8; ++i) {
            cc.move({}, false, dt);
        }
        bool climbed = false;
        for (int i = 0; i < 40; ++i) {
            cc.move({0.f, 0.f, 1.f}, false, dt);
            if (cc.position().z > 1.2f && cc.position().y > rest + 0.1f && cc.grounded()) {
                climbed = true;
                break;
            }
        }
        step_ok = step_h.valid() && climbed;

        slope_ok = is_walkable_ground({0.f, 1.f, 0.f}, 45.f)
            && !is_walkable_ground({0.f, 0.5f, 0.866f}, 45.f);

        cc.destroy();
        physics->destroy_body(step_h);
        physics->destroy_body(floor_h);
    }

    const bool passed = grounded_ok && walk_ok && jump_ok && step_ok && slope_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Character gate: walk=%s jump=%s step=%s slope=%s grounded=%s (%s)",
        walk_ok ? "yes" : "no", jump_ok ? "yes" : "no", step_ok ? "yes" : "no",
        slope_ok ? "yes" : "no", grounded_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

} // namespace sandbox
