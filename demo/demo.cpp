#include <iostream>
#include <sstream>
#include <fstream>
#include <iterator>
#include <vector>
#include <string>
#include <stdexcept>
#include <filesystem>

#include "kvk.h"

#define VK_ONLY_EXPORTED_PROTOTYPES
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

#define CELLULAR_AUTOMATA_GRID_WIDTH 512
#define CELLULAR_AUTOMATA_GRID_HEIGHT 256
#define WINDOW_WIDTH 1664
#define WINDOW_HEIGHT 832
#define CELLULAR_AUTOMATA_BYTES_PER_CELL 8

#define EMBED_SHADER

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
    uint32_t tick;
    uint32_t bytes_per_cell;
    uint32_t width;
    uint32_t height;
    uint32_t visual_mode;
    uint32_t conditions;
};

class IPass {
public:
    IPass() = default;

    virtual void set_swapchain_backbuffer(VkImage vk_backbuffer, VkImageView vk_backbuffer_view, VkExtent2D vk_extent, VkFormat vk_format) = 0;

    virtual bool pre_transition(VkCommandBuffer vk_command_buffer) = 0;
    virtual bool bind(VkCommandBuffer vk_command_buffer) = 0;
    virtual bool execute(VkCommandBuffer vk_command_buffer) = 0;
    virtual bool post_transition(VkCommandBuffer vk_command_buffer) = 0;

    virtual VkSemaphore finished_semaphore() = 0;
    virtual VkFence finished_fence() = 0;
};

class ComputePass0_0 : public IPass {
private:
    VkDevice _device;
    VkDescriptorSetLayout _set_layout;
    VkDescriptorPool _descriptor_pool;
    VkDescriptorSet _descriptor_set;
    VkPipelineLayout _compute_pipeline_layout;
    VkPipeline _compute_pipeline;
    VkFence _finished_fence;

    VkImage _backbuffer;
    VkImageView _backbuffer_view;
    VkExtent2D _backbuffer_extent;
    VkFormat _backbuffer_format;

public:
    ComputePass0_0(VkDevice vk_device) : _device(vk_device) {
        VkDescriptorSetLayoutBinding descriptor_set_layout_bindings[4] = {
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
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = 3,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        };

        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = sizeof(descriptor_set_layout_bindings) / sizeof(VkDescriptorSetLayoutBinding),
            .pBindings = &descriptor_set_layout_bindings[0],
        };

        if (vkCreateDescriptorSetLayout(_device, &descriptor_set_layout_create_info, nullptr, &_set_layout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create compute pass 0.0 descriptor set layout");
        }

        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &_set_layout,
        };

        if (vkCreatePipelineLayout(_device, &pipeline_layout_create_info, nullptr, &_compute_pipeline_layout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create compute pass 0.0 pipeline layout");
        }

        /* load and compile shader */
#ifndef EMBED_SHADER
        std::vector<uint8_t> shader_spv(std::filesystem::file_size("cellular_automata.bin"));
        {
            std::ifstream in("cellular_automata.bin", std::ios::binary);
            if (!in.good()) {
                std::runtime_error("Failed to open cellular_automata.bin");
            }

            in.read(reinterpret_cast<char*>(&shader_spv[0]), shader_spv.size());
            in.close();
        }
#else
        std::vector<uint8_t> shader_spv = {
            #include "cellular_automata.bin.h"
        };
#endif

        VkShaderModuleCreateInfo shader_module_create_info = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = shader_spv.size(),
            .pCode = reinterpret_cast<uint32_t const*>(shader_spv.data()),
        };

        VkShaderModule shader_module;
        if (vkCreateShaderModule(_device, &shader_module_create_info, nullptr, &shader_module) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create compute pass 0.0 shader module");
        }

