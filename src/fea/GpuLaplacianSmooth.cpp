#include "fea/GpuLaplacianSmooth.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "fea/MeshAdjacency.h"
#include "geometry/Mesh.h"
#include "rendering/VulkanPipeline.h"

#ifndef PHYSISIM_SHADER_DIR
#define PHYSISIM_SHADER_DIR "."
#endif

namespace physisim::fea {

namespace {

struct PushData {
    uint32_t vertexCount;
    float lambda;
};

} // namespace

GpuLaplacianSmooth::~GpuLaplacianSmooth() { shutdown(); }

bool GpuLaplacianSmooth::init(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t queueFamily,
                              std::string& err) {
    shutdown();

    std::string spvPath = std::string(PHYSISIM_SHADER_DIR) + "/laplacian_smooth.spv";
    auto code = rendering::readSpvFile(spvPath);
    if (code.empty()) {
        err = "Missing SPIR-V: " + spvPath;
        return false;
    }
    VkShaderModule csTry = rendering::loadShaderModule(device, code);
    if (!csTry) {
        err = "loadShaderModule laplacian_smooth failed";
        return false;
    }
    physical_ = physical;
    device_ = device;
    queue_ = queue;
    queueFamily_ = queueFamily;
    cs_ = csTry;

    VkDescriptorSetLayoutBinding binds[4]{};
    for (int i = 0; i < 4; ++i) {
        binds[i].binding = static_cast<uint32_t>(i);
        binds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binds[i].descriptorCount = 1;
        binds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dsl{};
    VkPushConstantRange pcr{};
    VkPipelineLayoutCreateInfo pl{};
    VkComputePipelineCreateInfo pci{};
    VkDescriptorPoolSize ps{};
    VkDescriptorPoolCreateInfo dpci{};
    VkDescriptorSetAllocateInfo dsai{};
    VkCommandPoolCreateInfo cpi{};
    VkCommandBufferAllocateInfo cbai{};
    VkFenceCreateInfo fi{};

    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 4;
    dsl.pBindings = binds;
    if (vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &descLayout_) != VK_SUCCESS) {
        err = "vkCreateDescriptorSetLayout (fea) failed";
        goto fail;
    }

    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(PushData);

    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &descLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device_, &pl, nullptr, &pipeLayout_) != VK_SUCCESS) {
        err = "vkCreatePipelineLayout (fea) failed";
        goto fail;
    }

    pci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pci.stage.module = cs_;
    pci.stage.pName = "main";
    pci.layout = pipeLayout_;
    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline_) != VK_SUCCESS) {
        err = "vkCreateComputePipelines failed";
        goto fail;
    }

    ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps.descriptorCount = 4;
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(device_, &dpci, nullptr, &descPool_) != VK_SUCCESS) {
        err = "vkCreateDescriptorPool (fea) failed";
        goto fail;
    }

    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = descPool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &descLayout_;
    if (vkAllocateDescriptorSets(device_, &dsai, &descSet_) != VK_SUCCESS) {
        err = "vkAllocateDescriptorSets (fea) failed";
        goto fail;
    }

    cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpi.queueFamilyIndex = queueFamily_;
    if (vkCreateCommandPool(device_, &cpi, nullptr, &cmdPool_) != VK_SUCCESS) {
        err = "vkCreateCommandPool (fea) failed";
        goto fail;
    }

    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmdPool_;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &cbai, &cmd_) != VK_SUCCESS) {
        err = "vkAllocateCommandBuffers (fea) failed";
        goto fail;
    }

    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device_, &fi, nullptr, &fence_) != VK_SUCCESS) {
        err = "vkCreateFence (fea) failed";
        goto fail;
    }

    return true;

