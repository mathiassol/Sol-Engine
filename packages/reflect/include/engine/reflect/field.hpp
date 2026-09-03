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

constexpr u32 size_of(FieldType) {
    return 0; // STUB - Task 2 Step 6 replaces this
}

constexpr u32 align_of(FieldType) {
    return 0; // STUB - Task 2 Step 6 replaces this
}

const char* to_string(FieldType type);

} // namespace engine::reflect
