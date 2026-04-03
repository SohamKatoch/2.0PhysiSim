#pragma once

#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

namespace physisim::rendering {

struct QueueIndices {
    uint32_t graphics = VK_QUEUE_FAMILY_IGNORED;
    uint32_t present = VK_QUEUE_FAMILY_IGNORED;
    bool complete() const {
        return graphics != VK_QUEUE_FAMILY_IGNORED && present != VK_QUEUE_FAMILY_IGNORED;
    }
};

class VulkanDevice {
public:
    bool init(GLFWwindow* window, std::string& err);
    void shutdown();

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physical() const { return physical_; }
    VkDevice device() const { return device_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    VkQueue presentQueue() const { return presentQueue_; }
    QueueIndices queues() const { return queues_; }

    VkPhysicalDeviceProperties props() const { return props_; }

private:
    bool createInstance(std::string& err);
    bool pickPhysical(std::string& err);
    bool createLogical(std::string& err);

    GLFWwindow* window_{nullptr};
    VkInstance instance_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue graphicsQueue_{VK_NULL_HANDLE};
    VkQueue presentQueue_{VK_NULL_HANDLE};
    QueueIndices queues_;
    VkPhysicalDeviceProperties props_{};

    std::vector<const char*> instanceExtensions_;
    std::vector<const char*> deviceExtensions_ = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
};

} // namespace physisim::rendering
