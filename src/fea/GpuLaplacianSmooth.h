#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan.h>

namespace physisim::geometry {
struct Mesh;
}

namespace physisim::fea {

/// First GPU FEA-related path: Vulkan **compute** Laplacian smoothing (Jacobi-style mix toward neighbor centroid).
/// Writes results back to CPU `Mesh::positions` (PoC); normals should be recomputed after.
class GpuLaplacianSmooth {
public:
    GpuLaplacianSmooth() = default;
    ~GpuLaplacianSmooth();

    GpuLaplacianSmooth(const GpuLaplacianSmooth&) = delete;
    GpuLaplacianSmooth& operator=(const GpuLaplacianSmooth&) = delete;

    bool init(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t queueFamily, std::string& err);
    void shutdown();

    /// One smoothing step: lambda in (0,1] — fraction toward neighbor average.
    bool smoothStep(geometry::Mesh& mesh, float lambda, std::string& err);

    bool ready() const { return device_ != VK_NULL_HANDLE && pipeline_ != VK_NULL_HANDLE; }

private:
    bool ensureBuffers(size_t vertexCount, size_t neighIdxBytes, size_t neighOffBytes, std::string& err);
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props);
    void destroyBuffers();

    VkPhysicalDevice physical_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    uint32_t queueFamily_{0};

    VkShaderModule cs_{VK_NULL_HANDLE};
    VkDescriptorSetLayout descLayout_{VK_NULL_HANDLE};
    VkPipelineLayout pipeLayout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    VkDescriptorPool descPool_{VK_NULL_HANDLE};
    VkDescriptorSet descSet_{VK_NULL_HANDLE};
    VkCommandPool cmdPool_{VK_NULL_HANDLE};
    VkCommandBuffer cmd_{VK_NULL_HANDLE};
    VkFence fence_{VK_NULL_HANDLE};

    VkBuffer posIn_{VK_NULL_HANDLE};
    VkDeviceMemory posInMem_{VK_NULL_HANDLE};
    VkBuffer posOut_{VK_NULL_HANDLE};
    VkDeviceMemory posOutMem_{VK_NULL_HANDLE};
    VkBuffer neighIdx_{VK_NULL_HANDLE};
    VkDeviceMemory neighIdxMem_{VK_NULL_HANDLE};
    VkBuffer neighOff_{VK_NULL_HANDLE};
    VkDeviceMemory neighOffMem_{VK_NULL_HANDLE};

    void* posInMapped_{nullptr};
    void* posOutMapped_{nullptr};
    void* neighIdxMapped_{nullptr};
    void* neighOffMapped_{nullptr};

    size_t capVertices_{0};
    size_t capNeighIdx_{0};
    size_t capNeighOff_{0};
};

} // namespace physisim::fea
