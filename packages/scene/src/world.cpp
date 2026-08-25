#include <engine/scene/world.hpp>

#include <engine/core/assert.hpp>

#include <algorithm>
#include <cstring>
#include <string_view>

namespace engine::scene {

u32 add_material(World& world, const Material& material) {
    ENGINE_ASSERT_MSG(world.material_count < kMaxMaterials, "scene material overflow");
    const u32 index = world.material_count;
    world.materials[index] = material;
    world.material_count += 1;
    return index;
}

u32 add_instance(World& world, const Instance& instance) {
    ENGINE_ASSERT_MSG(world.instance_count < kMaxInstances, "scene instance overflow");
    const u32 index = world.instance_count;
    world.instances[index] = instance;
    world.instance_count += 1;
    return index;
}

void set_instance_model(World& world, u32 index, const math::Mat4& model) {
    ENGINE_ASSERT(index < world.instance_count);
    world.instances[index].model = model;
}

namespace {

bool would_cycle(const World& world, u32 child, u32 parent) {
    u32 walk = parent;
    for (u32 i = 0; i < kMaxInstances && walk != kInvalidInstance; ++i) {
        if (walk == child) {
            return true;
        }
        if (walk >= world.instance_count) {
            return true;
        }
        walk = world.instances[walk].parent;
    }
    return false;
}

} // namespace

math::Mat4 instance_world_model(const World& world, u32 index) {
    ENGINE_ASSERT(index < world.instance_count);
    math::Mat4 world_model = world.instances[index].model;
    u32 walk = world.instances[index].parent;
    for (u32 i = 0; i < kMaxInstances && walk != kInvalidInstance; ++i) {
        if (walk >= world.instance_count) {
            break;
        }
        world_model = world.instances[walk].model * world_model;
        walk = world.instances[walk].parent;
    }
    return world_model;
}

u32 instance_parent(const World& world, u32 index) {
    ENGINE_ASSERT(index < world.instance_count);
    return world.instances[index].parent;
}

bool set_instance_parent(World& world, u32 index, u32 parent, bool keep_world) {
    ENGINE_ASSERT(index < world.instance_count);
    if (parent == index) {
        return false;
    }
    if (parent != kInvalidInstance) {
        if (parent >= world.instance_count || would_cycle(world, index, parent)) {
            return false;
        }
    }

    const math::Mat4 child_world = instance_world_model(world, index);
    math::Mat4 parent_world = math::Mat4::identity();
    if (parent != kInvalidInstance) {
        parent_world = instance_world_model(world, parent);
    }

    world.instances[index].parent = parent;
    if (keep_world) {
        if (parent == kInvalidInstance) {
            world.instances[index].model = child_world;
        } else {
            world.instances[index].model = math::Mat4::inverse_affine(parent_world) * child_world;
        }
    }
    return true;
}

NameId intern_name(World& world, std::string_view name) {
    if (name.empty()) {
        return 0;
    }
    for (u32 i = 1; i < world.names.count; ++i) {
        if (name == std::string_view(world.names.chars[i])) {
            return i;
        }
    }
    ENGINE_ASSERT_MSG(world.names.count < NameTable::kCapacity, "scene name intern overflow");
    const u32 id = world.names.count;
    const usize n = std::min(name.size(), static_cast<usize>(kMaxNameChars));
    std::memcpy(world.names.chars[id], name.data(), n);
    world.names.chars[id][n] = '\0';
    world.names.count += 1;
    return id;
}

void set_instance_name(World& world, u32 index, std::string_view name) {
    ENGINE_ASSERT(index < world.instance_count);
    world.instances[index].name = intern_name(world, name);
}

std::string_view instance_name(const World& world, u32 index) {
    ENGINE_ASSERT(index < world.instance_count);
    const NameId id = world.instances[index].name;
    if (id == 0 || id >= world.names.count) {
        return {};
    }
    return world.names.chars[id];
}

u32 find_instance(const World& world, std::string_view name) {
    if (name.empty()) {
        return kInvalidInstance;
    }
    for (u32 i = 0; i < world.instance_count; ++i) {
        if (instance_name(world, i) == name) {
            return i;
        }
    }
    return kInvalidInstance;
}

} // namespace engine::scene
