#pragma once

// The header volk is pointed at, via VOLK_VULKAN_H_PATH.
//
// volk guards every Win32 entry point - declaration *and* definition - on
// VK_KHR_win32_surface, which is #defined by vulkan_win32.h. volk.h and volk.c
// both include whatever this path names, so pointing volk at vulkan_core.h
// alone declares no surface function and defines no pointer: the symptom is
// vkCreateWin32SurfaceKHR unresolved at link, which reads like a missing
// extension rather than a missing include.
//
// So the platform headers are gathered here, once, and both volk translation
// units see the same set. vulkan.h would do the same job by pulling in every
// platform's surface header, which is exactly why it is not vendored.
//
// Not in third_party/: this file is ours, and that directory is for upstream
// code kept byte-for-byte.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>
