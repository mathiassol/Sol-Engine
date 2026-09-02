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
constexpr u32 kInvalidMaterial = ~0u;

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
    // 1 means opaque and takes the opaque pipeline; anything less takes the
    // blended one, chosen in the app's scene-to-renderer bridge.
    //
    // A float rather than a bool because the value is what the shader
    // multiplies its alpha by - a separate flag could disagree with it, which
    // is the failure DepthConvention exists as a single value to avoid.
    // Defaulting to 1 is what keeps every existing material opaque.
    f32 opacity = 1.f;
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

// Both return the invalid sentinel and log when the world is full, rather than
// aborting. How many instances a game spawns is a content outcome, not a
// programmer error, and a caller outside the engine cannot be expected to
// bound-check a cap it does not own. Callers must check the result.
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
