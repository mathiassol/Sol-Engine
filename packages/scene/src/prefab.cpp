#include <engine/scene/prefab.hpp>

#include <engine/scene/scene_file.hpp>

#include <cstring>
#include <string_view>
#include <vector>

namespace engine::scene {
namespace {

bool in_subtree(const World& world, u32 index, u32 root) {
    u32 walk = index;
    for (u32 i = 0; i < kMaxInstances && walk != kInvalidInstance; ++i) {
        if (walk >= world.instance_count) {
            return false;
        }
        if (walk == root) {
            return true;
        }
        walk = world.instances[walk].parent;
    }
    return false;
}

bool join_name(std::string_view prefix, std::string_view name, char* buf, usize buf_bytes,
    std::string_view& out) {
    const usize total = prefix.size() + name.size();
    if (total == 0 || total > kMaxNameChars || total + 1 > buf_bytes) {
        return false;
    }
    if (!prefix.empty()) {
        std::memcpy(buf, prefix.data(), prefix.size());
    }
    if (!name.empty()) {
        std::memcpy(buf + prefix.size(), name.data(), name.size());
    }
    buf[total] = '\0';
    out = std::string_view(buf, total);
    return true;
}

} // namespace

bool extract_prefab(const World& world, std::string_view root_name, World& out) {
    out = World{};
    const u32 root = find_instance(world, root_name);
    if (root == kInvalidInstance) {
        return false;
    }

    u32 mat_remap[kMaxMaterials];
    for (u32 i = 0; i < kMaxMaterials; ++i) {
        mat_remap[i] = kInvalidInstance;
    }

    u32 copied = 0;
    for (u32 i = 0; i < world.instance_count; ++i) {
        if (!in_subtree(world, i, root)) {
            continue;
        }
        const std::string_view name = instance_name(world, i);
        if (name.empty()) {
            continue;
        }
        Instance instance = world.instances[i];
        instance.parent = kInvalidInstance;
        if (instance.material < world.material_count) {
            if (mat_remap[instance.material] == kInvalidInstance) {
                if (out.material_count >= kMaxMaterials) {
                    return false;
                }
                mat_remap[instance.material] = add_material(out, world.materials[instance.material]);
            }
            instance.material = mat_remap[instance.material];
        }
        if (out.instance_count >= kMaxInstances) {
            return false;
        }
        const u32 index = add_instance(out, instance);
        set_instance_name(out, index, name);
        copied += 1;
    }
    if (copied == 0) {
        return false;
    }

    for (u32 i = 0; i < world.instance_count; ++i) {
        if (!in_subtree(world, i, root)) {
            continue;
        }
        const std::string_view name = instance_name(world, i);
        const std::string_view parent_name =
            world.instances[i].parent != kInvalidInstance && world.instances[i].parent < world.instance_count
            ? instance_name(world, world.instances[i].parent)
            : std::string_view{};
        if (name.empty() || parent_name.empty()) {
            continue;
        }
        if (!in_subtree(world, world.instances[i].parent, root)) {
            continue;
        }
        const u32 child = find_instance(out, name);
        const u32 parent = find_instance(out, parent_name);
        if (child == kInvalidInstance || parent == kInvalidInstance) {
            continue;
        }
        if (!set_instance_parent(out, child, parent, false)) {
            return false;
        }
    }
    return true;
}

u32 instantiate_prefab(World& dest, const World& prefab, const math::Mat4& world_transform,
    std::string_view prefix) {
    u32 named = 0;
    for (u32 i = 0; i < prefab.instance_count; ++i) {
        const std::string_view name = instance_name(prefab, i);
        if (name.empty()) {
            continue;
        }
        if (prefix.size() + name.size() > kMaxNameChars) {
            return kInvalidInstance;
        }
        named += 1;
    }
    if (named == 0 || dest.instance_count + named > kMaxInstances
        || dest.material_count + prefab.material_count > kMaxMaterials) {
        return kInvalidInstance;
    }

    u32 mat_remap[kMaxMaterials];
    for (u32 i = 0; i < kMaxMaterials; ++i) {
        mat_remap[i] = kInvalidInstance;
    }
    for (u32 i = 0; i < prefab.material_count; ++i) {
        mat_remap[i] = add_material(dest, prefab.materials[i]);
    }

    struct PendingParent {
        u32 dest_index = 0;
        std::string_view parent_name;
    };
    std::vector<PendingParent> parents;
    u32 first = kInvalidInstance;

    for (u32 i = 0; i < prefab.instance_count; ++i) {
        const std::string_view name = instance_name(prefab, i);
        if (name.empty()) {
            continue;
        }
        Instance instance = prefab.instances[i];
        if (instance.material < prefab.material_count) {
            instance.material = mat_remap[instance.material];
        }
        const u32 prefab_parent = instance.parent;
        instance.parent = kInvalidInstance;
        if (prefab_parent == kInvalidInstance) {
            instance.model = world_transform * instance.model;
        }
        const u32 dest_index = add_instance(dest, instance);
        char buf[kMaxNameChars + 1]{};
        std::string_view spawned_name;
        if (!join_name(prefix, name, buf, sizeof(buf), spawned_name)) {
            return kInvalidInstance;
        }
        set_instance_name(dest, dest_index, spawned_name);
        if (first == kInvalidInstance) {
            first = dest_index;
        }
        if (prefab_parent != kInvalidInstance && prefab_parent < prefab.instance_count) {
            const std::string_view parent_name = instance_name(prefab, prefab_parent);
            if (!parent_name.empty()) {
                parents.push_back({dest_index, parent_name});
            }
        }
    }

    char parent_buf[kMaxNameChars + 1]{};
    for (const PendingParent& pending : parents) {
        std::string_view parent_spawned;
        if (!join_name(prefix, pending.parent_name, parent_buf, sizeof(parent_buf), parent_spawned)) {
            return kInvalidInstance;
        }
        const u32 parent = find_instance(dest, parent_spawned);
        if (parent == kInvalidInstance) {
            continue;
        }
        if (!set_instance_parent(dest, pending.dest_index, parent, false)) {
            return kInvalidInstance;
        }
    }
    return first;
}

u32 instantiate_prefab(World& dest, std::string_view text, const math::Mat4& world_transform,
    std::string_view prefix) {
    World prefab{};
    if (!read_world(text, prefab)) {
        return kInvalidInstance;
    }
    return instantiate_prefab(dest, prefab, world_transform, prefix);
}

} // namespace engine::scene
