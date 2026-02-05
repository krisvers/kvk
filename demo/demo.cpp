#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <filesystem>

#define KVK_USE_DXC
#include "kvk.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#define CELLULAR_AUTOMATA_GRID_WIDTH 256
#define CELLULAR_AUTOMATA_GRID_HEIGHT 256
#define CELLULAR_AUTOMATA_CELLS_PER_BYTE 2

struct Queue {
    VkQueue vk_queue;
    uint32_t family_index;
    uint32_t queue_index;
};

struct Queues {
    Queue compute0_0;
    Queue transfer1_0;
};

struct Uniforms {
    uint32_t x;
    uint32_t y;
    uint32_t v;
};

int main() {
    /* setup SDL3 and window */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        std::cerr << "SDL_Vulkan_LoadLibrary Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* sdl_window = SDL_CreateWindow("cellular automata", 1200, 900, SDL_WINDOW_VULKAN);
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

    /* setup instance */
    VkInstance vk_instance;
    if (kvk::create_instance({
        .app_name = "cellular automata",
        .app_version = VK_MAKE_VERSION(0, 1, 0),
        .vk_version = VK_MAKE_API_VERSION(0, 1, 2, 197),
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

    /* setup surface */
    VkSurfaceKHR vk_surface;
    if (!SDL_Vulkan_CreateSurface(sdl_window, vk_instance, nullptr, &vk_surface)) {
        std::cerr << "SDL_Vulkan_CreateSurface Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    /* setup device */
    VkDevice vk_device;
    VkPhysicalDevice vk_physical_device;
    std::vector<kvk::DeviceQueueReturn> vk_device_queues;
    if (kvk::create_device(vk_instance, {
        .vk_extensions = {},
        .physical_device_query = {
            .minimum_vk_version = VK_MAKE_API_VERSION(0, 1, 2, 197),
            .excluded_device_types = kvk::PhysicalDeviceTypeFlags::CPU | kvk::PhysicalDeviceTypeFlags::VIRTUAL_GPU | kvk::PhysicalDeviceTypeFlags::OTHER,
            .minimum_features = {
                .shaderSampledImageArrayDynamicIndexing = true,
                .shaderStorageBufferArrayDynamicIndexing = true,
                .shaderStorageImageArrayDynamicIndexing = true,
            },
            .minimum_limits = {},
            .required_extensions = {},
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
                        .queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
                        .queueCount = 1,
                    },
                    .surface_support = vk_surface,
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
        .compute0_0 = {
            .vk_queue = vk_device_queues[0].vk_queue,
            .family_index = vk_device_queues[0].family_index,
            .queue_index = vk_device_queues[0].queue_index,
        },
        .transfer1_0 = {
            .vk_queue = vk_device_queues[1].vk_queue,
            .family_index = vk_device_queues[1].family_index,
            .queue_index = vk_device_queues[1].queue_index,
        },
    };

    /* setup command pool and buffer */
    VkCommandPoolCreateInfo vk_compute_queue0_command_pool0_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queues.compute0_0.family_index,
    };

    VkCommandPool vk_compute_queue0_command_pool0;
    if (vkCreateCommandPool(vk_device, &vk_compute_queue0_command_pool0_create_info, nullptr, &vk_compute_queue0_command_pool0) != VK_SUCCESS) {
        std::cerr << "Failed to create compute queue 0 command pool 0" << std::endl;
        return 1;
    }

    VkCommandBufferAllocateInfo vk_compute_queue0_command_pool0_command_buffer0_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk_compute_queue0_command_pool0,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer vk_compute_queue0_command_pool0_command_buffer0;
    if (vkAllocateCommandBuffers(vk_device, &vk_compute_queue0_command_pool0_command_buffer0_allocate_info, &vk_compute_queue0_command_pool0_command_buffer0) != VK_SUCCESS) {
        std::cerr << "Failed to allocate compute queue 0 command pool 0 command buffer 0" << std::endl;
        return 1;
    }

    /* setup swapchain */
    std::vector<VkImage> vk_swapchain_backbuffers;
    kvk::SwapchainReturns swapchain_returns;
    if (kvk::create_swapchain(vk_device, {
        .vk_physical_device = vk_physical_device,
        .vk_surface = vk_surface,
        .vk_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .preferences = {
            {
                .image_count = 3,
                .layer_count = 1,
                .vk_surface_format = {
                    .format = VK_FORMAT_B8G8R8A8_SRGB,
                    .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                },
                .vk_present_mode = VK_PRESENT_MODE_MAILBOX_KHR,
            },
            {
                .image_count = 3,
                .layer_count = 1,
                .vk_surface_format = {
                    .format = VK_FORMAT_B8G8R8A8_SRGB,
                    .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                },
                .vk_present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR,
            },
            {
                .image_count = 2,
                .layer_count = 1,
                .vk_surface_format = {
                    .format = VK_FORMAT_B8G8R8A8_SRGB,
                    .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                },
                .vk_present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR,
            },
            {
                .image_count = 3,
                .layer_count = 1,
                .vk_surface_format = {
                    .format = VK_FORMAT_B8G8R8A8_SRGB,
                    .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                },
                .vk_present_mode = VK_PRESENT_MODE_FIFO_KHR,
            },
            {
                .image_count = 2,
                .layer_count = 1,
                .vk_surface_format = {
                    .format = VK_FORMAT_B8G8R8A8_SRGB,
                    .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                },
                .vk_present_mode = VK_PRESENT_MODE_FIFO_KHR,
            },
        },
        .vk_image_sharing_mode = VK_SHARING_MODE_EXCLUSIVE,
        .vk_queue_family_indices = {},
        .vk_pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .vk_composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .vk_clipped = VK_TRUE,
    }, swapchain_returns) != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan swapchain" << std::endl;
        return 1;
    }

    VkSwapchainKHR vk_swapchain = swapchain_returns.vk_swapchain;
    std::vector<VkImageView> vk_swapchain_backbuffer_views(vk_swapchain_backbuffers.size());
    for (uint32_t i = 0; i < vk_swapchain_backbuffers.size(); ++i) {
        VkImageViewCreateInfo vk_image_view_create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vk_swapchain_backbuffers[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_SRGB,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        if (vkCreateImageView(vk_device, &vk_image_view_create_info, nullptr, &vk_swapchain_backbuffer_views[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create Vulkan swapchain backbuffer image view at index " << i << std::endl;
            return 1;
        }
    }

    /* setup cellular automata resources and allocate heaps */
    VkBufferCreateInfo vk_cellular_automata_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = CELLULAR_AUTOMATA_GRID_WIDTH * CELLULAR_AUTOMATA_GRID_HEIGHT / CELLULAR_AUTOMATA_CELLS_PER_BYTE,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };

    VkBuffer vk_cellular_automata_buffer0, vk_cellular_automata_buffer1;
    if (vkCreateBuffer(vk_device, &vk_cellular_automata_buffer_create_info, nullptr, &vk_cellular_automata_buffer0) != VK_SUCCESS) {
        std::cerr << "Failed to create cellular automata buffer 0" << std::endl;
        return 1;
    }

    if (vkCreateBuffer(vk_device, &vk_cellular_automata_buffer_create_info, nullptr, &vk_cellular_automata_buffer1) != VK_SUCCESS) {
        std::cerr << "Failed to create cellular automata buffer 1" << std::endl;
        return 1;
    }

    VkImageCreateInfo vk_cellular_automata_render_image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_SRGB,
        .extent = {
            .width = CELLULAR_AUTOMATA_GRID_WIDTH,
            .height = CELLULAR_AUTOMATA_GRID_HEIGHT,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImage vk_cellular_automata_render_image;
    if (vkCreateImage(vk_device, &vk_cellular_automata_render_image_create_info, nullptr, &vk_cellular_automata_render_image) != VK_SUCCESS) {
        std::cerr << "Failed to create cellular automata render image" << std::endl;
        return 1;
    }

    kvk::resource::MonoAllocationHeap cellular_automata_heap;
    if (kvk::resource::mono_alloc_for_residents(vk_device, {
        .vk_physical_device = vk_physical_device,
        .vk_minimum_heap_size = 0,
        .vk_memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        .residents = {
            {
                .vk_buffer = vk_cellular_automata_buffer0,
            },
            {
                .vk_buffer = vk_cellular_automata_buffer1,
            },
            {
                .vk_image = vk_cellular_automata_render_image,
                .is_image = true,
            },
        },
    }, cellular_automata_heap) != VK_SUCCESS) {
        std::cerr << "Failed to create mono allocation for cellular automata buffers" << std::endl;
        return 1;
    }

    if (kvk::resource::mono_bind_residents(vk_device, cellular_automata_heap) != VK_SUCCESS) {
        std::cerr << "Failed to bind cellular automata resources to heap" << std::endl;
        return 1;
    }

    /* setup uniform resources and allocate heap */
    VkBufferCreateInfo vk_uniform_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(Uniforms),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    };

    VkBuffer vk_uniform_buffer;
    if (vkCreateBuffer(vk_device, &vk_uniform_buffer_create_info, nullptr, &vk_uniform_buffer) != VK_SUCCESS) {
        std::cerr << "Failed to create uniform buffer" << std::endl;
        return 1;
    }

    kvk::resource::MonoAllocationHeap uniform_heap;
    if (kvk::resource::mono_alloc_for_residents(vk_device, {
        .vk_physical_device = vk_physical_device,
        .vk_minimum_heap_size = 0,
        .vk_memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        .residents = {
            {
                .vk_buffer = vk_uniform_buffer,
            },
        },
    }, uniform_heap) != VK_SUCCESS) {
        std::cerr << "Failed to allocate uniform heap" << std::endl;
        return 1;
    }

    if (kvk::resource::mono_bind_residents(vk_device, uniform_heap) != VK_SUCCESS) {
        std::cerr << "Failed to bind uniform resources to heap" << std::endl;
        return 1;
    }

    /* prepare for pipeline creation */
    VkDescriptorSetLayoutBinding vk_compute_pass0_0_descriptor_set_layout_bindings[4] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo vk_compute_pass0_0_descriptor_set_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = sizeof(vk_compute_pass0_0_descriptor_set_layout_bindings) / sizeof(VkDescriptorSetLayoutBinding),
        .pBindings = &vk_compute_pass0_0_descriptor_set_layout_bindings[0],
    };

    VkDescriptorSetLayout vk_compute_pass0_0_descriptor_set_layout;
    if (vkCreateDescriptorSetLayout(vk_device, &vk_compute_pass0_0_descriptor_set_layout_create_info, nullptr, &vk_compute_pass0_0_descriptor_set_layout) != VK_SUCCESS) {
        std::cerr << "Failed to create compute pass 0.0 descriptor set layout" << std::endl;
        return 1;
    }

    VkPipelineLayoutCreateInfo vk_compute_pass0_0_pipeline_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &vk_compute_pass0_0_descriptor_set_layout,
    };

    VkPipelineLayout vk_compute_pass0_0_pipeline_layout;
    if (vkCreatePipelineLayout(vk_device, &vk_compute_pass0_0_pipeline_layout_create_info, nullptr, &vk_compute_pass0_0_pipeline_layout) != VK_SUCCESS) {
        std::cerr << "Failed to create compute pass 0.0 pipeline layout" << std::endl;
        return 1;
    }

    /* load and compile shader */
    std::vector<uint32_t> compute0_0_shader_spv(std::filesystem::file_size("cellular_automata.hlsl.bin") / sizeof(uint32_t));
    {
        std::ifstream in("cellular_automata.hlsl.bin");
        if (!in.good()) {
            std::cerr << "Failed to open cellular_automata.hlsl.bin" << std::endl;
            return 1;
        }

        in.read(reinterpret_cast<char*>(&compute0_0_shader_spv[0]), compute0_0_shader_spv.size() * sizeof(uint32_t));
        in.close();
    }

    VkShaderModuleCreateInfo vk_compute_pass0_0_shader_module_create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = compute0_0_shader_spv.size() * sizeof(uint32_t),
        .pCode = compute0_0_shader_spv.data(),
    };

    VkShaderModule vk_compute_pass0_0_shader_module;
    if (vkCreateShaderModule(vk_device, &vk_compute_pass0_0_shader_module_create_info, nullptr, &vk_compute_pass0_0_shader_module) != VK_SUCCESS) {
        std::cerr << "Failed to create compute pass 0.0 shader module" << std::endl;
        return 1;
    }

    /* setup compute pass 0.0 pipeline */
    VkComputePipelineCreateInfo vk_compute_pass0_0_pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = vk_compute_pass0_0_shader_module,
            .pName = "cellular_automata",
        },
        .layout = vk_compute_pass0_0_pipeline_layout,
    };

    VkPipeline vk_compute_pass0_0_pipeline;
    if (vkCreateComputePipelines(vk_device, nullptr, 1, &vk_compute_pass0_0_pipeline_create_info, nullptr, &vk_compute_pass0_0_pipeline) != VK_SUCCESS) {
        std::cerr << "Failed to create compute pass 0.0 pipeline" << std::endl;
        return 1;
    }

    /* setup descriptor sets */

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

    /* cleanup compute pass 0.0 pipeline and shaders */
    vkDestroyPipeline(vk_device, vk_compute_pass0_0_pipeline, nullptr);
    vkDestroyShaderModule(vk_device, vk_compute_pass0_0_shader_module, nullptr);

    /* cleanup compute pass 0.0 layouts */
    vkDestroyPipelineLayout(vk_device, vk_compute_pass0_0_pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(vk_device, vk_compute_pass0_0_descriptor_set_layout, nullptr);

    /* cleanup uniform resources and free heap */
    vkDestroyBuffer(vk_device, vk_uniform_buffer, nullptr);
    kvk::resource::mono_free_heap(vk_device, uniform_heap);

    /* cleanup cellular automata resources and free heap */
    vkDestroyImage(vk_device, vk_cellular_automata_render_image, nullptr);
    vkDestroyBuffer(vk_device, vk_cellular_automata_buffer1, nullptr);
    vkDestroyBuffer(vk_device, vk_cellular_automata_buffer0, nullptr);
    kvk::resource::mono_free_heap(vk_device, cellular_automata_heap);

    /* cleanup swapchain */
    for (uint32_t i = 0; i < vk_swapchain_backbuffer_views.size(); ++i) {
        vkDestroyImageView(vk_device, vk_swapchain_backbuffer_views[i], nullptr);
    }
    vkDestroySwapchainKHR(vk_device, vk_swapchain, nullptr);

    /* cleanup command pool and buffers */
    vkFreeCommandBuffers(vk_device, vk_compute_queue0_command_pool0, 1, &vk_compute_queue0_command_pool0_command_buffer0);
    vkDestroyCommandPool(vk_device, vk_compute_queue0_command_pool0, nullptr);

    /* cleanup device, instance and surface */
    vkDestroyDevice(vk_device, nullptr);
    vkDestroySurfaceKHR(vk_instance, vk_surface, nullptr);
    vkDestroyInstance(vk_instance, nullptr);

    /* cleanup SDL3 */
    SDL_Vulkan_UnloadLibrary();
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
}