fail:
    if (fence_) {
        vkDestroyFence(device_, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }
    if (cmdPool_) {
        vkDestroyCommandPool(device_, cmdPool_, nullptr);
        cmdPool_ = VK_NULL_HANDLE;
        cmd_ = VK_NULL_HANDLE;
    }
    if (descPool_) {
        vkDestroyDescriptorPool(device_, descPool_, nullptr);
        descPool_ = VK_NULL_HANDLE;
        descSet_ = VK_NULL_HANDLE;
    }
    if (pipeline_) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeLayout_) {
        vkDestroyPipelineLayout(device_, pipeLayout_, nullptr);
        pipeLayout_ = VK_NULL_HANDLE;
    }
    if (descLayout_) {
        vkDestroyDescriptorSetLayout(device_, descLayout_, nullptr);
        descLayout_ = VK_NULL_HANDLE;
    }
    if (cs_) {
        vkDestroyShaderModule(device_, cs_, nullptr);
        cs_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
    physical_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    return false;
}

void GpuLaplacianSmooth::shutdown() {
    destroyBuffers();
    if (fence_) {
        vkDestroyFence(device_, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }
    if (cmdPool_) {
        vkDestroyCommandPool(device_, cmdPool_, nullptr);
        cmdPool_ = VK_NULL_HANDLE;
        cmd_ = VK_NULL_HANDLE;
    }
    if (descPool_) {
        vkDestroyDescriptorPool(device_, descPool_, nullptr);
        descPool_ = VK_NULL_HANDLE;
        descSet_ = VK_NULL_HANDLE;
    }
    if (pipeline_) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeLayout_) {
        vkDestroyPipelineLayout(device_, pipeLayout_, nullptr);
        pipeLayout_ = VK_NULL_HANDLE;
    }
    if (descLayout_) {
        vkDestroyDescriptorSetLayout(device_, descLayout_, nullptr);
        descLayout_ = VK_NULL_HANDLE;
    }
    if (cs_) {
        vkDestroyShaderModule(device_, cs_, nullptr);
        cs_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
    physical_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
}

uint32_t GpuLaplacianSmooth::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical_, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    return 0;
}

void GpuLaplacianSmooth::destroyBuffers() {
    auto freeBuf = [&](VkBuffer& b, VkDeviceMemory& m, void*& map) {
        if (m) {
            if (map) {
                vkUnmapMemory(device_, m);
                map = nullptr;
            }
            vkFreeMemory(device_, m, nullptr);
            m = VK_NULL_HANDLE;
        }
        if (b) {
            vkDestroyBuffer(device_, b, nullptr);
            b = VK_NULL_HANDLE;
        }
    };
    freeBuf(posIn_, posInMem_, posInMapped_);
    freeBuf(posOut_, posOutMem_, posOutMapped_);
    freeBuf(neighIdx_, neighIdxMem_, neighIdxMapped_);
    freeBuf(neighOff_, neighOffMem_, neighOffMapped_);
    capVertices_ = 0;
    capNeighIdx_ = 0;
    capNeighOff_ = 0;
}

bool GpuLaplacianSmooth::ensureBuffers(size_t vertexCount, size_t neighIdxBytes, size_t neighOffBytes,
                                       std::string& err) {
    if (vertexCount == 0) {
        err = "empty mesh";
        return false;
    }
    if (capVertices_ >= vertexCount && capNeighIdx_ >= neighIdxBytes && capNeighOff_ >= neighOffBytes) return true;

    destroyBuffers();

    auto allocHost = [&](VkBuffer& buf, VkDeviceMemory& mem, void** map, VkDeviceSize size, const char* what) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &bi, nullptr, &buf) != VK_SUCCESS) {
            err = std::string("vkCreateBuffer ") + what;
            return false;
        }
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(device_, buf, &req);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex =
            findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device_, &mai, nullptr, &mem) != VK_SUCCESS) {
            err = std::string("vkAllocateMemory ") + what;
            return false;
        }
        vkBindBufferMemory(device_, buf, mem, 0);
        if (vkMapMemory(device_, mem, 0, size, 0, map) != VK_SUCCESS) {
            err = std::string("vkMapMemory ") + what;
            return false;
        }
        return true;
    };

    const VkDeviceSize posBytes = static_cast<VkDeviceSize>(vertexCount * sizeof(glm::vec4));
    if (!allocHost(posIn_, posInMem_, &posInMapped_, posBytes, "posIn")) return false;
    if (!allocHost(posOut_, posOutMem_, &posOutMapped_, posBytes, "posOut")) return false;
    if (!allocHost(neighIdx_, neighIdxMem_, &neighIdxMapped_, static_cast<VkDeviceSize>(neighIdxBytes), "neighIdx"))
        return false;
    if (!allocHost(neighOff_, neighOffMem_, &neighOffMapped_, static_cast<VkDeviceSize>(neighOffBytes), "neighOff"))
        return false;

    capVertices_ = vertexCount;
    capNeighIdx_ = neighIdxBytes;
    capNeighOff_ = neighOffBytes;

    VkDescriptorBufferInfo infos[4]{};
    infos[0] = {posIn_, 0, posBytes};
    infos[1] = {posOut_, 0, posBytes};
    infos[2] = {neighIdx_, 0, static_cast<VkDeviceSize>(neighIdxBytes)};
    infos[3] = {neighOff_, 0, static_cast<VkDeviceSize>(neighOffBytes)};

    VkWriteDescriptorSet writes[4]{};
    for (int i = 0; i < 4; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descSet_;
        writes[i].dstBinding = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);
    return true;
}

