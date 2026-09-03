#include <engine/reflect/field.hpp>

namespace engine::reflect {

const char* to_string(FieldType type) {
    return is_type(type) ? kFieldTypes[static_cast<usize>(type)].name : "?";
}

} // namespace engine::reflect
