#include <engine/scene/scene_file.hpp>

#include <engine/core/log.hpp>

#include <cctype>
#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace engine::scene {
namespace {

// A `.solscene` is hand-editable, so a rejection has to say where and why. This
// used to be 22 bare `return false` paths: the caller learned that the file was
// bad and nothing else, which is a support ticket nobody can answer.
u32 line_of(std::string_view text, usize pos) {
    u32 line = 1;
    const usize end = pos < text.size() ? pos : text.size();
    for (usize k = 0; k < end; ++k) {
        if (text[k] == '\n') {
            line += 1;
        }
    }
    return line;
}

// Always returns false, so a failure path stays a one-liner at the call site.
bool reject(std::string_view text, usize pos, const char* reason) {
    char message[192];
    std::snprintf(message, sizeof(message), "solscene rejected at line %u: %s",
        line_of(text, pos), reason);
    log(LogLevel::Error, LogChannel::Assets, message);
    return false;
}

void skip(std::string_view text, usize& i) {
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isspace(c)) {
            i += 1;
            continue;
        }
        if (text[i] == '#') {
            while (i < text.size() && text[i] != '\n') {
                i += 1;
            }
            continue;
        }
        break;
    }
}

bool take_token(std::string_view text, usize& i, std::string& out) {
    skip(text, i);
    if (i >= text.size()) {
        return false;
    }
    out.clear();
    if (text[i] == '"') {
        i += 1;
        while (i < text.size() && text[i] != '"') {
            out.push_back(text[i]);
            i += 1;
        }
        if (i >= text.size()) {
            return false;
        }
        i += 1;
        return true;
    }
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isspace(c) || text[i] == '#') {
            break;
        }
        out.push_back(text[i]);
        i += 1;
    }
    return !out.empty();
}

bool take_keyword(std::string_view text, usize& i, std::string_view expected) {
    std::string token;
    const usize mark = i;
    if (!take_token(text, i, token) || token != expected) {
        i = mark;
        return false;
    }
    return true;
}

template <typename T>
bool parse_number(std::string_view token, T& out) {
    const char* begin = token.data();
    const char* end = begin + token.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

bool take_f32(std::string_view text, usize& i, f32& out) {
    std::string token;
    return take_token(text, i, token) && parse_number(token, out);
}

bool take_u32(std::string_view text, usize& i, u32& out) {
    std::string token;
    return take_token(text, i, token) && parse_number(token, out);
}

bool take_u64(std::string_view text, usize& i, u64& out) {
    std::string token;
    return take_token(text, i, token) && parse_number(token, out);
}

bool take_vec3(std::string_view text, usize& i, math::Vec3& out) {
    return take_f32(text, i, out.x) && take_f32(text, i, out.y) && take_f32(text, i, out.z);
}

void append_f32(std::string& out, f32 value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), " %.9g", static_cast<double>(value));
    out += buf;
}

void append_vec3(std::string& out, math::Vec3 v) {
    append_f32(out, v.x);
    append_f32(out, v.y);
    append_f32(out, v.z);
}

void append_mat4(std::string& out, const math::Mat4& m) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            append_f32(out, m.cols[c][r]);
        }
    }
}

bool take_mat4(std::string_view text, usize& i, math::Mat4& out) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!take_f32(text, i, out.cols[c][r])) {
                return false;
            }
        }
    }
    return true;
}

void append_name(std::string& out, std::string_view name) {
    out.push_back('"');
    out.append(name.data(), name.size());
    out.push_back('"');
}

bool name_ok(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameChars) {
        return false;
    }
    for (char c : name) {
        if (c == '"' || c == '\n' || c == '\r') {
            return false;
        }
    }
    return true;
}

} // namespace

