#pragma once

#include <engine/assets/mesh.hpp>
#include <engine/core/types.hpp>
#include <engine/math/mat4.hpp>
#include <engine/math/vec3.hpp>

#include <string_view>

namespace engine::scene {

constexpr u32 kMaxInstances = 512;
constexpr u32 kMaxMaterials = 16;
constexpr u32 kMaxPointLights = 4;
constexpr u32 kMaxNameChars = 31;
constexpr u32 kInvalidInstance = ~0u;

using MaterialHandle = u32;
using NameId = u32;

struct Camera {
    math::Mat4 view{};
    math::Mat4 projection{};
};

struct DirectionalLight {
    math::Vec3 direction{0.35f, 0.85f, -0.4f};
    math::Vec3 color{1.f, 0.95f, 0.88f};
};

struct PointLight {
    math::Vec3 position{};
    math::Vec3 color{1.f, 1.f, 1.f};
    f32 radius = 0.f;
    f32 intensity = 0.f;
};

struct Material {
    u32 albedo = 0;
    f32 metallic = 0.f;
    f32 roughness = 0.5f;
};

struct Instance {
    assets::MeshHandle mesh{};
    math::Mat4 model = math::Mat4::identity(); // local; world is instance_world_model
    MaterialHandle material = 0;
    NameId name = 0;
    u32 parent = kInvalidInstance;
};

struct NameTable {
    static constexpr u32 kCapacity = kMaxInstances + 1;
    char chars[kCapacity][kMaxNameChars + 1]{};
    u32 count = 1;
};

struct World {
    Camera camera;
    DirectionalLight sun;
    math::Vec3 ambient{0.18f, 0.19f, 0.22f};
    PointLight points[kMaxPointLights]{};
    Material materials[kMaxMaterials]{};
    Instance instances[kMaxInstances]{};
    NameTable names{};
    u32 material_count = 0;
    u32 instance_count = 0;
};

u32 add_material(World& world, const Material& material);
u32 add_instance(World& world, const Instance& instance);
void set_instance_model(World& world, u32 index, const math::Mat4& model);
bool set_instance_parent(World& world, u32 index, u32 parent, bool keep_world = true);
u32 instance_parent(const World& world, u32 index);
math::Mat4 instance_world_model(const World& world, u32 index);
NameId intern_name(World& world, std::string_view name);
void set_instance_name(World& world, u32 index, std::string_view name);
std::string_view instance_name(const World& world, u32 index);
u32 find_instance(const World& world, std::string_view name);

} // namespace engine::scene
