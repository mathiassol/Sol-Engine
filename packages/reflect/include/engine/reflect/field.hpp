#pragma once

#include <engine/core/types.hpp>

#include <cstddef>
#include <span>

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
    FieldType type = FieldType::Count;
};

// A POD struct and its fields. Does not own the fields - descriptors are
// `static constexpr` beside the struct they describe. A span rather than a
// pointer and a count, so "the count matches the array" is a property of the
// type instead of an obligation on every call site.
struct TypeDesc {
    const char* name = nullptr;
    u32 size = 0;
    u32 align = 0;
    std::span<const FieldDesc> fields{};
};

// What is wrong with a descriptor set. Ordered so the cheapest structural
// checks come first; validate returns the first failure it finds.
enum class TypeError : u8 {
    Ok,
    NoFields,
    EmptyName,
    // The field names FieldType::Count, or a value outside the enum. Caught
    // explicitly because size_of returns kVariableSize for it, which is the
    // same signal Name legitimately gives - so without this guard a sentinel
    // used by mistake would skip the size check exactly like a variable-width
    // field, which is the hazard Task 2b existed to close.
    InvalidFieldType,
    SizeDisagreesWithType,
    OffsetsNotAscending,
    FieldOverlapsPrevious,
    FieldPastEnd,
    // A gap wide enough to hold another field of the following field's
    // alignment - the signature of a field left out of the table.
    InteriorGapTooLarge,
    // Room after the last field for one more - the signature of a field
    // appended to the struct and forgotten here. The common case.
    TrailingGapTooLarge,
};

// Checks a descriptor set against the struct it claims to describe. constexpr
// so a type can static_assert its own completeness, which is the generalisation
// of frame_pipelines.hpp's
// static_assert(sizeof(FramePipelines) == count * sizeof(void*)) to fields that
// do not all have the same width.
//
// The gap rules are what catch a *missing* field, and they are heuristics with
// a precise bound: padding between two fields can never be as wide as the
// following field's alignment, so a gap that wide means something is not
// described. Same reasoning after the last field, using the struct's alignment.
constexpr TypeError validate(const TypeDesc& type) {
    if (type.fields.empty()) {
        return TypeError::NoFields;
    }
    u32 cursor = 0;
    for (usize i = 0; i < type.fields.size(); ++i) {
        const FieldDesc& f = type.fields[i];
        if (f.name == nullptr || f.name[0] == '\0') {
            return TypeError::EmptyName;
        }
        // Before the size cross-check, not after: size_of(Count) is
        // kVariableSize, so a sentinel would otherwise be waved through by the
        // `nominal != kVariableSize` test below as if it were a Name field.
        if (!is_valid_field_type(f.type)) {
            return TypeError::InvalidFieldType;
        }
        const u32 nominal = size_of(f.type);
        if (nominal != kVariableSize && f.size != nominal) {
            return TypeError::SizeDisagreesWithType;
        }
        if (i > 0) {
            const FieldDesc& prev = type.fields[i - 1];
            if (f.offset < prev.offset) {
                return TypeError::OffsetsNotAscending;
            }
            if (f.offset < prev.offset + prev.size) {
                return TypeError::FieldOverlapsPrevious;
            }
            const u32 gap = f.offset - cursor;
            // align_of never returns 0 since Task 2b's table - Name and any
            // out-of-range value both report 1 - so no guard is needed here.
            if (gap >= align_of(f.type)) {
                return TypeError::InteriorGapTooLarge;
            }
        }
        if (f.offset + f.size > type.size) {
            return TypeError::FieldPastEnd;
        }
        cursor = f.offset + f.size;
    }
    const u32 tail = type.size - cursor;
    // type.align comes from alignof() at the call site, so 0 means a caller
    // built the TypeDesc by hand and got it wrong; treat it as 1 rather than
    // dividing the tail rule by nothing.
    const u32 struct_align = type.align == 0 ? 1 : type.align;
    if (tail >= struct_align) {
        return TypeError::TrailingGapTooLarge;
    }
    return TypeError::Ok;
}

const char* to_string(TypeError error);

} // namespace engine::reflect

// Builds a FieldDesc whose name, offset and width all come from the same token,
// so a descriptor cannot disagree with the member it describes. Hand-writing
// the three is the error this removes - the same reason ENGINE_ASSERT captures
// __FILE__ rather than asking for it.
#define ENGINE_REFLECT_FIELD(Type, member, field_type)                        \
    ::engine::reflect::FieldDesc{                                             \
        #member, static_cast<::engine::u32>(offsetof(Type, member)),          \
        static_cast<::engine::u32>(sizeof(Type::member)), (field_type)}
