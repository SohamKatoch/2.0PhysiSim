#include "ui/ImGuiLayer.h"

#include <iterator>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "ui/UiTheme.h"

namespace physisim::ui {

static void checkVk(VkResult err) {
    if (err != VK_SUCCESS) {
        // ImGui may call this during init; avoid exceptions in GUI path.
    }
}

bool ImGuiLayer::init(GLFWwindow* window, rendering::VulkanRenderer& vk, std::string& err) {
    VkDevice device = vk.device();
    VkInstance inst = vk.instance();

    ImGui_ImplVulkan_LoadFunctions(
        [](const char* fn, void* user) {
            auto* pinst = static_cast<VkInstance*>(user);
            return vkGetInstanceProcAddr(*pinst, fn);
        },
        &inst);

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &imguiPool_) != VK_SUCCESS) {
        err = "ImGui descriptor pool failed";
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    applyPhysiSimTheme();

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = inst;
    initInfo.PhysicalDevice = vk.physical();
    initInfo.Device = device;
    initInfo.QueueFamily = vk.graphicsFamily();
    initInfo.Queue = vk.graphicsQueue();
    initInfo.DescriptorPool = imguiPool_;
    initInfo.RenderPass = vk.renderPass();
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = vk.swapchainImageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.Subpass = 0;
    initInfo.DescriptorPoolSize = 0;
    initInfo.UseDynamicRendering = false;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = checkVk;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        err = "ImGui_ImplVulkan_Init failed";
        return false;
    }

    ImGui_ImplVulkan_CreateFontsTexture();
    initialized_ = true;
    return true;
}

void ImGuiLayer::shutdown(rendering::VulkanRenderer& vk) {
    if (!initialized_) return;
    VkDevice device = vk.device();
    vkDeviceWaitIdle(device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (imguiPool_) {
        vkDestroyDescriptorPool(device, imguiPool_, nullptr);
        imguiPool_ = VK_NULL_HANDLE;
    }
    initialized_ = false;
}

void ImGuiLayer::newFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::render(VkCommandBuffer cmd) {
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd) ImGui_ImplVulkan_RenderDrawData(dd, cmd, VK_NULL_HANDLE);
}

} // namespace physisim::ui
