#pragma once

// The only file that includes the vendored Vulkan headers, so that surface has
// exactly one door. Everything else in this package includes this.
//
// VK_NO_PROTOTYPES and VOLK_VULKAN_H_PATH come from the target's compile
// definitions rather than from here, so a translation unit that forgets to
// include this header fails to compile instead of quietly declaring the API
// twice.
// volk, pointed at vulkan_headers_sol.h - see that file for why the platform
// headers are gathered rather than included here. This is still the only door
// to the vendored surface: everything else in the package includes this.
#include <volk.h>

#include <engine/core/log.hpp>
#include <engine/core/types.hpp>

#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace engine::rhi::vulkan {

// The results this backend can actually produce, named.
//
// vk_enum_string_helper.h covers every enum in the API and costs 817 KB of
// vendored code to do it. This covers what the calls here return and fits on a
// screen, in the house aligned-case style.
inline const char* to_string(VkResult result) {
    switch (result) {
    case VK_SUCCESS:                     return "VK_SUCCESS";
    case VK_NOT_READY:                   return "VK_NOT_READY";
    case VK_TIMEOUT:                     return "VK_TIMEOUT";
    case VK_INCOMPLETE:                  return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:    return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:  return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:           return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:     return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:     return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:   return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:   return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:      return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:  return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:       return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_OUT_OF_POOL_MEMORY:    return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_UNKNOWN:               return "VK_ERROR_UNKNOWN";
    default:                             return "VkResult(other)";
    }
}

// Logs and returns true on failure, so a call site reads as one `if`.
//
// A named function rather than a macro: a macro that hides a `return` is how a
// failure path stops being visible in the code that contains it.
inline bool vk_failed(VkResult result, const char* what) {
    if (result == VK_SUCCESS) {
        return false;
    }
    char message[192];
    std::snprintf(message, sizeof(message), "Vulkan %s failed: %s", what, to_string(result));
    log(LogLevel::Error, LogChannel::Render, message);
    return true;
}

// True when the bytecode is a SPIR-V module, checked before the API sees it.
//
// Handing DXIL to vkCreateShaderModule is a validation error whose message is
// about a magic number, from a layer the caller may not have enabled at all -
// so it is worth one comparison to say "this is the other backend's bytecode"
// in the engine's own words instead. 0x07230203 is SPIR-V's magic; DXIL
// containers start with 'DXBC', 0x43425844.
inline bool is_spirv(std::span<const u8> bytecode, const char* what) {
    u32 magic = 0;
    if (bytecode.size() >= 4) {
        std::memcpy(&magic, bytecode.data(), 4);
    }
    if (bytecode.size() >= 4 && bytecode.size() % 4 == 0 && magic == 0x07230203u) {
        return true;
    }
    char message[176];
    std::snprintf(message, sizeof(message),
        "%s is not SPIR-V: %zu bytes, first word 0x%08X (SPIR-V is 0x07230203, DXIL is "
        "0x43425844)", what, bytecode.size(), magic);
    log(LogLevel::Error, LogChannel::Render, message);
    return false;
}

// The name of the module's entry point, read out of the SPIR-V itself.
//
// Vulkan wants the entry point name at pipeline creation; the contract supplies
// it at *compile* time, on ShaderCompileDesc, and the other backend needs it
// never - a D3D12 pipeline takes bytecode and nothing else. Rather than add a
// field to GraphicsPipelineDesc and ComputePipelineDesc that only one backend
// reads, the module is asked: it declares its own entry point, so this cannot
// disagree with the shader the way a duplicated string can.
//
// This is the fourth place the contract holds a fact at bind or compile time
// that Vulkan needs at pipeline creation - after the vertex stride, the `u`
// slot descriptor type, and the resolve. The others needed a variant cache;
// this one is just a read.
//
// SPIR-V layout: five header words, then instructions. OpEntryPoint is opcode
// 15 - word 0 is (wordCount << 16) | opcode, word 1 the execution model, word 2
// the function id, and words 3.. the null-terminated name packed four bytes to
// a word.
inline std::string spirv_entry_point(std::span<const u8> bytecode, const char* fallback) {
    if (bytecode.size() < 20 || bytecode.size() % 4 != 0) {
        return fallback;
    }
    const usize word_count = bytecode.size() / 4;
    std::vector<u32> words(word_count);
    std::memcpy(words.data(), bytecode.data(), bytecode.size());
    usize at = 5;
    while (at < word_count) {
        const u32 instruction = words[at];
        const u32 length = instruction >> 16;
        const u32 opcode = instruction & 0xFFFFu;
        if (length == 0 || at + length > word_count) {
            break;
        }
        if (opcode == 15u && length >= 4) {
            const char* name = reinterpret_cast<const char*>(&words[at + 3]);
            const usize available = (length - 3) * 4;
            const usize len = ::strnlen(name, available);
            return std::string(name, len);
        }
        at += length;
    }
    return fallback;
}

// Says so, once per name, the first time it is reached.
//
// This backend is deliberately partial - it exists to prove the rhi contract
// survives a second API before it makes the engine run on one - so there will
// be many of these for a while. A stub that silently returns is
// indistinguishable from a working implementation, both to a caller chasing a
// black screen and to whoever reads it next.
void not_implemented(const char* what);

} // namespace engine::rhi::vulkan