        /* setup compute pass 0.0 pipeline */
        VkComputePipelineCreateInfo pipeline_create_info = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = shader_module,
                .pName = "cellular_automata",
            },
            .layout = _compute_pipeline_layout,
        };

        if (vkCreateComputePipelines(_device, nullptr, 1, &pipeline_create_info, nullptr, &_compute_pipeline) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create compute pass 0.0 pipeline");
        }

        vkDestroyShaderModule(_device, shader_module, nullptr);

        /* setup descriptor sets */
        VkDescriptorPoolSize descriptor_pool_sizes[3] = {
            {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 2,
            },
            {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
            },
            {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
            },
        };

        VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = sizeof(descriptor_pool_sizes) / sizeof(VkDescriptorPoolSize),
            .pPoolSizes = &descriptor_pool_sizes[0],
        };

        if (vkCreateDescriptorPool(_device, &descriptor_pool_create_info, nullptr, &_descriptor_pool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create compute pass 0.0 descriptor pool 0");
        }

        VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = _descriptor_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &_set_layout,
        };

        if (vkAllocateDescriptorSets(_device, &descriptor_set_allocate_info, &_descriptor_set) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate compute pass 0.0 descriptor pool 0 set 0");
        }

        /* setup synchronization primitives */
        VkFenceCreateInfo finished_fence_create_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        if (vkCreateFence(_device, &finished_fence_create_info, nullptr, &_finished_fence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create compute pass 0.0 finished fence");
        }
    }

    ~ComputePass0_0() {
        vkDestroyFence(_device, _finished_fence, nullptr);
        vkDestroyDescriptorPool(_device, _descriptor_pool, nullptr);
        vkDestroyPipeline(_device, _compute_pipeline, nullptr);
        vkDestroyPipelineLayout(_device, _compute_pipeline_layout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _set_layout, nullptr);
    }

    void set_swapchain_backbuffer(VkImage vk_backbuffer, VkImageView vk_backbuffer_view, VkExtent2D vk_extent, VkFormat vk_format) override {
        _backbuffer = vk_backbuffer;
        _backbuffer_view = vk_backbuffer_view;
        _backbuffer_extent = vk_extent;
        _backbuffer_format = vk_format;
    }

    bool update_descriptor_sets(kvk::resource::MonoAllocationResident const& input_buffer_resident, kvk::resource::MonoAllocationResident const& output_buffer_resident, kvk::resource::MonoAllocationResident const& uniform_buffer_resident, VkImageView vk_render_image_view) {
        VkDescriptorBufferInfo binding0_buffer_info = {
            .buffer = input_buffer_resident.id.vk_buffer,
            .offset = 0,
            .range = input_buffer_resident.vk_size,
        };

        VkDescriptorBufferInfo binding1_buffer_info = {
            .buffer = output_buffer_resident.id.vk_buffer,
            .offset = 0,
            .range = output_buffer_resident.vk_size,
        };

        VkDescriptorBufferInfo binding2_buffer_info = {
            .buffer = uniform_buffer_resident.id.vk_buffer,
            .offset = 0,
            .range = sizeof(Uniforms),
        };

        VkDescriptorImageInfo binding3_image_info = {
            .sampler = nullptr,
            .imageView = vk_render_image_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };

        VkWriteDescriptorSet descriptor_writes[4] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = _descriptor_set,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &binding0_buffer_info,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = _descriptor_set,
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &binding1_buffer_info,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = _descriptor_set,
                .dstBinding = 2,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &binding2_buffer_info,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = _descriptor_set,
                .dstBinding = 3,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &binding3_image_info,
            },
        };

        vkUpdateDescriptorSets(_device, sizeof(descriptor_writes) / sizeof(VkWriteDescriptorSet), &descriptor_writes[0], 0, nullptr);
        return true;
    }

    bool pre_transition(VkCommandBuffer vk_command_buffer) override {
        
    }
};

