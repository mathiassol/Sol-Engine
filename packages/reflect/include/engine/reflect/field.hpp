#pragma once

#include <engine/core/types.hpp>

#include <cstddef>

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
    // A sentinel, not a type. Every table indexed by FieldType is
    // static_asserted against it, so adding an enumerator without extending
    // the tables is a build error rather than a silent zero.
    Count,
};

// size_of/align_of answer this for a type whose width is per-field.
inline constexpr u32 kVariableSize = 0;

// One row per FieldType, indexed by it. `size` is kVariableSize when the width
// is per-field. Byte counts for the math types are stated rather than derived,
// because reflect does not depend on math; the reflect gate compares every one
// of them against the real sizeof/alignof, and asserts it covered them all.
struct FieldTypeInfo {
    u32 size = kVariableSize;
    u32 align = 1;
    const char* name = "?";
};

inline constexpr FieldTypeInfo kFieldTypes[] = {
    {1, 1, "bool"},
    {4, 4, "i32"},
    {4, 4, "u32"},
    {4, 4, "f32"},
    {8, 8, "f64"},
    {8, 4, "vec2"},
    {12, 4, "vec3"},
    {16, 4, "vec4"},
    {64, 4, "mat4"},
    {kVariableSize, 1, "name"},
};

static_assert(sizeof(kFieldTypes) / sizeof(kFieldTypes[0])
        == static_cast<usize>(FieldType::Count),
    "kFieldTypes needs exactly one row per FieldType");

// Count is a sentinel, so it is not a valid index. Guarding rather than
// asserting keeps these usable in a constant expression from a caller that has
// not validated its input yet.
constexpr bool is_valid_field_type(FieldType type) {
    return static_cast<usize>(type) < static_cast<usize>(FieldType::Count);
}

constexpr u32 size_of(FieldType type) {
    return is_valid_field_type(type) ? kFieldTypes[static_cast<usize>(type)].size : kVariableSize;
}

constexpr u32 align_of(FieldType type) {
    return is_valid_field_type(type) ? kFieldTypes[static_cast<usize>(type)].align : 1;
}

const char* to_string(FieldType type);

// One field of a POD struct. `size` is authoritative: for FieldType::Name it is
// the only width available, and for everything else the reflect gate proves it
// equals size_of(type).
struct FieldDesc {
    const char* name = nullptr;
    u32 offset = 0;
    u32 size = 0;
    FieldType type = FieldType::U32;
};

// A POD struct and its fields. Does not own the array - descriptors are
// `static constexpr` beside the struct they describe.
struct TypeDesc {
    const char* name = nullptr;
    u32 size = 0;
    u32 align = 0;
    const FieldDesc* fields = nullptr;
    u32 field_count = 0;
};

} // namespace engine::reflect

// Builds a FieldDesc whose name, offset and width all come from the same token,
// so a descriptor cannot disagree with the member it describes. Hand-writing
// the three is the error this removes - the same reason ENGINE_ASSERT captures
// __FILE__ rather than asking for it.
#define ENGINE_FIELD(Type, member, field_type)                                 \
    ::engine::reflect::FieldDesc{                                              \
        #member, static_cast<::engine::u32>(offsetof(Type, member)),           \
        static_cast<::engine::u32>(sizeof(Type::member)), field_type}
