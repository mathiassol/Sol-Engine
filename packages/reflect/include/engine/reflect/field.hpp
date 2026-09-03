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

// What is wrong with a descriptor set. validate returns the first failure it
// finds, per field, in the order the checks appear in its body - which is not
// quite this declaration order (FieldPastEnd is declared before GapTooLarge and
// evaluated after it). Nothing depends on the enum's order; do not read it as
// the evaluation sequence.
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
    // alignment - the signature of a field left out of the table. Applies
    // before the first field as well as between two, since `cursor` starts at
    // zero.
    GapTooLarge,
    // Room after the last field for one more - the signature of a field
    // appended to the struct and forgotten here. The common case.
    TrailingGapTooLarge,
};

// Checks a descriptor set against the struct it claims to describe.
//
// **This must stay in the header.** It is constexpr so a type can
// static_assert its own completeness beside the struct it describes, and a
// constexpr body that lives in a .cpp cannot be evaluated in a static_assert
// from another translation unit. Moving it to src/ would not be a tidy-up; it
// would silently remove the only reason the function exists. It is the largest
// non-template body in any public header here, and that is the trade.
//
// It generalises frame_pipelines.hpp's
// static_assert(sizeof(FramePipelines) == count * sizeof(void*)) to fields that
// do not all have the same width.
//
// For a worked example of a type checking itself this way at namespace scope,
// see the static_asserts beside `kProbeFields` in
// packages/sandbox/src/gates/gates_core.cpp - that is the pattern a consumer
// copies.
//
// The gap rules are what catch a *missing* field. Padding before any member can
// never be as wide as that member's own alignment - the compiler would have
// placed it earlier - so a gap that wide means something is undescribed. That
// holds before the first field, between two, and (against the struct's own
// alignment) after the last.
//
// **What it does not catch.** Read this before trusting it as a correctness
// check; it verifies completeness and widths, not identity, and the limits
// below are inherent to that rather than gaps to be fixed later:
//
//  - **A missing field narrower than the tolerance it hides in.** The rule
//    permits a gap of up to align-1 bytes, so any contiguous run of missing
//    bytes narrower than that is invisible - and it is the run's total width
//    that matters, not how many fields it was. Two trailing `bool`s where only
//    the first is described leaves a 3-byte tail under a 4-byte threshold and
//    passes. Worse, a *whole* 4-byte field vanishes from
//    `struct { f64 d; u32 x; f32 tail; }` when `tail` is omitted, because the
//    f64 raises the struct's alignment to 8 and the 4-byte tail stays under it.
//  - **A field described with the wrong type but the right width.** `U32` on an
//    actual `f32` member passes every check, because reflect stores no type
//    identity - only widths. It cannot name `engine::math::Vec3` to compare
//    against, by design: this package depends on `core` alone.
//  - **A member aligned more strictly than its FieldType.** `alignas(16)` on a
//    member makes the real gap exceed align_of(FieldType), so a *complete and
//    correct* descriptor set is rejected with GapTooLarge. This is the one
//    failure that is loud rather than silent, and the gap rules assume it does
//    not happen: no reflected member may be aligned more strictly, in the real
//    struct, than its FieldType's nominal alignment. True for every type
//    reflect supports today, and unchecked.
//  - **Packed structs.** Under `#pragma pack` there is no natural slack, yet
//    the rule still tolerates align-1 bytes, so a missing field hides more
//    easily than in an ordinary struct.
//  - **Bitfields.** Not a validate() gap but a model one: `offsetof` on a
//    bit-field member is not allowed, and FieldDesc has no bit-level
//    representation, so such a struct cannot be described field-by-field at all.
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
        }
        // Outside the `i > 0` block on purpose: `cursor` starts at zero, so for
        // the first field this is the gap before it, and a field missing from
        // the front is exactly as easy to make as one missing from the middle.
        // align_of never returns 0 since Task 2b's table, so no guard is needed.
        if (f.offset - cursor >= align_of(f.type)) {
            return TypeError::GapTooLarge;
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
