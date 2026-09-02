#pragma once

// The only file that includes the vendored Vulkan headers, so that surface has
// exactly one door. Everything else in this package includes this.
//
// VK_NO_PROTOTYPES and VOLK_VULKAN_H_PATH come from the target's compile
// definitions rather than from here, so a translation unit that forgets to
// include this header fails to compile instead of quietly declaring the API
// twice.
#include <volk.h>

#include <engine/core/log.hpp>
#include <engine/core/types.hpp>

#include <cstdio>

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

// Says so, once per name, the first time it is reached.
//
// This backend is deliberately partial - it exists to prove the rhi contract
// survives a second API before it makes the engine run on one - so there will
// be many of these for a while. A stub that silently returns is
// indistinguishable from a working implementation, both to a caller chasing a
// black screen and to whoever reads it next.
void not_implemented(const char* what);

} // namespace engine::rhi::vulkan
