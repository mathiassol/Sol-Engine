#include "device_vulkan.hpp"

#include <cstring>
#include <iterator>
#include <vector>

namespace engine::rhi::vulkan {

namespace {

bool gpu_debug_enabled() {
#ifdef NDEBUG
    return false;
#else
    // Same switch the D3D12 backend reads for its debug layer, so one variable
    // turns on whichever backend's validation is in play - and read the same
    // way, because std::getenv is a C4996 deprecation warning under MSVC.
    char value[8]{};
    return GetEnvironmentVariableA("ENGINE_GPU_DEBUG", value, sizeof(value)) > 0
        && value[0] == '1';
#endif
}

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool validation_layer_present() {
    u32 count = 0;
    if (vk_failed(vkEnumerateInstanceLayerProperties(&count, nullptr), "layer enumeration")) {
        return false;
    }
    std::vector<VkLayerProperties> layers(count);
    if (count > 0
        && vk_failed(
            vkEnumerateInstanceLayerProperties(&count, layers.data()), "layer enumeration")) {
        return false;
    }
    for (const VkLayerProperties& layer : layers) {
        if (std::strcmp(layer.layerName, kValidationLayer) == 0) {
            return true;
        }
    }
    return false;
}

// Routed into engine::log on the same channel the D3D12 debug layer uses, so
// CLAUDE.md's rule applies unchanged: any message here is a build-breaking bug,
// not a warning to skip.
VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* user_data) {
    (void)types;
    (void)user_data;
    if (data == nullptr || data->pMessage == nullptr) {
        return VK_FALSE;
    }
    const LogLevel level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0
        ? LogLevel::Error
        : LogLevel::Warn;
    log(level, LogChannel::Render, data->pMessage);
    // Always VK_FALSE: returning VK_TRUE aborts the call that triggered the
    // message, which turns a diagnostic into a different bug.
    return VK_FALSE;
}

// Discrete first, then integrated, and never a CPU device. This mirrors the
// D3D12 backend skipping DXGI_ADAPTER_FLAG_SOFTWARE, and it is the reason
// --gates cannot run on a hosted runner on either backend: there is no hardware
// device to pick and falling back to a software one would report an
// environmental fact as a product defect.
u32 device_score(const VkPhysicalDeviceProperties& properties) {
    switch (properties.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 3;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 2;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 1;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return 0;
    default:                                     return 0;
    }
}

// Dynamic rendering is required, not preferred. It is what lets
// begin_render_pass(RenderPassInfo) map to vkCmdBeginRendering with no
// VkRenderPass/VkFramebuffer cache behind it - and a cache keyed on an
// attachment set is exactly the kind of per-backend machinery the rhi contract
// is shaped to avoid needing.
bool supports_swapchain(VkPhysicalDevice gpu) {
    u32 count = 0;
    if (vk_failed(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr),
            "device extension enumeration")) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (count > 0
        && vk_failed(
            vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, extensions.data()),
            "device extension enumeration")) {
        return false;
    }
    for (const VkExtensionProperties& extension : extensions) {
        if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            return true;
        }
    }
    return false;
}

bool supports_dynamic_rendering(VkPhysicalDevice gpu) {
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(gpu, &features);
    return features13.dynamicRendering == VK_TRUE && features13.synchronization2 == VK_TRUE;
}

u32 find_graphics_family(VkPhysicalDevice gpu) {
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
    for (u32 i = 0; i < count; ++i) {
        // Graphics implies transfer, so one family covers the draw and the
        // readback copy. A dedicated transfer queue is worth having when
        // uploads compete with the frame; nothing here does yet.
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            return i;
        }
    }
    return ~0u;
}

} // namespace

