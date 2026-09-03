#include <engine/reflect/field.hpp>

namespace engine::reflect {

const char* to_string(FieldType type) {
    switch (type) {
    case FieldType::Bool: return "bool";
    case FieldType::I32:  return "i32";
    case FieldType::U32:  return "u32";
    case FieldType::F32:  return "f32";
    case FieldType::F64:  return "f64";
    case FieldType::Vec2: return "vec2";
    case FieldType::Vec3: return "vec3";
    case FieldType::Vec4: return "vec4";
    case FieldType::Mat4: return "mat4";
    case FieldType::Name: return "name";
    }
    return "?";
}

} // namespace engine::reflect
