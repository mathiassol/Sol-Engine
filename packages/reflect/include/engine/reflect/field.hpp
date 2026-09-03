#pragma once

#include <engine/core/types.hpp>

// Describes the fields of a POD struct: name, byte offset, size, type.
//
// This exists so that text serialisation, the command layer and an editor
// inspector are one mechanism rather than three hand-written ones that drift
// apart. See docs/superpowers/specs/2026-09-03-editor-architecture-design.md.
//
// Deliberately knows nothing about `math`: `size_of` carries byte counts for
// the vector types, and the reflect gate in the sandbox cross-checks them
// against the real `sizeof`, because that is where both are visible.
namespace engine::reflect {

// Only types that exist in the tree today. `EntityId` and `EntityRef` arrive
// with those types in step 2; a tag for a type nobody can name is drift.
enum class FieldType : u8 {
    Bool,
    I32,
    U32,
    F32,
    F64,
    Vec2,
    Vec3,
    Vec4,
    Mat4,
    // A fixed-size char buffer. Its width differs per field, so FieldDesc's own
    // `size` is authoritative and size_of reports kVariableSize.
    Name,
};

// size_of/align_of answer this for a type whose width is per-field.
inline constexpr u32 kVariableSize = 0;

// Byte counts for the math types are stated here rather than derived, because
// reflect does not depend on math. The reflect gate compares every one of them
// against the real sizeof/alignof - that is what keeps this honest.
constexpr u32 size_of(FieldType type) {
    switch (type) {
    case FieldType::Bool: return 1;
    case FieldType::I32:  return 4;
    case FieldType::U32:  return 4;
    case FieldType::F32:  return 4;
    case FieldType::F64:  return 8;
    case FieldType::Vec2: return 8;
    case FieldType::Vec3: return 12;
    case FieldType::Vec4: return 16;
    case FieldType::Mat4: return 64;
    case FieldType::Name: return kVariableSize;
    }
    return kVariableSize;
}

constexpr u32 align_of(FieldType type) {
    switch (type) {
    case FieldType::Bool: return 1;
    case FieldType::F64:  return 8;
    case FieldType::Name: return 1;
    default:              return 4;
    }
}

const char* to_string(FieldType type);

} // namespace engine::reflect