namespace {

// The one instance, and how many devices hold it. A function-local static so
// static-initialisation order cannot bite - the same reason the cvar registry
// uses one.
struct SharedInstance {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    bool validation_enabled = false;
    u32 refs = 0;
};

SharedInstance& shared_instance() {
    static SharedInstance state;
    return state;
}

bool instance_extension_present(const char* name) {
    u32 count = 0;
    if (vk_failed(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
            "instance extension enumeration")) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (count > 0
        && vk_failed(
            vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()),
            "instance extension enumeration")) {
        return false;
    }
    for (const VkExtensionProperties& extension : extensions) {
        if (std::strcmp(extension.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool create_shared_instance(SharedInstance& shared) {
    if (vk_failed(volkInitialize(), "loader initialisation")) {
        log(LogLevel::Error, LogChannel::Render,
            "No Vulkan loader found. vulkan-1.dll ships with the GPU driver, so this "
            "usually means no Vulkan-capable driver is installed.");
        return false;
    }

    const bool want_validation = gpu_debug_enabled() && validation_layer_present();
    if (gpu_debug_enabled() && !want_validation) {
        log(LogLevel::Warn, LogChannel::Render,
            "ENGINE_GPU_DEBUG=1 but VK_LAYER_KHRONOS_validation is not installed - Vulkan "
            "work will run unvalidated. It ships with the Vulkan SDK.");
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Sol Engine";
    app.pEngineName = "Sol Engine";
    // 1.3 for dynamic rendering and synchronization2 in core. Asking for it up
    // front means an old driver fails here, by name, rather than at the first
    // vkCmdBeginRendering.
    app.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> layers;
    std::vector<const char*> extensions;
    // Requested whenever the loader has them, rather than only when the first
    // caller wants a window. The instance is shared now, so it cannot be
    // tailored to one device's needs: an offscreen device created first would
    // otherwise leave a later windowed one with no surface extension and no way
    // to add one. Asking for what is present costs nothing, and a machine with
    // no presentation support simply does not list them - a windowed device
    // then fails at create_surface, by name.
    if (instance_extension_present(VK_KHR_SURFACE_EXTENSION_NAME)
        && instance_extension_present(VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    }
    if (want_validation) {
        layers.push_back(kValidationLayer);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    }

    // Two validation features beyond the default set, both because the default
    // set was **silent** through a device loss.
    //
    // RHI #24's first live frame reported VK_ERROR_DEVICE_LOST from the present
    // with the standard layer saying nothing at all - which is what happens
    // when the fault is on the GPU rather than in the API calls. Standard
    // validation checks the calls; these two check what the calls describe.
    //
    //   * SYNCHRONIZATION_VALIDATION tracks hazards - a resource written while
    //     it is still being read, a pool reset while the GPU has work in it.
    //     That is the class of bug that appears only on *reuse*, and the frame
    //     loop gate found the loss on the third frame, the first one that
    //     reuses a slot.
    //   * GPU_ASSISTED instruments the shaders to catch out-of-bounds
    //     descriptor and buffer access, which faults the device and reports
    //     nothing on the API side.
    //
    // Both are expensive - GPU-assisted rewrites every shader - and both are
    // behind ENGINE_GPU_DEBUG with everything else. A diagnostic that only
    // exists on the machine of whoever debugged it once is worth less than the
    // afternoon it saves.
    const VkValidationFeatureEnableEXT enabled_features[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
    };
    VkValidationFeaturesEXT features{};
    features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    features.enabledValidationFeatureCount
        = static_cast<u32>(std::size(enabled_features));
    features.pEnabledValidationFeatures = enabled_features;

    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &app;
    create.pNext = want_validation ? &features : nullptr;
    create.enabledLayerCount = static_cast<u32>(layers.size());
    create.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
    create.enabledExtensionCount = static_cast<u32>(extensions.size());
    create.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

    if (vk_failed(
            vkCreateInstance(&create, nullptr, &shared.instance), "instance creation")) {
        return false;
    }
    volkLoadInstance(shared.instance);
    shared.validation_enabled = want_validation;

    if (want_validation) {
        VkDebugUtilsMessengerCreateInfoEXT messenger{};
        messenger.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger.pfnUserCallback = debug_callback;
        if (vk_failed(vkCreateDebugUtilsMessengerEXT(
                          shared.instance, &messenger, nullptr, &shared.messenger),
                "debug messenger creation")) {
            // Not fatal: an unvalidated device still renders, and saying so is
            // better than refusing to start over a diagnostic channel.
            shared.messenger = VK_NULL_HANDLE;
        } else {
            log(LogLevel::Info, LogChannel::Render,
                "Vulkan validation layer enabled (ENGINE_GPU_DEBUG=1)");
        }
    }

    return true;
}

} // namespace

bool acquire_vulkan_instance(VulkanInstance& out, bool want_surface) {
    SharedInstance& shared = shared_instance();
    if (shared.instance == VK_NULL_HANDLE) {
        if (!create_shared_instance(shared)) {
            // A half-built instance is a reference nobody holds: tear it down
            // here rather than leave it for the next caller to find.
            if (shared.instance != VK_NULL_HANDLE) {
                vkDestroyInstance(shared.instance, nullptr);
                shared.instance = VK_NULL_HANDLE;
            }
            return false;
        }
    }
    out.instance = shared.instance;
    out.messenger = shared.messenger;
    out.validation_enabled = shared.validation_enabled;

    u32 gpu_count = 0;
    if (vk_failed(vkEnumeratePhysicalDevices(out.instance, &gpu_count, nullptr),
            "physical device enumeration")
        || gpu_count == 0) {
        log(LogLevel::Error, LogChannel::Render, "No Vulkan physical devices");
        return false;
    }
    std::vector<VkPhysicalDevice> gpus(gpu_count);
    if (vk_failed(vkEnumeratePhysicalDevices(out.instance, &gpu_count, gpus.data()),
            "physical device enumeration")) {
        return false;
    }

    u32 best_score = 0;
    VkPhysicalDeviceProperties best_properties{};
    for (VkPhysicalDevice gpu : gpus) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(gpu, &properties);
        const u32 score = device_score(properties);
        if (score == 0) {
            continue;
        }
        if (!supports_dynamic_rendering(gpu)) {
            continue;
        }
        // Only demanded when presenting. A device that can render but not
        // present is still a perfectly good offscreen device, and refusing it
        // would make the parity gates unavailable on one.
        if (want_surface && !supports_swapchain(gpu)) {
            continue;
        }
        const u32 family = find_graphics_family(gpu);
        if (family == ~0u) {
            continue;
        }
        if (score > best_score) {
            best_score = score;
            best_properties = properties;
            out.gpu = gpu;
            out.graphics_family = family;
        }
    }

    if (out.gpu == VK_NULL_HANDLE) {
        log(LogLevel::Error, LogChannel::Render,
            "No Vulkan device with a graphics queue, dynamic rendering, synchronization2 "
            "and (when presenting) a swapchain. Software devices are skipped deliberately, "
            "the same way the D3D12 backend skips software adapters.");
        return false;
    }

    out.api_version = best_properties.apiVersion;
    out.device_name = best_properties.deviceName;
    // Taken here, not at the top: every failure above returns with `out` still
    // holding VK_NULL_HANDLE, and release_vulkan_instance keys off that to stay
    // a no-op. A reference taken before the physical-device pass would be
    // leaked by any caller that failed it.
    ++shared.refs;
    char message[192];
    std::snprintf(message, sizeof(message), "Vulkan device selected: %s (API %u.%u.%u)",
        out.device_name.c_str(), VK_API_VERSION_MAJOR(out.api_version),
        VK_API_VERSION_MINOR(out.api_version), VK_API_VERSION_PATCH(out.api_version));
    log(LogLevel::Info, LogChannel::Render, message);
    return true;
}

void release_vulkan_instance(VulkanInstance& state) {
    // Only a device that actually acquired one holds a reference - a failed
    // acquire leaves its struct null, so this stays a no-op for it.
    if (state.instance == VK_NULL_HANDLE) {
        return;
    }
    state.instance = VK_NULL_HANDLE;
    state.messenger = VK_NULL_HANDLE;
    state.gpu = VK_NULL_HANDLE;
    state.graphics_family = ~0u;

    SharedInstance& shared = shared_instance();
    if (shared.refs > 0) {
        --shared.refs;
    }
    if (shared.refs != 0) {
        return;
    }
    if (shared.messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(shared.instance, shared.messenger, nullptr);
        shared.messenger = VK_NULL_HANDLE;
    }
    if (shared.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(shared.instance, nullptr);
        shared.instance = VK_NULL_HANDLE;
    }
    shared.validation_enabled = false;
}

u32 find_memory_type(VkPhysicalDevice gpu, u32 type_bits, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &properties);
    for (u32 i = 0; i < properties.memoryTypeCount; ++i) {
        const bool allowed = (type_bits & (1u << i)) != 0;
        const bool suits = (properties.memoryTypes[i].propertyFlags & required) == required;
        if (allowed && suits) {
            return i;
        }
    }
    return ~0u;
}

} // namespace engine::rhi::vulkan
