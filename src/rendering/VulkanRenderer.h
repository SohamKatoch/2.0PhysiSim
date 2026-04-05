#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "geometry/Mesh.h"
#include "rendering/Camera.h"
#include "rendering/VulkanDevice.h"

namespace physisim::rendering {

/// Tunable weakness visualization (matches `mesh.frag` / UBO packing).
struct MeshDefectViewParams {
    float stressScale{1.f};
    float velocityScale{1.f};
    float loadScale{1.f};
    /// 0 = weighted combined heat, 1 = RGB false-color (geo / stress / motion+load), 2 = multi-objective alignment.
    float visualMode{0.f};
    /// Blends local combined metric with mesh-connectivity propagated field.
    float timeMix{0.f};
};

struct GpuMesh {
    VkBuffer vbo{VK_NULL_HANDLE};
    VkDeviceMemory vboMem{VK_NULL_HANDLE};
    VkBuffer ibo{VK_NULL_HANDLE};
    VkDeviceMemory iboMem{VK_NULL_HANDLE};
    uint32_t indexCount{0};
    void destroy(VkDevice dev);
};

class VulkanRenderer {
public:
    bool init(GLFWwindow* window, int width, int height, std::string& err);
    void shutdown();

    void resize(int width, int height);

    bool beginFrame();
    void recordMeshPass(const geometry::Mesh& mesh, const glm::mat4& model, const Camera& cam,
                        const MeshDefectViewParams& defectView = MeshDefectViewParams{});
    bool endFrame();

    void uploadMesh(const geometry::Mesh& mesh, std::string& err);

    GLFWwindow* window() const { return window_; }
    VkInstance instance() const { return dev_.instance(); }
    VkPhysicalDevice physical() const { return dev_.physical(); }
    VkDevice device() const { return dev_.device(); }
    VkQueue graphicsQueue() const { return dev_.graphicsQueue(); }
    uint32_t graphicsFamily() const { return dev_.queues().graphics; }
    VkRenderPass renderPass() const { return renderPass_; }
    VkPipeline pipeline() const { return pipeline_; }
    VkPipelineLayout pipelineLayout() const { return pipelineLayout_; }
    VkCommandBuffer commandBuffer() const { return cmd_; }
    VkDescriptorPool descriptorPool() const { return descPool_; }
    uint32_t swapchainImageCount() const { return static_cast<uint32_t>(swapImages_.size()); }
    VkFramebuffer framebuffer(uint32_t i) const { return framebuffers_[i]; }
    VkExtent2D extent() const { return extent_; }

private:
    bool createSwapchain(std::string& err);
    void destroySwapchain();
    bool createDepth(std::string& err);
    bool createRenderPass(std::string& err);
    bool createFramebuffers(std::string& err);
    bool createPipeline(std::string& err);
    bool createCommandPool(std::string& err);
    bool createSync(std::string& err);
    bool createUniform(std::string& err);
    uint32_t findMemory(uint32_t typeBits, VkMemoryPropertyFlags props);

    GLFWwindow* window_{nullptr};
    VulkanDevice dev_;
    VkSwapchainKHR swap_{VK_NULL_HANDLE};
    VkFormat swapFormat_{};
    VkExtent2D extent_{};
    std::vector<VkImage> swapImages_;
    std::vector<VkImageView> swapViews_;
    VkImage depthImg_{VK_NULL_HANDLE};
    VkDeviceMemory depthMem_{VK_NULL_HANDLE};
    VkImageView depthView_{VK_NULL_HANDLE};
    VkRenderPass renderPass_{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    VkPipelineCache pipeCache_{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool cmdPool_{VK_NULL_HANDLE};
    VkCommandBuffer cmd_{VK_NULL_HANDLE};
    VkSemaphore semImg_{VK_NULL_HANDLE};
    VkSemaphore semRender_{VK_NULL_HANDLE};
    VkFence fence_{VK_NULL_HANDLE};

    VkDescriptorSetLayout descLayout_{VK_NULL_HANDLE};
    VkDescriptorPool descPool_{VK_NULL_HANDLE};
    VkDescriptorSet descSet_{VK_NULL_HANDLE};
    VkBuffer ubo_{VK_NULL_HANDLE};
    VkDeviceMemory uboMem_{VK_NULL_HANDLE};
    void* uboMapped_{nullptr};

    VkShaderModule vert_{VK_NULL_HANDLE};
    VkShaderModule frag_{VK_NULL_HANDLE};

    GpuMesh gpuMesh_{};
    uint32_t frameIndex_{0};

    struct UboData {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 cameraWorld{};
        /// xyz = stress / velocity / load scales; w = visualMode.
        glm::vec4 defectScales{};
        /// x = timeMix (propagated vs local); yzw unused.
        glm::vec4 defectTime{};
    };
};

} // namespace physisim::rendering