int main(int argc, char** argv) {
    uint32_t width = CELLULAR_AUTOMATA_GRID_WIDTH;
    uint32_t height = CELLULAR_AUTOMATA_GRID_HEIGHT;
    uint32_t window_width = WINDOW_WIDTH;
    uint32_t window_height = WINDOW_HEIGHT;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--grid") == 0) {
            if (i == argc - 1) {
                std::cout << "Provide one more argument: \"[grid width]x[grid height]\"" << std::endl;
                return 1;
            }

            const char* x = std::strchr(argv[i + 1], 'x');
            if (x == nullptr) {
                std::cout << "Invalid argument: \"" << argv[i + 1] << "\"; should be: \"[grid width]x[grid height]\"" << std::endl;
                return 1;
            }

            int w = std::stoi(std::string(argv[i + 1], x - argv[i + 1]));
            int h = std::atoi(x + 1);
            width = static_cast<uint32_t>(w);
            height = static_cast<uint32_t>(h);
        } else if (std::strcmp(argv[i], "--window") == 0) {
            if (i == argc - 1) {
                std::cout << "Provide one more argument: \"[window width]x[window height]\"" << std::endl;
                return 1;
            }

            const char* x = std::strchr(argv[i + 1], 'x');
            if (x == nullptr) {
                std::cout << "Invalid argument: \"" << argv[i + 1] << "\"; should be: \"[window width]x[window height]\"" << std::endl;
                return 1;
            }

            int w = std::stoi(std::string(argv[i + 1], x - argv[i + 1]));
            int h = std::atoi(x + 1);
            window_width = static_cast<uint32_t>(w);
            window_height = static_cast<uint32_t>(h);
        }
    }

    /* setup SDL3 and window */
    SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "1");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        std::cerr << "SDL_Vulkan_LoadLibrary Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* sdl_window = SDL_CreateWindow("cellular automata", window_width, window_height, SDL_WINDOW_VULKAN);
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
    VkPhysicalDeviceVulkan12Features vk_physical_device_vulkan12_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .shaderStorageBufferArrayNonUniformIndexing = true,
        .shaderStorageImageArrayNonUniformIndexing = true,
    };

    VkDevice vk_device;
    VkPhysicalDevice vk_physical_device;
    std::vector<kvk::DeviceQueueReturn> vk_device_queues;
    if (kvk::create_device(vk_instance, {
        .vk_pnext = &vk_physical_device_vulkan12_features,
        .vk_extensions = {},
        .physical_device_query = {
            .minimum_vk_version = VK_MAKE_API_VERSION(0, 1, 2, 197),
            .excluded_device_types = kvk::PhysicalDeviceTypeFlags::CPU | kvk::PhysicalDeviceTypeFlags::VIRTUAL_GPU | kvk::PhysicalDeviceTypeFlags::OTHER,
            .minimum_features = {
                .shaderStorageBufferArrayDynamicIndexing = true,
                .shaderStorageImageArrayDynamicIndexing = true,
                .shaderInt64 = true,
            },
            .minimum_limits = {},
            .required_extensions = {},
            .minimum_format_properties = {
                {
                    .format = VK_FORMAT_B8G8R8A8_SRGB,
                    .minimum_properties = {
                        .optimalTilingFeatures = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT,
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
    kvk::SwapchainReturns swapchain_returns = {
        .vk_backbuffers = vk_swapchain_backbuffers,
    };

    std::vector<kvk::SwapchainPreference> swapchain_preferences = {
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
    };

    if (kvk::create_swapchain(vk_device, {
        .vk_physical_device = vk_physical_device,
        .vk_surface = vk_surface,
        .vk_image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preferences = swapchain_preferences,
        .vk_image_sharing_mode = VK_SHARING_MODE_EXCLUSIVE,
        .vk_queue_family_indices = {},
        .vk_pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .vk_composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .vk_clipped = VK_TRUE,
    }, swapchain_returns) != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan swapchain" << std::endl;
        return 1;
    }

    kvk::SwapchainPreference const& swapchain_preference = swapchain_preferences[swapchain_returns.chosen_preference];

    VkSwapchainKHR vk_swapchain = swapchain_returns.vk_swapchain;
    std::vector<VkImageView> vk_swapchain_backbuffer_views(vk_swapchain_backbuffers.size());
    for (uint32_t i = 0; i < vk_swapchain_backbuffers.size(); ++i) {
        VkImageViewCreateInfo vk_image_view_create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vk_swapchain_backbuffers[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchain_preference.vk_surface_format.format,
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

    /* setup swapchain synchronization primitives */
    VkSemaphoreCreateInfo vk_swapchain_image_acquisition_semaphore_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkSemaphore vk_swapchain_image_acquisition_semaphore;
    if (vkCreateSemaphore(vk_device, &vk_swapchain_image_acquisition_semaphore_create_info, nullptr, &vk_swapchain_image_acquisition_semaphore) != VK_SUCCESS) {
        std::cerr << "Failed to create swapchain image acquisition semaphore" << std::endl;
        return 1;
    }

    std::vector<VkSemaphore> vk_swapchain_image_finished_semaphores(vk_swapchain_backbuffers.size());
    for (size_t i = 0; i < vk_swapchain_backbuffers.size(); ++i) {
        VkSemaphoreCreateInfo vk_swapchain_image_finished_semaphore_create_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        if (vkCreateSemaphore(vk_device, &vk_swapchain_image_finished_semaphore_create_info, nullptr, &vk_swapchain_image_finished_semaphores[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create swapchain image finished semaphore " << i << std::endl;
            return 1;
        }
    }

    /* setup cellular automata resources and allocate heaps */
    VkBufferCreateInfo vk_cellular_automata_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = width * height * CELLULAR_AUTOMATA_BYTES_PER_CELL,
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
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {
            .width = width,
            .height = height,
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

    kvk::resource::MonoAllocationResident const& cellular_automata_buffer0_resident = cellular_automata_heap.residents[{ .vk_buffer = vk_cellular_automata_buffer0 }];
    kvk::resource::MonoAllocationResident const& cellular_automata_buffer1_resident = cellular_automata_heap.residents[{ .vk_buffer = vk_cellular_automata_buffer1 }];

    /* setup render image view */
    VkImageViewCreateInfo vk_cellular_automata_render_image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk_cellular_automata_render_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
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

    VkImageView vk_cellular_automata_render_image_view;
    if (vkCreateImageView(vk_device, &vk_cellular_automata_render_image_view_create_info, nullptr, &vk_cellular_automata_render_image_view) != VK_SUCCESS) {
        std::cerr << "Failed to create cellular automata render image view" << std::endl;
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

    kvk::resource::MonoAllocationResident const& uniform_buffer_resident = uniform_heap.residents[{ .vk_buffer = vk_uniform_buffer }];

    Uniforms* p_uniforms;
    if (vkMapMemory(vk_device, uniform_heap.vk_heap_memory, uniform_buffer_resident.vk_heap_offset, sizeof(Uniforms), 0, reinterpret_cast<void**>(&p_uniforms)) != VK_SUCCESS) {
        std::cerr << "Failed to map uniform heap memory for uniform buffer" << std::endl;
        return 1;
    }

    /* prepare for pipeline creation */
    VkBuffer vk_cellular_automata_input_buffer = vk_cellular_automata_buffer1;
    VkBuffer vk_cellular_automata_output_buffer = vk_cellular_automata_buffer0;

    ImGuiContext* imgui_context = ImGui::CreateContext();
    if (imgui_context == nullptr) {
        std::cerr << "Failed to create ImGui context" << std::endl;
        return 1;
    }

    ImGuiIO& imgui_io = ImGui::GetIO();
    imgui_io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    if (!ImGui_ImplSDL3_InitForVulkan(sdl_window)) {
        std::cerr << "Failed to init ImGui for Vulkan using SDL window" << std::endl;
        return 1;
    }

    ImGui_ImplVulkan_InitInfo imgui_vulkan_init_info = {
        .ApiVersion = VK_MAKE_API_VERSION(0, 1, 2, 197),
        .Instance = vk_instance,
        .PhysicalDevice = vk_physical_device,
        .Device = vk_device,
        .QueueFamily = queues.compute0_0.family_index,
        .Queue = queues.compute0_0.vk_queue,
        .DescriptorPool = nullptr,
        .DescriptorPoolSize = 1024,
        .MinImageCount = swapchain_preference.image_count,
        .ImageCount = swapchain_preference.image_count,
        .PipelineInfoMain = {
            .RenderPass = nullptr,
            .Subpass = 0,
            .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
            .PipelineRenderingCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
                .colorAttachmentCount = 1,
                .pColorAttachmentFormats = &swapchain_preference.vk_surface_format.format,
                .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
                .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
            },
        },
        .UseDynamicRendering = true,
        .Allocator = nullptr,
        .MinAllocationSize = 1024 * 1024,
    };

    if (!ImGui_ImplVulkan_Init(&imgui_vulkan_init_info)) {
        std::cerr << "Failed to init ImGui Vulkan" << std::endl;
        return 1;
    }

    int32_t view_x = 0;
    int32_t view_y = 0;
    float mouse_x = 0;
    float mouse_y = 0;
    float zoom = 1.0f;

    float frames_per_tick = 1.0f;
    uint32_t tick = 0;
    uint32_t frame = 0;
    uint32_t game_tick = 0;
    bool running = true;
    bool left_click = false;
    bool right_click = false;
    bool paused = false;
    bool failed_swapchain = false;
    bool minimized = false;

    SDL_DisplayMode const* sdl_display_mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(sdl_window));
    if (sdl_display_mode != nullptr) {
        frames_per_tick = sdl_display_mode->refresh_rate / 60.0f * 1.0f;
    }

    PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetInstanceProcAddr(vk_instance, "vkCmdBeginRenderingKHR"));
    PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHR = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetInstanceProcAddr(vk_instance, "vkCmdEndRenderingKHR"));

    SDL_Event sdl_event;
    int live_count[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    p_uniforms->visual_mode = 8;
    p_uniforms->v = 0x01000080;
    while (running) {
        bool compute = false;
        bool render = false;
        bool advance = false;
        bool reverse = false;
        frames_per_tick = std::max(std::floor(frames_per_tick * 5.0f) / 5.0f, 0.2f);

        if (frames_per_tick >= 1.0f) {
            ++frame;
            render = true;
            if (frame % static_cast<uint32_t>(frames_per_tick) == 0) {
                compute = true;
                ++tick;
            }
        } else {
            ++tick;
            compute = true;
            if (tick % std::max(static_cast<uint32_t>(1.0f / frames_per_tick), 0u) == 0) {
                render = true;
                ++frame;
            }
        }

        if (frame == 1 || tick == 1) {
            p_uniforms->v = 0x01000000;
        }

        while (SDL_PollEvent(&sdl_event)) {
            ImGui_ImplSDL3_ProcessEvent(&sdl_event);

            switch (sdl_event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_WINDOW_MOUSE_ENTER:
                    SDL_HideCursor();
                    break;
                case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                    SDL_ShowCursor();
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (imgui_io.WantCaptureMouse) {
                        break;
                    }

                    if (sdl_event.button.button == SDL_BUTTON_LEFT) {
                        left_click = sdl_event.button.down;
                        if (left_click) {
                            p_uniforms->v = (p_uniforms->v & ~0x1f) | 0x18;
                        } else {
                            p_uniforms->v &= ~0x1f;
                        }
                    } else if (sdl_event.button.button == SDL_BUTTON_RIGHT) {
                        right_click = sdl_event.button.down;
                        if (right_click) {
                            p_uniforms->v = (p_uniforms->v & ~0x100) | 0x100;
                        } else {
                            p_uniforms->v &= ~0x100;
                        }
                    }
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (imgui_io.WantCaptureKeyboard) {
                        break;
                    }

                    switch (sdl_event.key.key) {
                        case SDLK_R:
                            p_uniforms->v = (p_uniforms->v & ~0x80) | 0x80;
                            break;
                        case SDLK_SPACE:
                            if (!sdl_event.key.repeat) {
                                paused = !paused;
                                p_uniforms->v = (p_uniforms->v & ~0x400) | (!paused ? 0x400 : 0x00);
                            }
                            break;
                        case SDLK_0:
                        case SDLK_1:
                        case SDLK_2:
                        case SDLK_3:
                        case SDLK_4:
                        case SDLK_5:
                        case SDLK_6:
                        case SDLK_7:
                        case SDLK_8:
                        case SDLK_9:
                            if (!sdl_event.key.repeat) {
                                p_uniforms->visual_mode = sdl_event.key.key - SDLK_0;
                            }
                            break;
                        case SDLK_LEFT:
                            reverse = true;
                            compute = true;
                            break;
                        case SDLK_RIGHT:
                            advance = true;
                            compute = true;
                            break;
                        case SDLK_UP:
                            if (frames_per_tick < 1.0f) {
                                frames_per_tick -= 0.2f;
                            } else {
                                frames_per_tick -= 1.0f;
                            }
                            break;
                        case SDLK_DOWN:
                            if (!sdl_event.key.repeat) {
                                if (frames_per_tick < 1.0f) {
                                    frames_per_tick += 0.2f;
                                } else {
                                    frames_per_tick += 1.0f;
                                }
                            }
                            break;
                        case SDLK_EQUALS:
                            zoom *= 1.15f;
                            break;
                        case SDLK_MINUS:
                            zoom /= 1.15f;
                            break;
                        default:
                            break;
                    }
                    break;
                case SDL_EVENT_KEY_UP:
                    if (imgui_io.WantCaptureKeyboard) {
                        break;
                    }

                    switch (sdl_event.key.key) {
                        case SDLK_R:
                            p_uniforms->v &= ~0x80;
                            break;
                        default:
                            break;
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    mouse_x = sdl_event.motion.x;
                    mouse_y = sdl_event.motion.y;
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    view_x += sdl_event.wheel.x;
                    view_y -= sdl_event.wheel.y;
                    p_uniforms->v = (p_uniforms->v & 0xffffff) | (std::min(255u, static_cast<uint32_t>(std::max(1, static_cast<int32_t>((((p_uniforms->v >> 24) + ((sdl_event.wheel.integer_y > 0) ? 1 : -1))))))) << 24);
                    break;
                case SDL_EVENT_WINDOW_MINIMIZED:
                    minimized = true;
                    break;
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    minimized = false;
                    break;
            }

            if (!running) {
                break;
            }
        }

        if (minimized) {
            render = false;
        }

        p_uniforms->v = (p_uniforms->v & ~0x23f) | ((!(advance || reverse) && (paused || !compute)) ? 0x20 : 0x00) | (reverse ? 0x200 : 0x000) | (left_click ? 0x58 : 0x00) | (right_click ? 0x100 : 0x00);
        p_uniforms->x = mouse_x * static_cast<float>(width) / window_width;
        p_uniforms->y = mouse_y * static_cast<float>(height) / window_height;
        p_uniforms->tick = game_tick;
        p_uniforms->bytes_per_cell = CELLULAR_AUTOMATA_BYTES_PER_CELL;
        p_uniforms->width = width;
        p_uniforms->height = height;

        if (!failed_swapchain) {
            if (vkWaitForFences(vk_device, 1, &vk_compute_pass0_0_finished_fence, false, std::numeric_limits<uint64_t>::max()) != VK_SUCCESS) {
                std::cerr << "Failed to wait for compute pass 0.0 finished fence" << std::endl;
                return 1;
            }

            if (vkResetFences(vk_device, 1, &vk_compute_pass0_0_finished_fence) != VK_SUCCESS) {
                std::cerr << "Failed to reset compute pass 0.0 finished fence" << std::endl;
                return 1;
            }
        }

        failed_swapchain = false;

        /* acquire swapchain image */
        uint32_t vk_swapchain_backbuffer_index = 0;
        if (render) {
            if (vkAcquireNextImageKHR(vk_device, vk_swapchain, std::numeric_limits<uint64_t>::max(), vk_swapchain_image_acquisition_semaphore, nullptr, &vk_swapchain_backbuffer_index) != VK_SUCCESS) {
                std::cerr << "Failed to acquire swapchain image index" << std::endl;
                failed_swapchain = true;
                continue;
            }
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Instruction");
        ImGui::Text("To play, draw on the cells using the mouse");
        ImGui::Text("Left click to draw, right click to erase");
        ImGui::Text("Scroll up to increase cursor size, down to decrease cursor size");
        ImGui::Text("Space to pause simulation, right arrow to advance, left arrow to reverse");
        ImGui::Text("Press R to restart");
        ImGui::Text("");
        ImGui::Text("Play around with the rule set");
        ImGui::Text("If you get lost, you can always use a preset");
        ImGui::End();

        ImGui::Begin("Rule Set");
        ImGui::Text("Behavior when certain neighbor count:");
        ImGui::Text("(Note):");
        ImGui::Text(" -1 means cell always dies");
        ImGui::Text("  0 means cell continues with previous state");
        ImGui::Text("  1 means cell always lives");

        if (ImGui::SmallButton("Load Preset: \"Conway's Game of Life\"")) {
            live_count[0] = -1;
            live_count[1] = -1;
            live_count[2] =  0;
            live_count[3] =  1;
            live_count[4] = -1;
            live_count[5] = -1;
            live_count[6] = -1;
            live_count[7] = -1;
            live_count[8] = -1;
        }

        if (ImGui::SmallButton("Load Preset: \"High Life\"")) {
            live_count[0] = -1;
            live_count[1] = -1;
            live_count[2] =  0;
            live_count[3] =  1;
            live_count[4] = -1;
            live_count[5] = -1;
            live_count[6] =  1;
            live_count[7] = -1;
            live_count[8] = -1;
        }

        ImGui::SliderInt("0 neighbors", &live_count[0], -1, 1);
        ImGui::SliderInt("1 neighbors", &live_count[1], -1, 1);
        ImGui::SliderInt("2 neighbors", &live_count[2], -1, 1);
        ImGui::SliderInt("3 neighbors", &live_count[3], -1, 1);
        ImGui::SliderInt("4 neighbors", &live_count[4], -1, 1);
        ImGui::SliderInt("5 neighbors", &live_count[5], -1, 1);
        ImGui::SliderInt("6 neighbors", &live_count[6], -1, 1);
        ImGui::SliderInt("7 neighbors", &live_count[7], -1, 1);
        ImGui::SliderInt("8 neighbors", &live_count[8], -1, 1);
        ImGui::End();

        p_uniforms->conditions =
            ((live_count[0] + 1)) |
            ((live_count[1] + 1) << 2) |
            ((live_count[2] + 1) << 4) |
            ((live_count[3] + 1) << 6) |
            ((live_count[4] + 1) << 8) |
            ((live_count[5] + 1) << 10) |
            ((live_count[6] + 1) << 12) |
            ((live_count[7] + 1) << 14) |
            ((live_count[8] + 1) << 16);

        std::cout << std::hex << p_uniforms->conditions << std::endl;

        ImGui::Render();

        /* prepare for compute pass 0.0 */
        {
            VkBuffer vk_tmp_buffer = vk_cellular_automata_input_buffer;
            vk_cellular_automata_input_buffer = vk_cellular_automata_output_buffer;
            vk_cellular_automata_output_buffer = vk_tmp_buffer;
        }

        kvk::resource::MonoAllocationResident const& cellular_automata_input_buffer_resident = cellular_automata_heap.residents[{ .vk_buffer = vk_cellular_automata_input_buffer }];
        kvk::resource::MonoAllocationResident const& cellular_automata_output_buffer_resident = cellular_automata_heap.residents[{ .vk_buffer = vk_cellular_automata_output_buffer }];

        if (vkResetCommandPool(vk_device, vk_compute_queue0_command_pool0, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT) != VK_SUCCESS) {
            std::cerr << "Failed to reset compute queue 0 command pool 0" << std::endl;
            return 1;
        }

        VkCommandBufferBeginInfo vk_compute_queue0_command_pool0_command_buffer0_begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };

        if (vkBeginCommandBuffer(vk_compute_queue0_command_pool0_command_buffer0, &vk_compute_queue0_command_pool0_command_buffer0_begin_info) != VK_SUCCESS) {
            std::cerr << "Failed to begin compute queue 0 command pool 0 command buffer 0" << std::endl;
            return 1;
        }

        /* transition all resources */
        VkBufferMemoryBarrier vk_compute_pass0_0_initial_transition_buffer_memory_barriers[3] = {
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .srcQueueFamilyIndex = queues.compute0_0.family_index,
                .dstQueueFamilyIndex = queues.compute0_0.family_index,
                .buffer = vk_cellular_automata_input_buffer,
                .offset = 0,
                .size = cellular_automata_input_buffer_resident.vk_size,
            },
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .srcQueueFamilyIndex = queues.compute0_0.family_index,
                .dstQueueFamilyIndex = queues.compute0_0.family_index,
                .buffer = vk_cellular_automata_output_buffer,
                .offset = 0,
                .size = cellular_automata_output_buffer_resident.vk_size,
            },
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT,
                .srcQueueFamilyIndex = queues.compute0_0.family_index,
                .dstQueueFamilyIndex = queues.compute0_0.family_index,
                .buffer = vk_uniform_buffer,
                .offset = 0,
                .size = sizeof(Uniforms),
            },
        };

        VkImageMemoryBarrier vk_compute_pass0_0_initial_transition_image_memory_barriers[1] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = queues.compute0_0.family_index,
                .dstQueueFamilyIndex = queues.compute0_0.family_index,
                .image = vk_cellular_automata_render_image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
        };

        vkCmdPipelineBarrier(vk_compute_queue0_command_pool0_command_buffer0,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr,
            sizeof(vk_compute_pass0_0_initial_transition_buffer_memory_barriers) / sizeof(VkBufferMemoryBarrier),
            &vk_compute_pass0_0_initial_transition_buffer_memory_barriers[0],
            sizeof(vk_compute_pass0_0_initial_transition_image_memory_barriers) / sizeof(VkImageMemoryBarrier),
            &vk_compute_pass0_0_initial_transition_image_memory_barriers[0]
        );

        vkCmdBindPipeline(vk_compute_queue0_command_pool0_command_buffer0, VK_PIPELINE_BIND_POINT_COMPUTE, vk_compute_pass0_0_pipeline);
        vkCmdBindDescriptorSets(vk_compute_queue0_command_pool0_command_buffer0, VK_PIPELINE_BIND_POINT_COMPUTE, vk_compute_pass0_0_pipeline_layout, 0, 1, &vk_compute_pass0_0_descriptor_pool0_set0, 0, nullptr);
        vkCmdDispatch(vk_compute_queue0_command_pool0_command_buffer0, width / 8, height / 8, 1);

        VkImageMemoryBarrier vk_compute_pass0_0_prepare_for_blit_image_memory_barriers[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = queues.compute0_0.family_index,
                .dstQueueFamilyIndex = queues.compute0_0.family_index,
                .image = vk_cellular_automata_render_image,
                .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = queues.compute0_0.family_index,
                .dstQueueFamilyIndex = queues.compute0_0.family_index,
                .image = vk_swapchain_backbuffers[vk_swapchain_backbuffer_index],
                .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
        };

        vkCmdPipelineBarrier(vk_compute_queue0_command_pool0_command_buffer0,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            render ? 2 : 1,
            &vk_compute_pass0_0_prepare_for_blit_image_memory_barriers[0]
        );
        
        if (render) {
            VkClearColorValue vk_swapchain_backbuffer_clear_color = {
                .float32 = { 0.0f, 0.0f, 0.0f, 1.0f }
            };

            VkImageSubresourceRange vk_swapchain_backbuffer_clear_range = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };

            vkCmdClearColorImage(vk_compute_queue0_command_pool0_command_buffer0, vk_swapchain_backbuffers[vk_swapchain_backbuffer_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &vk_swapchain_backbuffer_clear_color, 1, &vk_swapchain_backbuffer_clear_range);

            VkImageBlit vk_compute_pass0_0_blit_render_image_to_swapchain = {
                .srcSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .srcOffsets = {
                    {
                        .x = 0,
                        .y = 0,
                        .z = 0,
                    },
                    {
                        .x = static_cast<int32_t>(width),
                        .y = static_cast<int32_t>(height),
                        .z = 1,
                    },
                },
                .dstSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .dstOffsets = {
                    {
                        .x = 0,
                        .y = 0,
                        .z = 0,
                    },
                    {
                        .x = static_cast<int32_t>(window_width),
                        .y = static_cast<int32_t>(window_height),
                        .z = 1,
                    },
                },
            };

            vkCmdBlitImage(vk_compute_queue0_command_pool0_command_buffer0, vk_cellular_automata_render_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_swapchain_backbuffers[vk_swapchain_backbuffer_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vk_compute_pass0_0_blit_render_image_to_swapchain, (width >= window_width / 2) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);

            VkImageMemoryBarrier vk_swapchain_backbuffer_prepare_for_imgui_render_pass0_0_image_memory_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = queues.compute0_0.family_index,
                .dstQueueFamilyIndex = queues.compute0_0.family_index,
                .image = vk_swapchain_backbuffers[vk_swapchain_backbuffer_index],
                .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                .layerCount = 1,
                },
            };

            vkCmdPipelineBarrier(vk_compute_queue0_command_pool0_command_buffer0,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                0, nullptr,
                0, nullptr,
                1,
                &vk_swapchain_backbuffer_prepare_for_imgui_render_pass0_0_image_memory_barrier
            );

            VkRenderingAttachmentInfoKHR vk_imgui_render_pass0_0_color_attachment_info = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
                .imageView = vk_swapchain_backbuffer_views[vk_swapchain_backbuffer_index],
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {
                    .color = {
                        .float32 = { 0.0f, 0.0f, 0.0f, 1.0f },
                    },
                },
            };

            VkRenderingInfoKHR vk_imgui_render_pass0_0_rendering_info = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
                .renderArea = {
                    .offset = {
                        .x = 0,
                        .y = 0,
                    },
                    .extent = swapchain_returns.vk_current_extent,
                },
                .layerCount = 1,
                .viewMask = 0,
                .colorAttachmentCount = 1,
                .pColorAttachments = &vk_imgui_render_pass0_0_color_attachment_info,
            };

            vkCmdBeginRenderingKHR(vk_compute_queue0_command_pool0_command_buffer0, &vk_imgui_render_pass0_0_rendering_info);
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vk_compute_queue0_command_pool0_command_buffer0, nullptr);
            vkCmdEndRenderingKHR(vk_compute_queue0_command_pool0_command_buffer0);

            VkImageMemoryBarrier vk_swapchain_backbuffer_prepare_for_present_image_memory_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = queues.compute0_0.family_index,
                .dstQueueFamilyIndex = queues.compute0_0.family_index,
                .image = vk_swapchain_backbuffers[vk_swapchain_backbuffer_index],
                .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                .layerCount = 1,
                },
            };

            vkCmdPipelineBarrier(vk_compute_queue0_command_pool0_command_buffer0,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr,
                0, nullptr,
                1,
                &vk_swapchain_backbuffer_prepare_for_present_image_memory_barrier
            );
        }

        if (vkEndCommandBuffer(vk_compute_queue0_command_pool0_command_buffer0) != VK_SUCCESS) {
            std::cerr << "Failed to end compute queue 0 command pool 0 command buffer 0" << std::endl;
            return 1;
        }

        VkPipelineStageFlags vk_compute_queue0_submit_wait_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;

        VkSubmitInfo vk_compute_queue0_submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = (render) ? 1u : 0u,
            .pWaitSemaphores = (render) ? &vk_swapchain_image_acquisition_semaphore : nullptr,
            .pWaitDstStageMask = (render) ? &vk_compute_queue0_submit_wait_stage_mask : nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &vk_compute_queue0_command_pool0_command_buffer0,
            .signalSemaphoreCount = (render) ? 1u : 0u,
            .pSignalSemaphores = (render) ? &vk_swapchain_image_finished_semaphores[vk_swapchain_backbuffer_index] : nullptr,
        };

        if (vkQueueSubmit(queues.compute0_0.vk_queue, 1, &vk_compute_queue0_submit_info, vk_compute_pass0_0_finished_fence) != VK_SUCCESS) {
            std::cerr << "Failed to submit compute pass 0.0 work to compute queue 0.0" << std::endl;
            return 1;
        }

        if (render) {
            VkPresentInfoKHR vk_compute_queue0_present_info = {
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &vk_swapchain_image_finished_semaphores[vk_swapchain_backbuffer_index],
                .swapchainCount = 1,
                .pSwapchains = &vk_swapchain,
                .pImageIndices = &vk_swapchain_backbuffer_index,
                .pResults = nullptr,
            };

            if (vkQueuePresentKHR(queues.compute0_0.vk_queue, &vk_compute_queue0_present_info) != VK_SUCCESS) {
                std::cerr << "Failed to present compute queue 0" << std::endl;
            }
        }

        tick++;
        if ((p_uniforms->v & 0x20) == 0) {
            game_tick++;
        }
    }

    vkDeviceWaitIdle(vk_device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    /* cleanup uniform resources and free heap */
    vkDestroyBuffer(vk_device, vk_uniform_buffer, nullptr);
    vkUnmapMemory(vk_device, uniform_heap.vk_heap_memory);
    kvk::resource::mono_free_heap(vk_device, uniform_heap);

    /* cleanup cellular automata resources and free heap */
    vkDestroyImageView(vk_device, vk_cellular_automata_render_image_view, nullptr);
    vkDestroyImage(vk_device, vk_cellular_automata_render_image, nullptr);
    vkDestroyBuffer(vk_device, vk_cellular_automata_buffer1, nullptr);
    vkDestroyBuffer(vk_device, vk_cellular_automata_buffer0, nullptr);
    kvk::resource::mono_free_heap(vk_device, cellular_automata_heap);

    /* cleanup swapchain */
    for (uint32_t i = 0; i < vk_swapchain_backbuffers.size(); ++i) {
        vkDestroySemaphore(vk_device, vk_swapchain_image_finished_semaphores[i], nullptr);
        vkDestroyImageView(vk_device, vk_swapchain_backbuffer_views[i], nullptr);
    }
    vkDestroySemaphore(vk_device, vk_swapchain_image_acquisition_semaphore, nullptr);
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
