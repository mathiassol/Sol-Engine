#include <engine/reflect/field.hpp>

namespace engine::reflect {

const char* to_string(FieldType type) {
    return is_valid_field_type(type) ? kFieldTypes[static_cast<usize>(type)].name : "?";
}

} // namespace engine::reflect
