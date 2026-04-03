#pragma once

#include <string>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "rendering/VulkanRenderer.h"

namespace physisim::ui {

class ImGuiLayer {
public:
    bool init(GLFWwindow* window, rendering::VulkanRenderer& vk, std::string& err);
    void shutdown(rendering::VulkanRenderer& vk);

    void newFrame();
    void render(VkCommandBuffer cmd);

private:
    VkDescriptorPool imguiPool_{VK_NULL_HANDLE};
    bool initialized_{false};
};

} // namespace physisim::ui
