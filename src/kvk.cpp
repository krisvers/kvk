#define KVK_IMPLEMENTATION
#include "kvk.h"
#include "kvk_util.inl"

#include <vector>
#include <format>

template<>
struct std::formatter<VkFormat> {
    template<class ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template<class FormatContext>
    auto format(VkFormat const& vk_format, FormatContext& ctx) const {
        return std::format_to(ctx.out(), "{}", static_cast<uint32_t>(vk_format));
    }
};

namespace kvk {
static MessageCallback g_error_callback = nullptr;

void set_error_callback(MessageCallback callback) {
    g_error_callback = callback;
}

VkResult create_instance(InstanceCreateInfo const& create_info, VkInstance& vk_instance) {
    VkResult vk_result;
    VkApplicationInfo vk_application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = create_info.app_name,
        .applicationVersion = create_info.app_version,
        .pEngineName = "kvk",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = create_info.vk_version,
    };

    VkDebugUtilsMessengerCreateInfoEXT vk_debug_utils_messenger_create_info_ext = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = create_info.presets.debug_messenger_callback,
        .pUserData = create_info.presets.debug_messenger_callback_user_data,
    };

    VkDebugReportCallbackCreateInfoEXT vk_debug_report_callback_create_info_ext = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
        .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT,
        .pfnCallback = create_info.presets.debug_report_callback,
        .pUserData = create_info.presets.debug_report_callback_user_data,
    };

    void* vk_pnext = create_info.vk_pnext;
    VkFlags vk_flags = create_info.vk_flags;

    std::vector<const char*> enabled_layers(create_info.vk_layers);
    std::vector<const char*> enabled_extensions(create_info.vk_extensions);

    /* specific optional presets */
    if (create_info.presets.enable_validation_layers) {
        enabled_layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    if (create_info.presets.enable_debug_utils) {
        enabled_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    if (create_info.presets.debug_messenger_callback) {
        vk_debug_utils_messenger_create_info_ext.pNext = vk_pnext;
        vk_pnext = &vk_debug_utils_messenger_create_info_ext;
        if (!create_info.presets.enable_debug_utils) {
            enabled_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    }

    if (create_info.presets.debug_report_callback) {
        vk_debug_report_callback_create_info_ext.pNext = vk_pnext;
        vk_pnext = &vk_debug_report_callback_create_info_ext;
        enabled_extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    }

    /* recommended & optional presets */
    if (create_info.presets.recommended || create_info.presets.create_enumerate_portability_instance) {
        enabled_extensions.push_back("VK_KHR_portability_enumeration");
        vk_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    if (create_info.presets.recommended || create_info.presets.enable_surfaces) {
        enabled_extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    }

    if (create_info.presets.recommended || create_info.presets.enable_platform_specific_surfaces) {
        /* TODO: refine platform-specific surface extension selection */
#ifdef KVK_WINDOWS
        enabled_extensions.push_back("VK_KHR_win32_surface");
#elif defined(KVK_APPLE)
        enabled_extensions.push_back("VK_KHR_metal_surface");
#else
        enabled_extensions.push_back("VK_KHR_xlib_surface");
#endif
    }

    VkInstanceCreateInfo vk_instance_create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = vk_pnext,
        .flags = vk_flags,
        .pApplicationInfo = &vk_application_info,
        .enabledLayerCount = static_cast<uint32_t>(enabled_layers.size()),
        .ppEnabledLayerNames = enabled_layers.size() == 0 ? nullptr : enabled_layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size()),
        .ppEnabledExtensionNames = enabled_extensions.size() == 0 ? nullptr : enabled_extensions.data(),
    };

    vk_result = vkCreateInstance(&vk_instance_create_info, nullptr, &vk_instance);
    if (vk_result != VK_SUCCESS) {
        KVK_ERR(vk_result, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, "Failed to create Vulkan instance");
        return vk_result;
    }

    return vk_result;
}

struct PhysicalDeviceFeaturesNext {
    VkBool32 a;
    VkBool32 b;
};

VkPhysicalDevice select_physical_device(VkInstance vk_instance, PhysicalDeviceQuery const& query) {
    VkResult vk_result;
    uint32_t physical_device_count = 0;
    vk_result = vkEnumeratePhysicalDevices(vk_instance, &physical_device_count, nullptr);
    if (vk_result != VK_SUCCESS || physical_device_count == 0) {
        KVK_ERR(vk_result, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, "Failed to enumerate Vulkan physical devices");
        return VK_NULL_HANDLE;
    }

    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    vk_result = vkEnumeratePhysicalDevices(vk_instance, &physical_device_count, physical_devices.data());
    if (vk_result != VK_SUCCESS) {
        KVK_ERR(vk_result, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, "Failed to enumerate Vulkan physical devices");
        return VK_NULL_HANDLE;
    }

    for (VkPhysicalDevice physical_device : physical_devices) {
        VkPhysicalDeviceProperties physical_device_properties;
        vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);
        if (query.device_name_substring != nullptr) {
            if (std::strstr(physical_device_properties.deviceName, query.device_name_substring) == nullptr) {
                continue;
            }
        }

        if (((static_cast<uint32_t>(query.excluded_device_types)) & (1 << (static_cast<uint32_t>(physical_device_properties.deviceType)))) != 0) {
            continue;
        }

        if (physical_device_properties.apiVersion < query.minimum_vk_version) {
            KVK_ERR(VK_SUCCESS, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" has insufficient api version (found {}.{}.{}, required {}.{}.{})", physical_device_properties.deviceName, VK_API_VERSION_MAJOR(physical_device_properties.apiVersion), VK_API_VERSION_MINOR(physical_device_properties.apiVersion), VK_API_VERSION_PATCH(physical_device_properties.apiVersion), VK_API_VERSION_MAJOR(query.minimum_vk_version), VK_API_VERSION_MINOR(query.minimum_vk_version), VK_API_VERSION_PATCH(query.minimum_vk_version));
            continue;
        }

        VkPhysicalDeviceFeatures physical_device_features;
        vkGetPhysicalDeviceFeatures(physical_device, &physical_device_features);

        bool satisfied = true;
        PhysicalDeviceFeaturesNext* features_next = reinterpret_cast<PhysicalDeviceFeaturesNext*>(&physical_device_features.robustBufferAccess);
        PhysicalDeviceFeaturesNext* features_final = reinterpret_cast<PhysicalDeviceFeaturesNext*>(&physical_device_features.inheritedQueries);

        uint32_t feature_index = 0;
        PhysicalDeviceFeaturesNext const* queried_features_next = reinterpret_cast<PhysicalDeviceFeaturesNext const*>(&query.minimum_features.robustBufferAccess);
        while (features_next <= features_final) {
            if (queried_features_next->a && !features_next->a) {
                KVK_ERR(VK_SUCCESS, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum feature at index {}", physical_device_properties.deviceName, feature_index * 2);
                satisfied = false;
                break;
            }

            features_next = reinterpret_cast<PhysicalDeviceFeaturesNext*>(&features_next->b);
            queried_features_next = reinterpret_cast<PhysicalDeviceFeaturesNext const*>(&queried_features_next->b);
            ++feature_index;
        }

        if (!satisfied) {
            continue;
        }

        if (physical_device_properties.limits < query.minimum_limits) {
            KVK_ERR(VK_SUCCESS, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum limits", physical_device_properties.deviceName);
            continue;
        }

        uint32_t available_extension_count;
        vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &available_extension_count, nullptr);

        std::vector<VkExtensionProperties> available_extensions(available_extension_count);
        vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &available_extension_count, available_extensions.data());

        for (uint32_t i = 0; i < query.required_extensions.size(); ++i) {
            bool found = false;
            for (uint32_t j = 0; j < available_extension_count; ++j) {
                if (std::strcmp(query.required_extensions[i].extensionName, available_extensions[j].extensionName) == 0) {
                    if (available_extensions[j].specVersion < query.required_extensions[i].specVersion) {
                        KVK_ERR(VK_SUCCESS, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" has extension \"{}\" but with insufficient spec version (found {}, required {})", physical_device_properties.deviceName, query.required_extensions[i].extensionName, available_extensions[j].specVersion, query.required_extensions[i].specVersion);
                        satisfied = false;
                        break;
                    }

                    found = true;
                    break;
                }
            }

            if (!found && satisfied) {
                KVK_ERR(VK_SUCCESS, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" is missing required extension \"{}\"", physical_device_properties.deviceName, query.required_extensions[i].extensionName);
                satisfied = false;
                break;
            }
        }

        if (!satisfied) {
            continue;
        }

        for (uint32_t i = 0; i < query.minimum_format_properties.size(); ++i) {
            VkFormatProperties format_properties;
            vkGetPhysicalDeviceFormatProperties(physical_device, query.minimum_format_properties[i].format, &format_properties);
            if ((format_properties.linearTilingFeatures & query.minimum_format_properties[i].minimum_properties.linearTilingFeatures) != query.minimum_format_properties[i].minimum_properties.linearTilingFeatures ||
                (format_properties.optimalTilingFeatures & query.minimum_format_properties[i].minimum_properties.optimalTilingFeatures) != query.minimum_format_properties[i].minimum_properties.optimalTilingFeatures ||
                (format_properties.bufferFeatures & query.minimum_format_properties[i].minimum_properties.bufferFeatures) != query.minimum_format_properties[i].minimum_properties.bufferFeatures) {
                KVK_ERR(VK_SUCCESS, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum format properties for format {}", physical_device_properties.deviceName, query.minimum_format_properties[i].format);
                satisfied = false;
                break;
            }
        }

        if (!satisfied) {
            continue;
        }

        for (uint32_t i = 0; i < query.minimum_image_format_properties.size(); ++i) {
            VkImageFormatProperties image_format_properties;
            vk_result = vkGetPhysicalDeviceImageFormatProperties(physical_device,
                query.minimum_image_format_properties[i].format,
                query.minimum_image_format_properties[i].image_type,
                query.minimum_image_format_properties[i].tiling,
                query.minimum_image_format_properties[i].usage,
                query.minimum_image_format_properties[i].flags,
                &image_format_properties);

            if (vk_result != VK_SUCCESS) {
                KVK_ERR(vk_result, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not support image format {} with the specified type, tiling, usage, and flags", physical_device_properties.deviceName, query.minimum_image_format_properties[i].format);
                satisfied = false;
                break;
            }

            if (image_format_properties.maxExtent.width < query.minimum_image_format_properties[i].minimum_properties.maxExtent.width ||
                image_format_properties.maxExtent.height < query.minimum_image_format_properties[i].minimum_properties.maxExtent.height ||
                image_format_properties.maxExtent.depth < query.minimum_image_format_properties[i].minimum_properties.maxExtent.depth ||
                image_format_properties.maxMipLevels < query.minimum_image_format_properties[i].minimum_properties.maxMipLevels ||
                image_format_properties.maxArrayLayers < query.minimum_image_format_properties[i].minimum_properties.maxArrayLayers ||
                image_format_properties.sampleCounts < query.minimum_image_format_properties[i].minimum_properties.sampleCounts ||
                image_format_properties.maxResourceSize < query.minimum_image_format_properties[i].minimum_properties.maxResourceSize) {
                KVK_ERR(VK_SUCCESS, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum image format properties for format {}", physical_device_properties.deviceName, query.minimum_image_format_properties[i].format);
                satisfied = false;
                break;
            }
        }

        if (!satisfied) {
            continue;
        }

        VkPhysicalDeviceMemoryProperties memory_properties;
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
        if (memory_properties.memoryTypeCount < query.minimum_memory_properties.memoryTypeCount) {
            KVK_ERR(VK_SUCCESS, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum memory type count", physical_device_properties.deviceName);
            continue;
        }

        if (memory_properties.memoryHeapCount < query.minimum_memory_properties.memoryHeapCount) {
            KVK_ERR(VK_SUCCESS, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum memory heap count", physical_device_properties.deviceName);
            continue;
        }

        uint32_t physical_device_queue_family_count;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &physical_device_queue_family_count, nullptr);

        std::vector<VkQueueFamilyProperties> queue_family_properties_list(physical_device_queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &physical_device_queue_family_count, queue_family_properties_list.data());

        for (uint32_t i = 0; i < query.required_queues.size(); ++i) {
            bool found = false;
            for (uint32_t j = 0; j < physical_device_queue_family_count; ++j) {
                if (query.required_queues[i].surface_support != nullptr) {
                    VkBool32 vk_surface_support;
                    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, j, query.required_queues[i].surface_support, &vk_surface_support);

                    if (!vk_surface_support) {
                        continue;
                    }
                }

                if (queue_family_properties_list[j].queueCount >= query.required_queues[i].properties.queueCount &&
                    (queue_family_properties_list[j].queueFlags & query.required_queues[i].properties.queueFlags) == query.required_queues[i].properties.queueFlags &&
                    queue_family_properties_list[j].minImageTransferGranularity.width >= query.required_queues[i].properties.minImageTransferGranularity.width &&
                    queue_family_properties_list[j].minImageTransferGranularity.height >= query.required_queues[i].properties.minImageTransferGranularity.height &&
                    queue_family_properties_list[j].minImageTransferGranularity.depth >= query.required_queues[i].properties.minImageTransferGranularity.depth) {

                    queue_family_properties_list[j].queueCount = 0; // prevent re-use

                    found = true;
                    break;
                }
            }

            if (!found) {
                KVK_ERR(VK_SUCCESS, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum queue family properties at index {}", physical_device_properties.deviceName, i);
                satisfied = false;
                break;
            }
        }

        if (!satisfied) {
            continue;
        }

        return physical_device;
    }

    return VK_NULL_HANDLE;
}

VkResult create_device(VkInstance vk_instance, DeviceCreateInfo const& create_info, VkPhysicalDevice& vk_physical_device, VkDevice& vk_device, ReturnArray<DeviceQueueReturn> queue_returns) {
    vk_physical_device = create_info.vk_physical_device;

    VkResult vk_result;
    uint32_t physical_device_count = 0;
    vk_result = vkEnumeratePhysicalDevices(vk_instance, &physical_device_count, nullptr);
    if (vk_result != VK_SUCCESS || physical_device_count == 0) {
        KVK_ERR(vk_result, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, "Failed to enumerate Vulkan physical devices");
        return vk_result;
    }

    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    vk_result = vkEnumeratePhysicalDevices(vk_instance, &physical_device_count, physical_devices.data());
    if (vk_result != VK_SUCCESS) {
        KVK_ERR(vk_result, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, "Failed to enumerate Vulkan physical devices");
        return vk_result;
    }

    bool satisfied = true;
    VkPhysicalDeviceProperties physical_device_properties;
    VkPhysicalDeviceFeatures physical_device_features;
    std::vector<VkExtensionProperties> available_extensions;
    VkPhysicalDeviceMemoryProperties memory_properties;
    std::vector<VkQueueFamilyProperties> queue_family_properties_list;

    std::vector<VkDeviceQueueCreateInfo> vk_device_queue_create_infos;
    if (vk_physical_device == nullptr) {
        for (VkPhysicalDevice physical_device : physical_devices) {
            satisfied = true;

            vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);
            if (create_info.physical_device_query.device_name_substring != nullptr) {
                if (std::strstr(physical_device_properties.deviceName, create_info.physical_device_query.device_name_substring) == nullptr) {
                    continue;
                }
            }

            if ((static_cast<uint32_t>(create_info.physical_device_query.excluded_device_types) & (1 << static_cast<uint32_t>(physical_device_properties.deviceType))) != 0) {
                continue;
            }

            if (physical_device_properties.apiVersion < create_info.physical_device_query.minimum_vk_version) {
                KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" has insufficient api version (found {}.{}.{}, required {}.{}.{})", physical_device_properties.deviceName, VK_API_VERSION_MAJOR(physical_device_properties.apiVersion), VK_API_VERSION_MINOR(physical_device_properties.apiVersion), VK_API_VERSION_PATCH(physical_device_properties.apiVersion), VK_API_VERSION_MAJOR(create_info.physical_device_query.minimum_vk_version), VK_API_VERSION_MINOR(create_info.physical_device_query.minimum_vk_version), VK_API_VERSION_PATCH(create_info.physical_device_query.minimum_vk_version));
                continue;
            }

            vkGetPhysicalDeviceFeatures(physical_device, &physical_device_features);

            PhysicalDeviceFeaturesNext* features_next = reinterpret_cast<PhysicalDeviceFeaturesNext*>(&physical_device_features.robustBufferAccess);
            PhysicalDeviceFeaturesNext* features_final = reinterpret_cast<PhysicalDeviceFeaturesNext*>(&physical_device_features.inheritedQueries);

            uint32_t feature_index = 0;
            PhysicalDeviceFeaturesNext const* queried_features_next = reinterpret_cast<PhysicalDeviceFeaturesNext const*>(&create_info.physical_device_query.minimum_features.robustBufferAccess);
            while (features_next <= features_final) {
                if (queried_features_next->a && !features_next->a) {
                    KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum feature at index {}", physical_device_properties.deviceName, feature_index * 2);
                    satisfied = false;
                    break;
                }

                features_next = reinterpret_cast<PhysicalDeviceFeaturesNext*>(&features_next->b);
                queried_features_next = reinterpret_cast<PhysicalDeviceFeaturesNext const*>(&queried_features_next->b);
                ++feature_index;
            }

            if (!satisfied) {
                continue;
            }

            if (physical_device_properties.limits < create_info.physical_device_query.minimum_limits) {
                KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum limits", physical_device_properties.deviceName);
                continue;
            }

            uint32_t available_extension_count;
            vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &available_extension_count, nullptr);

            available_extensions.resize(available_extension_count);
            vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &available_extension_count, available_extensions.data());

            for (uint32_t i = 0; i < create_info.physical_device_query.required_extensions.size(); ++i) {
                bool found = false;
                for (uint32_t j = 0; j < available_extension_count; ++j) {
                    if (std::strcmp(create_info.physical_device_query.required_extensions[i].extensionName, available_extensions[j].extensionName) == 0) {
                        if (available_extensions[j].specVersion < create_info.physical_device_query.required_extensions[i].specVersion) {
                            KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" has extension \"{}\" but with insufficient spec version (found {}, required {})", physical_device_properties.deviceName, create_info.physical_device_query.required_extensions[i].extensionName, available_extensions[j].specVersion, create_info.physical_device_query.required_extensions[i].specVersion);
                            satisfied = false;
                            break;
                        }

                        found = true;
                        break;
                    }
                }

                if (!found && satisfied) {
                    KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" is missing required extension \"{}\"", physical_device_properties.deviceName, create_info.physical_device_query.required_extensions[i].extensionName);
                    satisfied = false;
                    break;
                }
            }

            if (!satisfied) {
                continue;
            }

            for (uint32_t i = 0; i < create_info.physical_device_query.minimum_format_properties.size(); ++i) {
                VkFormatProperties format_properties;
                vkGetPhysicalDeviceFormatProperties(physical_device, create_info.physical_device_query.minimum_format_properties[i].format, &format_properties);
                if ((format_properties.linearTilingFeatures & create_info.physical_device_query.minimum_format_properties[i].minimum_properties.linearTilingFeatures) != create_info.physical_device_query.minimum_format_properties[i].minimum_properties.linearTilingFeatures ||
                    (format_properties.optimalTilingFeatures & create_info.physical_device_query.minimum_format_properties[i].minimum_properties.optimalTilingFeatures) != create_info.physical_device_query.minimum_format_properties[i].minimum_properties.optimalTilingFeatures ||
                    (format_properties.bufferFeatures & create_info.physical_device_query.minimum_format_properties[i].minimum_properties.bufferFeatures) != create_info.physical_device_query.minimum_format_properties[i].minimum_properties.bufferFeatures) {
                    KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum format properties for format {}", physical_device_properties.deviceName, create_info.physical_device_query.minimum_format_properties[i].format);
                    satisfied = false;
                    break;
                }
            }

            if (!satisfied) {
                continue;
            }

            for (uint32_t i = 0; i < create_info.physical_device_query.minimum_image_format_properties.size(); ++i) {
                VkImageFormatProperties image_format_properties;
                vk_result = vkGetPhysicalDeviceImageFormatProperties(physical_device,
                    create_info.physical_device_query.minimum_image_format_properties[i].format,
                    create_info.physical_device_query.minimum_image_format_properties[i].image_type,
                    create_info.physical_device_query.minimum_image_format_properties[i].tiling,
                    create_info.physical_device_query.minimum_image_format_properties[i].usage,
                    create_info.physical_device_query.minimum_image_format_properties[i].flags,
                    &image_format_properties);

                if (vk_result != VK_SUCCESS) {
                    KVK_ERR(vk_result, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not support image format {} with the specified type, tiling, usage, and flags", physical_device_properties.deviceName, create_info.physical_device_query.minimum_image_format_properties[i].format);
                    satisfied = false;
                    break;
                }

                if (image_format_properties.maxExtent.width < create_info.physical_device_query.minimum_image_format_properties[i].minimum_properties.maxExtent.width ||
                    image_format_properties.maxExtent.height < create_info.physical_device_query.minimum_image_format_properties[i].minimum_properties.maxExtent.height ||
                    image_format_properties.maxExtent.depth < create_info.physical_device_query.minimum_image_format_properties[i].minimum_properties.maxExtent.depth ||
                    image_format_properties.maxMipLevels < create_info.physical_device_query.minimum_image_format_properties[i].minimum_properties.maxMipLevels ||
                    image_format_properties.maxArrayLayers < create_info.physical_device_query.minimum_image_format_properties[i].minimum_properties.maxArrayLayers ||
                    image_format_properties.sampleCounts < create_info.physical_device_query.minimum_image_format_properties[i].minimum_properties.sampleCounts ||
                    image_format_properties.maxResourceSize < create_info.physical_device_query.minimum_image_format_properties[i].minimum_properties.maxResourceSize) {
                    KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum image format properties for format {}", physical_device_properties.deviceName, create_info.physical_device_query.minimum_image_format_properties[i].format);
                    satisfied = false;
                    break;
                }
            }

            if (!satisfied) {
                continue;
            }

            vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
            if (memory_properties.memoryTypeCount < create_info.physical_device_query.minimum_memory_properties.memoryTypeCount) {
                KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum memory type count", physical_device_properties.deviceName);
                continue;
            }

            if (memory_properties.memoryHeapCount < create_info.physical_device_query.minimum_memory_properties.memoryHeapCount) {
                KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum memory heap count", physical_device_properties.deviceName);
                continue;
            }

            uint32_t physical_device_queue_family_count;
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &physical_device_queue_family_count, nullptr);

            queue_family_properties_list.resize(physical_device_queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &physical_device_queue_family_count, queue_family_properties_list.data());

            for (uint32_t i = 0; i < create_info.physical_device_query.required_queues.size(); ++i) {
                bool found = false;
                for (uint32_t j = 0; j < physical_device_queue_family_count; ++j) {
                    if (queue_family_properties_list[j].queueCount >= create_info.physical_device_query.required_queues[i].properties.queueCount &&
                        (queue_family_properties_list[j].queueFlags & create_info.physical_device_query.required_queues[i].properties.queueFlags) == create_info.physical_device_query.required_queues[i].properties.queueFlags &&
                        queue_family_properties_list[j].minImageTransferGranularity.width >= create_info.physical_device_query.required_queues[i].properties.minImageTransferGranularity.width &&
                        queue_family_properties_list[j].minImageTransferGranularity.height >= create_info.physical_device_query.required_queues[i].properties.minImageTransferGranularity.height &&
                        queue_family_properties_list[j].minImageTransferGranularity.depth >= create_info.physical_device_query.required_queues[i].properties.minImageTransferGranularity.depth) {

                        queue_family_properties_list[j].queueCount = 0; // prevent re-use

                        if (create_info.vk_physical_device == nullptr) {
                            if (create_info.physical_device_query.required_queues[i].priorities.size() != create_info.physical_device_query.required_queues[i].properties.queueCount) {
                                KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, "{} device queues requested with only {} priority values; these must match", create_info.physical_device_query.required_queues[i].properties.queueCount, create_info.physical_device_query.required_queues[i].priorities.size());
                                return VK_ERROR_INITIALIZATION_FAILED;
                            }

                            VkDeviceQueueCreateInfo vk_device_queue_create_info = {
                                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                .pNext = nullptr,
                                .flags = create_info.physical_device_query.required_queues[i].create_flags,
                                .queueFamilyIndex = j,
                                .queueCount = create_info.physical_device_query.required_queues[i].properties.queueCount,
                                .pQueuePriorities = create_info.physical_device_query.required_queues[i].priorities.data(),
                            };

                            vk_device_queue_create_infos.push_back(vk_device_queue_create_info);
                        }

                        found = true;
                        break;
                    }
                }

                if (!found) {
                    KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, "Physical device \"{}\" does not satisfy minimum queue family properties at index {}", physical_device_properties.deviceName, i);
                    satisfied = false;
                    break;
                }
            }

            if (!satisfied) {
                continue;
            }

            vk_physical_device = physical_device;
            break;
        }

        if (!satisfied) {
            KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, "Failed to find a suitable Vulkan physical device for logical device creation");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    uint32_t queue_count = 0;
    for (VkDeviceQueueCreateInfo const& ci : vk_device_queue_create_infos) {
        queue_count += ci.queueCount;
    }

    if (queue_returns.size() < queue_count) {
        if (!queue_returns.resize(queue_count)) {
            KVK_ERR(VK_ERROR_INITIALIZATION_FAILED, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, "Failed to resize guest return array for device queues to size {}", vk_device_queue_create_infos.size());
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    VkFlags vk_flags = create_info.vk_flags;
    void* vk_pnext = create_info.vk_pnext;
    std::vector<const char*> enabled_extensions(create_info.vk_extensions);
    if (create_info.presets.enable_portability_subset) {
        enabled_extensions.push_back("VK_KHR_portability_subset");
    } else if (create_info.presets.recommended) {
#ifdef KVK_APPLE
        enabled_extensions.push_back("VK_KHR_portability_subset");
#endif
    }

    if (create_info.presets.recommended || create_info.presets.enable_swapchain) {
        enabled_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT vk_pageable_device_local_memory_features_ext = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT,
        .pNext = nullptr,
        .pageableDeviceLocalMemory = VK_TRUE,
    };

    VkPhysicalDeviceMemoryPriorityFeaturesEXT vk_memory_priority_features_ext = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT,
        .pNext = nullptr,
        .memoryPriority = VK_TRUE,
    };

    if (create_info.presets.recommended) {
        enabled_extensions.push_back(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
        vk_pageable_device_local_memory_features_ext.pNext = vk_pnext;
        vk_pnext = &vk_pageable_device_local_memory_features_ext;
    }

    if (create_info.presets.recommended) {
        enabled_extensions.push_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
        vk_memory_priority_features_ext.pNext = vk_pnext;
        vk_pnext = &vk_memory_priority_features_ext;
    }

    if (create_info.presets.enable_dynamic_rendering) {
        enabled_extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    }

    VkDeviceCreateInfo vk_device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = vk_pnext,
        .flags = vk_flags,
        .queueCreateInfoCount = static_cast<uint32_t>(vk_device_queue_create_infos.size()),
        .pQueueCreateInfos = vk_device_queue_create_infos.size() == 0 ? nullptr : vk_device_queue_create_infos.data(),
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size()),
        .ppEnabledExtensionNames = enabled_extensions.size() == 0 ? nullptr : enabled_extensions.data(),
        .pEnabledFeatures = &physical_device_features,
    };

    if (create_info.vk_physical_device != nullptr) {
        vk_device_create_info.queueCreateInfoCount = static_cast<uint32_t>(create_info.manual_selection.vk_queue_create_infos.size());
        vk_device_create_info.pQueueCreateInfos = create_info.manual_selection.vk_queue_create_infos.size() == 0 ? nullptr : create_info.manual_selection.vk_queue_create_infos.data();
    }

    vk_result = vkCreateDevice(vk_physical_device, &vk_device_create_info, nullptr, &vk_device);
    if (vk_result != VK_SUCCESS) {
        KVK_ERR(vk_result, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, "Failed to create Vulkan device");
        return vk_result;
    }

    uint32_t queue_index = 0;
    for (uint32_t i = 0; i < vk_device_queue_create_infos.size(); ++i) {
        for (uint32_t j = 0; j < vk_device_queue_create_infos[i].queueCount; ++j) {
            VkQueue vk_queue;
            vkGetDeviceQueue(vk_device, vk_device_queue_create_infos[i].queueFamilyIndex, j, &vk_queue);

            queue_returns[queue_index] = {
                .vk_queue = vk_queue,
                .family_index = vk_device_queue_create_infos[i].queueFamilyIndex,
                .request_index = i,
                .queue_index = j,
            };

            ++queue_index;
        }
    }

    return vk_result;
}

}