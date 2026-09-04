#include "../sandbox_common.hpp"

// Scene, world and camera gates.
//
// Moved out of main.cpp, which held all 72 and was 26% of the engine
// (analizeMax A4). What a gate *is* has not changed - see CLAUDE.md. Helpers
// private to these gates are `static` here; only what main.cpp also uses lives
// in sandbox_common.

namespace sandbox {

bool run_scene_world_gate(const engine::scene::World& world) {
    const bool count_ok = world.instance_count >= 2;
    bool models_differ = false;
    bool moved = false;
    if (count_ok) {
        models_differ = std::memcmp(&world.instances[0].model, &world.instances[1].model,
            sizeof(engine::math::Mat4)) != 0;
        engine::scene::World nudged = world;
        const engine::math::Mat4 before = nudged.instances[0].model;
        engine::scene::set_instance_model(nudged, 0,
            engine::math::Mat4::translate({0.1f, 0.f, 0.f}) * before);
        moved = std::memcmp(&nudged.instances[0].model, &before, sizeof(engine::math::Mat4)) != 0;
    }
    const bool passed = count_ok && models_differ && moved;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Scene world gate: instances=%u distinct=%s move=%s (%s)",
        world.instance_count,
        models_differ ? "yes" : "no",
        moved ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

// The gate that makes scene files a content path rather than a library: it
// loads a real file off disk through the mounted loader, which is the thing no
// runtime code could do before load_world existed. Everything it asserts comes
// from the file, and the mesh ids are cross-checked against make_mesh_handle
// rather than trusted, so a change to the hash or the mesh key fails here
// instead of silently producing a scene of invalid handles.
bool run_scene_load_gate(engine::assets::IAssetLoader& loader) {
    engine::scene::World world;
    const bool loaded = engine::scene::load_world(loader, kDemoScene, world);

    const engine::assets::MeshHandle husky = engine::assets::make_mesh_handle(kHuskyMesh);
    const engine::assets::MeshHandle ground = engine::assets::make_mesh_handle(kGroundMesh);

    const bool counts_ok = loaded && world.instance_count == 3 && world.material_count == 2;
    bool names_ok = false;
    bool meshes_ok = false;
    bool parent_ok = false;
    bool child_moved = false;
    if (counts_ok) {
        const engine::u32 a = engine::scene::find_instance(world, "husky_a");
        const engine::u32 b = engine::scene::find_instance(world, "husky_b");
        const engine::u32 g = engine::scene::find_instance(world, "ground");
        names_ok = a != engine::scene::kInvalidInstance && b != engine::scene::kInvalidInstance
            && g != engine::scene::kInvalidInstance;
        if (names_ok) {
            meshes_ok = world.instances[a].mesh == husky && world.instances[b].mesh == husky
                && world.instances[g].mesh == ground;
            parent_ok = world.instances[b].parent == a
                && world.instances[a].parent == engine::scene::kInvalidInstance;
            // world = parent * local, so the child sits at 0.5 + 0.8.
            const engine::math::Mat4 world_b = engine::scene::instance_world_model(world, b);
            child_moved = std::fabs(world_b.cols[3].y - 1.3f) < 1.e-4f;
        }
    }

    const bool passed = counts_ok && names_ok && meshes_ok && parent_ok && child_moved;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Scene load gate: file=%s instances=%u materials=%u names=%s meshes=%s parent=%s "
        "child_y=%.3f (%s)",
        loaded ? "read" : "MISSING", world.instance_count, world.material_count,
        names_ok ? "yes" : "no", meshes_ok ? "yes" : "no", parent_ok ? "yes" : "no",
        static_cast<double>(counts_ok && names_ok
                ? engine::scene::instance_world_model(world,
                      engine::scene::find_instance(world, "husky_b")).cols[3].y
                : 0.f),
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

// Filling the world to its cap used to be untestable: add_instance asserted, and
// no gate can exercise a path that calls std::abort(). It returns a sentinel and
// logs now, so the degradation can be asserted like anything else.
bool run_scene_capacity_gate() {
    engine::scene::World world;

    engine::u32 last_instance = 0;
    bool all_instances_ok = true;
    for (engine::u32 i = 0; i < engine::scene::kMaxInstances; ++i) {
        last_instance = engine::scene::add_instance(world, {});
        if (last_instance != i) {
            all_instances_ok = false;
        }
    }
    const bool inst_full = world.instance_count == engine::scene::kMaxInstances;
    const engine::u32 over_instance = engine::scene::add_instance(world, {});
    const bool inst_rejected = over_instance == engine::scene::kInvalidInstance;
    const bool inst_no_growth = world.instance_count == engine::scene::kMaxInstances;

    bool all_materials_ok = true;
    for (engine::u32 i = 0; i < engine::scene::kMaxMaterials; ++i) {
        if (engine::scene::add_material(world, {}) != i) {
            all_materials_ok = false;
        }
    }
    const engine::u32 over_material = engine::scene::add_material(world, {});
    const bool mat_rejected = over_material == engine::scene::kInvalidMaterial;
    const bool mat_no_growth = world.material_count == engine::scene::kMaxMaterials;

    const bool passed = all_instances_ok && inst_full && inst_rejected && inst_no_growth
        && all_materials_ok && mat_rejected && mat_no_growth;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Scene capacity gate: instances=%u/%u last=%u over=%s count_held=%s "
        "materials=%u/%u over=%s count_held=%s (%s)",
        world.instance_count, engine::scene::kMaxInstances, last_instance,
        inst_rejected ? "invalid" : "GREW",
        inst_no_growth ? "yes" : "no",
        world.material_count, engine::scene::kMaxMaterials,
        mat_rejected ? "invalid" : "GREW",
        mat_no_growth ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_scene_name_gate() {
    engine::scene::World world{};
    const engine::u32 alpha = engine::scene::add_instance(world, {});
    const engine::u32 beta = engine::scene::add_instance(world, {});
    const engine::u32 unnamed = engine::scene::add_instance(world, {});
    engine::scene::set_instance_name(world, alpha, "alpha");
    engine::scene::set_instance_name(world, beta, "beta");

    const engine::scene::NameId intern_a = engine::scene::intern_name(world, "alpha");
    const engine::scene::NameId intern_b = engine::scene::intern_name(world, "alpha");
    const bool intern_ok = intern_a != 0 && intern_a == intern_b
        && intern_a == world.instances[alpha].name;

    const bool find_ok = engine::scene::find_instance(world, "alpha") == alpha
        && engine::scene::find_instance(world, "beta") == beta
        && engine::scene::instance_name(world, alpha) == "alpha"
        && engine::scene::instance_name(world, beta) == "beta";

    const bool unnamed_ok = world.instances[unnamed].name == 0
        && engine::scene::instance_name(world, unnamed).empty()
        && engine::scene::find_instance(world, "") == engine::scene::kInvalidInstance
        && engine::scene::intern_name(world, "") == 0;

    const engine::u32 dup = engine::scene::add_instance(world, {});
    engine::scene::set_instance_name(world, dup, "alpha");
    const bool dup_ok = engine::scene::find_instance(world, "alpha") == alpha
        && world.instances[dup].name == world.instances[alpha].name;

    const bool miss_ok
        = engine::scene::find_instance(world, "nope") == engine::scene::kInvalidInstance;

    const bool passed = intern_ok && find_ok && unnamed_ok && dup_ok && miss_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Scene name gate: intern=yes find=yes unnamed=yes dup=first miss=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_scene_hierarchy_gate() {
    using engine::math::Mat4;
    using engine::math::Vec3;

    auto origin_of = [](const Mat4& m) {
        return m.transform_point({0.f, 0.f, 0.f});
    };
    auto near3 = [](Vec3 a, Vec3 b) {
        const Vec3 d = a - b;
        return std::abs(d.x) < 1.e-4f && std::abs(d.y) < 1.e-4f && std::abs(d.z) < 1.e-4f;
    };

    engine::scene::World world{};
    const engine::u32 parent = engine::scene::add_instance(world, {});
    const engine::u32 child = engine::scene::add_instance(world, {});
    const engine::u32 grand = engine::scene::add_instance(world, {});
    engine::scene::set_instance_model(world, parent, Mat4::translate({10.f, 0.f, 0.f}));
    engine::scene::set_instance_model(world, child, Mat4::translate({1.f, 0.f, 0.f}));
    engine::scene::set_instance_model(world, grand, Mat4::translate({0.f, 2.f, 0.f}));

    const bool parented = engine::scene::set_instance_parent(world, child, parent, false)
        && engine::scene::set_instance_parent(world, grand, child, false);
    const Vec3 child_world = origin_of(engine::scene::instance_world_model(world, child));
    const Vec3 grand_world = origin_of(engine::scene::instance_world_model(world, grand));
    const bool compose_ok = parented && near3(child_world, {11.f, 0.f, 0.f})
        && near3(grand_world, {11.f, 2.f, 0.f});

    const bool cycle_ok = !engine::scene::set_instance_parent(world, parent, grand, false)
        && !engine::scene::set_instance_parent(world, child, child, false)
        && engine::scene::instance_parent(world, child) == parent;

    const engine::u32 extra = engine::scene::add_instance(world, {});
    engine::scene::set_instance_model(world, extra, Mat4::translate({3.f, 4.f, 5.f}));
    const Vec3 extra_before = origin_of(engine::scene::instance_world_model(world, extra));
    const bool keep_ok = engine::scene::set_instance_parent(world, extra, parent, true)
        && near3(origin_of(engine::scene::instance_world_model(world, extra)), extra_before);

    const bool unparent_ok = engine::scene::set_instance_parent(
        world, child, engine::scene::kInvalidInstance, true)
        && engine::scene::instance_parent(world, child) == engine::scene::kInvalidInstance
        && near3(origin_of(engine::scene::instance_world_model(world, child)), {11.f, 0.f, 0.f});

    const bool passed = compose_ok && cycle_ok && keep_ok && unparent_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Scene hierarchy gate: compose=yes cycle=no keep_world=yes unparent=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_scene_file_gate() {
    using engine::math::Mat4;
    using engine::math::Vec3;

    auto origin_of = [](const Mat4& m) {
        return m.transform_point({0.f, 0.f, 0.f});
    };
    auto near3 = [](Vec3 a, Vec3 b) {
        const Vec3 d = a - b;
        return std::abs(d.x) < 1.e-3f && std::abs(d.y) < 1.e-3f && std::abs(d.z) < 1.e-3f;
    };

    engine::scene::World world{};
    world.ambient = {0.16f, 0.17f, 0.21f};
    world.sun.direction = {0.12f, 0.42f, 0.90f};
    world.sun.color = {4.8f, 4.4f, 3.8f};
    world.points[0].position = {-0.55f, 0.38f, 0.45f};
    world.points[0].color = {1.f, 0.45f, 0.18f};
    world.points[0].radius = 1.8f;
    world.points[0].intensity = 2.2f;
    engine::scene::Material mat{};
    mat.albedo = 2;
    mat.metallic = 0.1f;
    mat.roughness = 0.4f;
    const engine::u32 mat_id = engine::scene::add_material(world, mat);

    engine::scene::Instance parent{};
    parent.mesh = engine::assets::make_mesh_handle("/content/meshes/cartoon_husky.gltf");
    parent.material = mat_id;
    parent.model = Mat4::translate({10.f, 0.f, 0.f});
    const engine::u32 parent_i = engine::scene::add_instance(world, parent);
    engine::scene::set_instance_name(world, parent_i, "parent");

    engine::scene::Instance child{};
    child.mesh = parent.mesh;
    child.material = mat_id;
    child.model = Mat4::translate({1.f, 0.f, 0.f});
    const engine::u32 child_i = engine::scene::add_instance(world, child);
    engine::scene::set_instance_name(world, child_i, "child");
    engine::scene::set_instance_parent(world, child_i, parent_i, false);

    engine::scene::Instance ghost{};
    ghost.mesh = parent.mesh;
    ghost.model = Mat4::translate({0.f, 9.f, 0.f});
    engine::scene::add_instance(world, ghost);

    std::string text;
    const bool wrote = engine::scene::write_world(world, text);
    engine::scene::World loaded{};
    const bool named_ok = wrote && engine::scene::read_world(text, loaded)
        && loaded.instance_count == 2
        && engine::scene::find_instance(loaded, "parent") != engine::scene::kInvalidInstance
        && engine::scene::find_instance(loaded, "child") != engine::scene::kInvalidInstance;

    const engine::u32 loaded_parent = engine::scene::find_instance(loaded, "parent");
    const engine::u32 loaded_child = engine::scene::find_instance(loaded, "child");
    const bool unnamed_ok = named_ok
        && engine::scene::find_instance(loaded, "") == engine::scene::kInvalidInstance
        && loaded.instance_count == 2;

    const bool hierarchy_ok = named_ok
        && engine::scene::instance_parent(loaded, loaded_child) == loaded_parent
        && near3(origin_of(engine::scene::instance_world_model(loaded, loaded_child)),
            {11.f, 0.f, 0.f});

    const bool lights_ok = named_ok
        && near3(loaded.ambient, world.ambient)
        && near3(loaded.sun.color, world.sun.color)
        && near3(loaded.points[0].position, world.points[0].position)
        && std::abs(loaded.points[0].intensity - 2.2f) < 1.e-3f
        && loaded.material_count == 1
        && loaded.materials[0].albedo == 2;

    const bool mesh_ok = named_ok
        && loaded.instances[loaded_parent].mesh == parent.mesh
        && loaded.instances[loaded_child].mesh.generation == parent.mesh.generation;

    engine::scene::World rejected{};
    const bool reject_ok = !engine::scene::read_world("nope", rejected)
        && !engine::scene::read_world("solscene 99\nambient 0 0 0", rejected);

    const bool passed = named_ok && unnamed_ok && hierarchy_ok && lights_ok && mesh_ok && reject_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Scene file gate: named=yes unnamed=drop hierarchy=yes lights=yes mesh=yes reject=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_scene_prefab_gate() {
    using engine::math::Mat4;
    using engine::math::Vec3;

    auto origin_of = [](const Mat4& m) {
        return m.transform_point({0.f, 0.f, 0.f});
    };
    auto near3 = [](Vec3 a, Vec3 b) {
        const Vec3 d = a - b;
        return std::abs(d.x) < 1.e-3f && std::abs(d.y) < 1.e-3f && std::abs(d.z) < 1.e-3f;
    };

    engine::scene::World source{};
    engine::scene::Material mat{};
    mat.albedo = 3;
    mat.roughness = 0.35f;
    const engine::u32 mat_id = engine::scene::add_material(source, mat);
    engine::scene::Instance body{};
    body.mesh = engine::assets::make_mesh_handle("/content/meshes/cartoon_husky.gltf");
    body.material = mat_id;
    body.model = Mat4::translate({1.f, 0.f, 0.f});
    const engine::u32 body_i = engine::scene::add_instance(source, body);
    engine::scene::set_instance_name(source, body_i, "body");
    engine::scene::Instance head{};
    head.mesh = body.mesh;
    head.material = mat_id;
    head.model = Mat4::translate({0.f, 2.f, 0.f});
    const engine::u32 head_i = engine::scene::add_instance(source, head);
    engine::scene::set_instance_name(source, head_i, "head");
    engine::scene::set_instance_parent(source, head_i, body_i, false);
    engine::scene::Instance other{};
    other.model = Mat4::translate({9.f, 0.f, 0.f});
    const engine::u32 other_i = engine::scene::add_instance(source, other);
    engine::scene::set_instance_name(source, other_i, "other");

    engine::scene::World fragment{};
    engine::scene::World miss{};
    const bool extract_ok = engine::scene::extract_prefab(source, "body", fragment)
        && fragment.instance_count == 2
        && engine::scene::find_instance(fragment, "body") != engine::scene::kInvalidInstance
        && engine::scene::find_instance(fragment, "head") != engine::scene::kInvalidInstance
        && engine::scene::find_instance(fragment, "other") == engine::scene::kInvalidInstance
        && !engine::scene::extract_prefab(source, "nope", miss);

    std::string text;
    const bool file_ok = extract_ok && engine::scene::write_world(fragment, text);

    engine::scene::World dest{};
    const engine::u32 a = engine::scene::instantiate_prefab(dest, fragment,
        Mat4::translate({10.f, 0.f, 0.f}), "a_");
    const engine::u32 b = engine::scene::instantiate_prefab(dest, text,
        Mat4::translate({0.f, 0.f, 5.f}), "b_");
    const engine::u32 a_body = engine::scene::find_instance(dest, "a_body");
    const engine::u32 a_head = engine::scene::find_instance(dest, "a_head");
    const engine::u32 b_body = engine::scene::find_instance(dest, "b_body");
    const bool spawn_ok = file_ok && a != engine::scene::kInvalidInstance
        && b != engine::scene::kInvalidInstance && a_body == a
        && dest.instance_count == 4
        && engine::scene::instance_parent(dest, a_head) == a_body
        && engine::scene::instance_parent(dest, engine::scene::find_instance(dest, "b_head"))
            == b_body;

    const bool compose_ok = spawn_ok
        && near3(origin_of(engine::scene::instance_world_model(dest, a_body)), {11.f, 0.f, 0.f})
        && near3(origin_of(engine::scene::instance_world_model(dest, a_head)), {11.f, 2.f, 0.f})
        && near3(origin_of(engine::scene::instance_world_model(dest, b_body)), {1.f, 0.f, 5.f});

    const bool prefix_ok = spawn_ok
        && dest.materials[0].albedo == 3
        && dest.material_count == 2
        && dest.instances[a_body].mesh == body.mesh;

    engine::scene::World packed{};
    for (engine::u32 i = 0; i < engine::scene::kMaxInstances; ++i) {
        const engine::u32 idx = engine::scene::add_instance(packed, {});
        char name[12];
        std::snprintf(name, sizeof(name), "n%u", i);
        engine::scene::set_instance_name(packed, idx, name);
    }
    const bool full_ok = engine::scene::instantiate_prefab(packed, fragment, Mat4::identity(), "x_")
        == engine::scene::kInvalidInstance;

    const bool passed = extract_ok && spawn_ok && compose_ok && prefix_ok && full_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Scene prefab gate: extract=yes spawn=yes prefix=yes compose=yes miss=yes full=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_light_gate(const engine::scene::World& world) {
    engine::renderer::Lighting lighting{};
    engine::scene_render::extract_lighting(world, {0.f, 0.35f, -2.2f}, lighting);
    const engine::f32 sun_len = lighting.sun_direction.length();
    const bool sun_ok = sun_len > 0.99f && sun_len < 1.01f && lighting.sun_color.x > 0.2f;
    const bool ambient_ok = lighting.ambient.x > 0.05f;
    bool point_ok = false;
    for (engine::u32 i = 0; i < engine::renderer::kMaxPointLights; ++i) {
        if (lighting.point_color_intensity[i].w > 0.f && lighting.point_pos_radius[i].w > 0.f) {
            point_ok = true;
        }
    }
    const bool layout_ok = sizeof(engine::renderer::FrameConstants) == 336;
    const bool copied = lighting.sun_color.x == world.sun.color.x
        && lighting.ambient.y == world.ambient.y;
    const bool passed = sun_ok && ambient_ok && point_ok && layout_ok && copied;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Light gate: sun_len=%.3f ambient=%.2f points=%s layout=%s (%s)",
        sun_len, lighting.ambient.x, point_ok ? "yes" : "no",
        layout_ok ? "400" : "bad", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_camera_gate() {
    using engine::gameplay::CameraMode;
    using engine::gameplay::GameCamera;
    using engine::gameplay::next_camera_mode;

    const engine::math::Vec3 target{0.f, 1.f, 0.f};
    GameCamera cam;
    cam.desc.follow_distance = 4.f;
    cam.desc.follow_height = 1.5f;
    cam.desc.look_height = 0.4f;
    cam.desc.orbit_distance = 4.f;
    cam.desc.eye_height = 0.5f;

    cam.yaw = 0.f;
    cam.pitch = 0.f;
    cam.set_mode(CameraMode::Follow);
    cam.update(target);
    const engine::math::Vec3 follow_a = cam.position();
    const engine::math::Vec3 follow_view = cam.view().transform_point(target);
    cam.pitch = 0.8f;
    cam.update(target);
    const engine::math::Vec3 follow_b = cam.position();
    const bool follow_ok = std::abs(follow_a.x) < 0.05f && follow_a.z < target.z - 3.5f
        && follow_a.y > target.y + 1.2f && follow_view.z < 0.f
        && std::abs(follow_b.y - follow_a.y) < 0.05f
        && next_camera_mode(CameraMode::Follow) == CameraMode::Orbit;

    cam.pitch = 0.f;
    cam.set_mode(CameraMode::Orbit);
    cam.update(target);
    const engine::math::Vec3 orbit_a = cam.position();
    const engine::f32 orbit_dist_a = (orbit_a - cam.look_at()).length();
    cam.pitch = 0.8f;
    cam.update(target);
    const engine::math::Vec3 orbit_b = cam.position();
    const engine::f32 orbit_dist_b = (orbit_b - cam.look_at()).length();
    const engine::math::Vec3 orbit_view = cam.view().transform_point(cam.look_at());
    const bool orbit_ok = std::abs(orbit_dist_a - 4.f) < 0.05f
        && std::abs(orbit_dist_b - 4.f) < 0.05f
        && orbit_b.y > orbit_a.y + 1.5f && orbit_view.z < 0.f
        && next_camera_mode(CameraMode::Orbit) == CameraMode::Fps;

    cam.pitch = 0.f;
    cam.set_mode(CameraMode::Fps);
    cam.update(target);
    const engine::math::Vec3 fps_pos = cam.position();
    const engine::math::Vec3 ahead = fps_pos + cam.forward();
    const engine::math::Vec3 fps_view = cam.view().transform_point(ahead);
    const bool fps_ok = std::abs(fps_pos.x - target.x) < 0.01f
        && std::abs(fps_pos.z - target.z) < 0.01f
        && std::abs(fps_pos.y - (target.y + 0.5f)) < 0.01f
        && fps_view.z < 0.f
        && (fps_pos - target).length() < 1.f
        && next_camera_mode(CameraMode::Fps) == CameraMode::Follow;

    const bool passed = follow_ok && orbit_ok && fps_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Camera gate: follow=%s orbit=%s fps=%s (%s)",
        follow_ok ? "yes" : "no", orbit_ok ? "yes" : "no", fps_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

} // namespace sandbox