bool GpuLaplacianSmooth::smoothStep(geometry::Mesh& mesh, float lambda, std::string& err) {
    if (!ready()) {
        err = "GpuLaplacianSmooth not initialized";
        return false;
    }
    lambda = std::clamp(lambda, 1e-4f, 1.f);

    const size_t V = mesh.positions.size();
    if (V == 0 || mesh.indices.empty()) {
        err = "mesh has no geometry";
        return false;
    }

    std::vector<uint32_t> neighOff, neighIdx;
    buildUndirectedNeighborCsr(mesh, neighOff, neighIdx);
    if (neighOff.size() != V + 1) {
        err = "adjacency build failed";
        return false;
    }

    const size_t idxBytes = neighIdx.size() * sizeof(uint32_t);
    const size_t offBytes = neighOff.size() * sizeof(uint32_t);
    if (!ensureBuffers(V, idxBytes, offBytes, err)) return false;

    auto* posIn = static_cast<glm::vec4*>(posInMapped_);
    auto* posOut = static_cast<glm::vec4*>(posOutMapped_);
    for (size_t i = 0; i < V; ++i) {
        glm::vec3 p = mesh.positions[i];
        float w = (i < mesh.defectHighlight.size()) ? mesh.defectHighlight[i].x : 0.f;
        posIn[i] = glm::vec4(p, w);
    }
    std::memcpy(neighIdxMapped_, neighIdx.data(), idxBytes);
    std::memcpy(neighOffMapped_, neighOff.data(), offBytes);

    vkDeviceWaitIdle(device_);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_, &bi);

    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0,
                         nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeLayout_, 0, 1, &descSet_, 0, nullptr);

    PushData push{};
    push.vertexCount = static_cast<uint32_t>(V);
    push.lambda = lambda;
    vkCmdPushConstants(cmd_, pipeLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushData), &push);

    uint32_t groups = static_cast<uint32_t>((V + 63) / 64);
    vkCmdDispatch(cmd_, groups, 1, 1);

    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0,
                         nullptr, 0, nullptr);

    vkEndCommandBuffer(cmd_);

    vkResetFences(device_, 1, &fence_);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    if (vkQueueSubmit(queue_, 1, &si, fence_) != VK_SUCCESS) {
        err = "vkQueueSubmit (fea) failed";
        return false;
    }
    if (vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        err = "vkWaitForFences (fea) failed";
        return false;
    }

    for (size_t i = 0; i < V; ++i) {
        mesh.positions[i] = glm::vec3(posOut[i].x, posOut[i].y, posOut[i].z);
        if (i < mesh.defectHighlight.size()) mesh.defectHighlight[i].x = posOut[i].w;
    }

    mesh.recomputeNormals();
    return true;
}

} // namespace physisim::fea
