#include <iostream>
#include "kvk.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

struct Queue {
    VkQueue vk_queue;
    uint32_t family_index;
    uint32_t queue_index;
};

struct Queues {
    Queue graphics0_0;
    Queue transfer1_0;
    Queue compute2_0;
};

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* sdl_window = SDL_CreateWindow("kvk window", 1200, 900, SDL_WINDOW_VULKAN);
    if (sdl_window == nullptr) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    kvk::set_error_callback([](VkResult vk_result, VkDebugUtilsMessageSeverityFlagsEXT severity, const char* message, const char* function) {
        const char* severity_strings[4] = {
            "VERBOSE",
            "INFO",
            "WARNING",
            "ERROR",
        };

        uint32_t severity_index = 0;
        uint32_t severity_shifted = (uint32_t) severity;
        for (severity_index = 0; severity_index < 4; ++severity_index) {
            if (severity_shifted == 1) {
                break;
            }

            severity_shifted >>= 4;
        }

        std::cerr << "[" << function << "] (" << vk_result << ", " << severity_strings[severity_index] << ") : " << message << std::endl;
    });

    VkInstance vk_instance;
    if (kvk::create_instance({
        .app_name = "kvk demo",
        .app_version = VK_MAKE_VERSION(1, 0, 0),
        .vk_version = VK_MAKE_API_VERSION(0, 1, 2, 0),
        .vk_layers = {},
        .vk_extensions = {},
        .presets = {
            .recommended = true,
            .enable_surfaces = true,
            .enable_platform_specific_surfaces = true,
            .enable_validation_layers = true,
            .enable_debug_utils = true,

            .debug_messenger_callback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* pcallback_data, void* puser_data) -> VkBool32 {
                const char* severity_strings[4] = {
                    "VERBOSE",
                    "INFO",
                    "WARNING",
                    "ERROR",
                };

                const char* type_strings[4] = {
                    "GENERAL",
                    "VALIDATION",
                    "PERFORMANCE",
                    "DEVICE_ADDRESS_BINDING_EXT",
                };

                uint32_t severity_index = 0;
                uint32_t severity_shifted = (uint32_t)severity;
                for (severity_index = 0; severity_index < 4; ++severity_index) {
                    if (severity_shifted == 1) {
                        break;
                    }

                    severity_shifted >>= 4;
                }

                std::cout << "[vk] (" << severity_strings[severity_index];
                for (uint32_t i = 0; i < 4; ++i) {
                    if ((types & (1 << i)) != 0) {
                        std::cout << ", " << type_strings[i];
                    }
                }
                std::cout << "): " << pcallback_data->pMessage << std::endl;
                return false;
            },
        },
    }, vk_instance) != VK_SUCCESS) {
        return 1;
    }

    VkSurfaceKHR vk_surface;
    if (!SDL_Vulkan_CreateSurface(sdl_window, vk_instance, nullptr, &vk_surface)) {
        std::cerr << "SDL_Vulkan_CreateSurface Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    VkDevice vk_device;
    VkPhysicalDevice vk_physical_device;
    std::vector<kvk::DeviceQueueReturn> vk_device_queues;
    if (kvk::create_device(vk_instance, {
        .vk_extensions = {},
        .physical_device_query = {
            .minimum_vk_version = VK_MAKE_API_VERSION(0, 1, 2, 0),
            .excluded_device_types = kvk::PhysicalDeviceTypeFlags::CPU | kvk::PhysicalDeviceTypeFlags::VIRTUAL_GPU | kvk::PhysicalDeviceTypeFlags::OTHER,
            .minimum_features = {
                .shaderSampledImageArrayDynamicIndexing = true,
                .shaderStorageBufferArrayDynamicIndexing = true,
                .shaderStorageImageArrayDynamicIndexing = true,
            },
            .minimum_limits = {},
            .required_extensions = {
                {
                    .extensionName = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
                    .specVersion = 1,
                }
            },
            .minimum_format_properties = {
                {
                    .format = VK_FORMAT_B8G8R8A8_SRGB,
                    .minimum_properties = {
                        .optimalTilingFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT,
                    },
                },
                {
                    .format = VK_FORMAT_D32_SFLOAT,
                    .minimum_properties = {
                        .optimalTilingFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    },
                },
            },
            .minimum_image_format_properties = {},
            .minimum_memory_properties = {
                .memoryTypeCount = 1,
                .memoryTypes = {
                    {
                        .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    },
                    {
                        .propertyFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    },
                },
            },
            .required_queues = {
                {
                    .properties = {
                        .queueFlags = VK_QUEUE_GRAPHICS_BIT,
                        .queueCount = 1,
                    },
                    .priorities = {
                        1.0f,
                    },
                },
                {
                    .properties = {
                        .queueFlags = VK_QUEUE_TRANSFER_BIT,
                        .queueCount = 1,
                    },
                    .priorities = {
                        1.0f,
                    },
                },
                {
                    .properties = {
                        .queueFlags = VK_QUEUE_COMPUTE_BIT,
                        .queueCount = 1,
                    },
                    .priorities = {
                        1.0f,
                    },
                },
            },
        },
        .presets = {
            .recommended = true,
            .enable_swapchain = true,
            .enable_dynamic_rendering = true,
        },
    }, vk_physical_device, vk_device, vk_device_queues) != VK_SUCCESS) {
        if (vk_physical_device == nullptr) {
            std::cerr << "No suitable physical device found" << std::endl;
        }

        if (vk_device == nullptr) {
            std::cerr << "Failed to create Vulkan device" << std::endl;
        }

        return 1;
    }

    for (kvk::DeviceQueueReturn const& queue_return : vk_device_queues) {
        std::cout << "Got device queue " << queue_return.family_index << "." << queue_return.queue_index << " rq (" << queue_return.request_index << ")" << std::endl;
    }

    Queues queues = {
        .graphics0_0 = {
            .vk_queue = vk_device_queues[0].vk_queue,
            .family_index = vk_device_queues[0].family_index,
            .queue_index = vk_device_queues[0].queue_index,
        },
        .transfer1_0 = {
            .vk_queue = vk_device_queues[1].vk_queue,
            .family_index = vk_device_queues[1].family_index,
            .queue_index = vk_device_queues[1].queue_index,
        },
        .compute2_0 = {
            .vk_queue = vk_device_queues[2].vk_queue,
            .family_index = vk_device_queues[2].family_index,
            .queue_index = vk_device_queues[2].queue_index,
        },
    };
    
    bool running = true;
    SDL_Event sdl_event;
    while (running) {
        while (SDL_PollEvent(&sdl_event)) {
            if (sdl_event.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            }
        }
    }

    vkDestroyDevice(vk_device, nullptr);
    vkDestroySurfaceKHR(vk_instance, vk_surface, nullptr);
    vkDestroyInstance(vk_instance, nullptr);

    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
}
