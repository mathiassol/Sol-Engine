#include <engine/reflect/field.hpp>

namespace engine::reflect {

const char* to_string(FieldType type) {
    return is_valid_field_type(type) ? kFieldTypes[static_cast<usize>(type)].name : "?";
}

const char* to_string(TypeError error) {
    switch (error) {
    case TypeError::Ok:                    return "ok";
    case TypeError::NoFields:              return "no fields";
    case TypeError::EmptyName:             return "empty name";
    case TypeError::InvalidFieldType:      return "field type is not a real type";
    case TypeError::SizeDisagreesWithType: return "size disagrees with type";
    case TypeError::OffsetsNotAscending:   return "offsets not ascending";
    case TypeError::FieldOverlapsPrevious: return "field overlaps previous";
    case TypeError::FieldPastEnd:          return "field past end of struct";
    case TypeError::GapTooLarge:           return "gap - a field is missing";
    case TypeError::TrailingGapTooLarge:   return "trailing gap - a field is missing";
    }
    return "?";
}

} // namespace engine::reflect