bool write_world(const World& world, std::string& out) {
    out.clear();
    out += "solscene 1\n";
    out += "ambient";
    append_vec3(out, world.ambient);
    out += "\nsun_dir";
    append_vec3(out, world.sun.direction);
    out += "\nsun_color";
    append_vec3(out, world.sun.color);
    out += '\n';
    for (u32 i = 0; i < kMaxPointLights; ++i) {
        const PointLight& light = world.points[i];
        out += "point ";
        out += std::to_string(i);
        append_vec3(out, light.position);
        append_vec3(out, light.color);
        append_f32(out, light.radius);
        append_f32(out, light.intensity);
        out += '\n';
    }
    for (u32 i = 0; i < world.material_count; ++i) {
        const Material& material = world.materials[i];
        out += "material ";
        out += std::to_string(material.albedo);
        append_f32(out, material.metallic);
        append_f32(out, material.roughness);
        out += '\n';
    }
    for (u32 i = 0; i < world.instance_count; ++i) {
        const std::string_view name = instance_name(world, i);
        if (!name_ok(name)) {
            continue;
        }
        const Instance& instance = world.instances[i];
        out += "instance ";
        append_name(out, name);
        out += ' ';
        out += std::to_string(static_cast<unsigned long long>(instance.mesh.id));
        out += ' ';
        out += std::to_string(instance.mesh.generation);
        out += ' ';
        out += std::to_string(instance.material);
        out += ' ';
        std::string_view parent_name{};
        if (instance.parent != kInvalidInstance && instance.parent < world.instance_count) {
            parent_name = instance_name(world, instance.parent);
        }
        if (name_ok(parent_name)) {
            append_name(out, parent_name);
        } else {
            out += '-';
        }
        append_mat4(out, instance.model);
        out += '\n';
    }
    return true;
}

bool read_world(std::string_view text, World& out) {
    out = World{};
    usize i = 0;
    if (!take_keyword(text, i, "solscene")) {
        return reject(text, i, "expected the magic word \"solscene\"");
    }
    u32 version = 0;
    if (!take_u32(text, i, version) || version != 1) {
        return reject(text, i, "expected format version 1");
    }

    struct PendingParent {
        u32 index = 0;
        std::string name;
    };
    std::vector<PendingParent> parents;

    while (true) {
        skip(text, i);
        if (i >= text.size()) {
            break;
        }
        std::string keyword;
        if (!take_token(text, i, keyword)) {
            return reject(text, i, "expected a keyword");
        }
        if (keyword == "ambient") {
            if (!take_vec3(text, i, out.ambient)) {
                return reject(text, i, "ambient wants three floats");
            }
        } else if (keyword == "sun_dir") {
            if (!take_vec3(text, i, out.sun.direction)) {
                return reject(text, i, "sun_dir wants three floats");
            }
        } else if (keyword == "sun_color") {
            if (!take_vec3(text, i, out.sun.color)) {
                return reject(text, i, "sun_color wants three floats");
            }
        } else if (keyword == "point") {
            u32 index = 0;
            PointLight light{};
            if (!take_u32(text, i, index) || index >= kMaxPointLights) {
                return reject(text, i, "point index must be below kMaxPointLights");
            }
            if (!take_vec3(text, i, light.position) || !take_vec3(text, i, light.color)
                || !take_f32(text, i, light.radius) || !take_f32(text, i, light.intensity)) {
                return reject(text, i, "point wants position, colour, radius, intensity");
            }
            out.points[index] = light;
        } else if (keyword == "material") {
            Material material{};
            if (!take_u32(text, i, material.albedo) || !take_f32(text, i, material.metallic)
                || !take_f32(text, i, material.roughness)) {
                return reject(text, i, "material wants albedo, metallic, roughness");
            }
            if (out.material_count >= kMaxMaterials) {
                return reject(text, i, "more materials than kMaxMaterials");
            }
            add_material(out, material);
        } else if (keyword == "instance") {
            std::string name;
            Instance instance{};
            std::string parent;
            if (!take_token(text, i, name) || !name_ok(name)) {
                return reject(text, i, "instance name missing or too long");
            }
            if (!take_u64(text, i, instance.mesh.id) || !take_u32(text, i, instance.mesh.generation)
                || !take_u32(text, i, instance.material) || !take_token(text, i, parent)
                || !take_mat4(text, i, instance.model)) {
                return reject(text, i, "instance wants mesh id, generation, material, parent, 16 floats");
            }
            if (out.instance_count >= kMaxInstances) {
                return reject(text, i, "more instances than kMaxInstances");
            }
            // The `point` and `material` cases below bound-check their index;
            // this one did not, so a file could name a material that does not
            // exist. Its only protection was a check in the sandbox's extract.
            if (instance.material >= kMaxMaterials) {
                return reject(text, i, "instance names a material index that does not exist");
            }
            const u32 index = add_instance(out, instance);
            set_instance_name(out, index, name);
            if (parent != "-") {
                parents.push_back({index, std::move(parent)});
            }
        } else {
            return reject(text, i, "unknown keyword");
        }
    }

    for (const PendingParent& pending : parents) {
        const u32 parent = find_instance(out, pending.name);
        if (parent == kInvalidInstance) {
            continue;
        }
        if (!set_instance_parent(out, pending.index, parent, false)) {
            return reject(text, i, "parent link would create a cycle");
        }
    }
    return true;
}

} // namespace engine::scene
