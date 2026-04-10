#include "rendering/VulkanRenderer.h"

#include <array>
#include <cstring>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

#include "geometry/Mesh.h"
#include "rendering/VulkanPipeline.h"

#ifndef PHYSISIM_SHADER_DIR
#define PHYSISIM_SHADER_DIR "."
#endif

namespace physisim::rendering {

void GpuMesh::destroy(VkDevice d) {
    if (vbo) vkDestroyBuffer(d, vbo, nullptr);
    if (vboMem) vkFreeMemory(d, vboMem, nullptr);
    if (ibo) vkDestroyBuffer(d, ibo, nullptr);
    if (iboMem) vkFreeMemory(d, iboMem, nullptr);
    vbo = ibo = VK_NULL_HANDLE;
    vboMem = iboMem = VK_NULL_HANDLE;
    indexCount = 0;
}

bool VulkanRenderer::init(GLFWwindow* window, int width, int height, std::string& err) {
    window_ = window;
    if (!dev_.init(window, err)) return false;

    VkDevice device = dev_.device();

    std::string spvBase = PHYSISIM_SHADER_DIR;
    auto vcode = readSpvFile(spvBase + "/mesh_vert.spv");
    auto fcode = readSpvFile(spvBase + "/mesh_frag.spv");
    if (vcode.empty() || fcode.empty()) {
        err = "Failed to load SPIR-V shaders from " + spvBase;
        return false;
    }
    vert_ = loadShaderModule(device, vcode);
    frag_ = loadShaderModule(device, fcode);
    if (!vert_ || !frag_) {
        err = "Shader module creation failed";
        return false;
    }

    VkDescriptorSetLayoutBinding uboBind{};
    uboBind.binding = 0;
    uboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBind.descriptorCount = 1;
    uboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 1;
    dsl.pBindings = &uboBind;
    if (vkCreateDescriptorSetLayout(device, &dsl, nullptr, &descLayout_) != VK_SUCCESS) {
        err = "vkCreateDescriptorSetLayout failed";
        return false;
    }

    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &descLayout_;
    if (vkCreatePipelineLayout(device, &pl, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        err = "vkCreatePipelineLayout failed";
        return false;
    }

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps.descriptorCount = 4;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 4;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &descPool_) != VK_SUCCESS) {
        err = "vkCreateDescriptorPool failed";
        return false;
    }

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = descPool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &descLayout_;
    if (vkAllocateDescriptorSets(device, &dsai, &descSet_) != VK_SUCCESS) {
        err = "vkAllocateDescriptorSets failed";
        return false;
    }

    VkBufferCreateInfo ubci{};
    ubci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ubci.size = sizeof(UboData);
    ubci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (vkCreateBuffer(device, &ubci, nullptr, &ubo_) != VK_SUCCESS) {
        err = "vkCreateBuffer UBO failed";
        return false;
    }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, ubo_, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemory(mr.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &mai, nullptr, &uboMem_) != VK_SUCCESS) {
        err = "vkAllocateMemory UBO failed";
        return false;
    }
    vkBindBufferMemory(device, ubo_, uboMem_, 0);
    vkMapMemory(device, uboMem_, 0, sizeof(UboData), 0, &uboMapped_);

    VkDescriptorBufferInfo dbi{};
    dbi.buffer = ubo_;
    dbi.offset = 0;
    dbi.range = sizeof(UboData);
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = descSet_;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);

    extent_.width = static_cast<uint32_t>(width);
    extent_.height = static_cast<uint32_t>(height);
    if (!createSwapchain(err)) return false;
    if (!createDepth(err)) return false;
    if (!createRenderPass(err)) return false;
    if (!createFramebuffers(err)) return false;
    if (!createCommandPool(err)) return false;
    if (!createPipeline(err)) return false;
    if (!createSync(err)) return false;

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmdPool_;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &cbai, &cmd_) != VK_SUCCESS) {
        err = "vkAllocateCommandBuffers failed";
        return false;
    }

    return true;
}

void VulkanRenderer::shutdown() {
    VkDevice device = dev_.device();
    if (device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device);
    gpuMesh_.destroy(device);
    if (fence_) vkDestroyFence(device, fence_, nullptr);
    if (semImg_) vkDestroySemaphore(device, semImg_, nullptr);
    if (semRender_) vkDestroySemaphore(device, semRender_, nullptr);
    if (cmdPool_) vkDestroyCommandPool(device, cmdPool_, nullptr);
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers_.clear();
    if (pipeline_) vkDestroyPipeline(device, pipeline_, nullptr);
    if (pipeCache_) vkDestroyPipelineCache(device, pipeCache_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    if (renderPass_) vkDestroyRenderPass(device, renderPass_, nullptr);
    if (depthView_) vkDestroyImageView(device, depthView_, nullptr);
    if (depthImg_) vkDestroyImage(device, depthImg_, nullptr);
    if (depthMem_) vkFreeMemory(device, depthMem_, nullptr);
    for (auto v : swapViews_) vkDestroyImageView(device, v, nullptr);
    swapViews_.clear();
    if (swap_) vkDestroySwapchainKHR(device, swap_, nullptr);
    swap_ = VK_NULL_HANDLE;
    if (ubo_) vkDestroyBuffer(device, ubo_, nullptr);
    if (uboMem_) {
        vkUnmapMemory(device, uboMem_);
        vkFreeMemory(device, uboMem_, nullptr);
    }
    if (descPool_) vkDestroyDescriptorPool(device, descPool_, nullptr);
    if (descLayout_) vkDestroyDescriptorSetLayout(device, descLayout_, nullptr);
    if (vert_) vkDestroyShaderModule(device, vert_, nullptr);
    if (frag_) vkDestroyShaderModule(device, frag_, nullptr);
    dev_.shutdown();
}

uint32_t VulkanRenderer::findMemory(uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(dev_.physical(), &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    return 0;
}

bool VulkanRenderer::createSwapchain(std::string& err) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev_.physical(), dev_.surface(), &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev_.physical(), dev_.surface(), &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev_.physical(), dev_.surface(), &fmtCount, fmts.data());
    VkSurfaceFormatKHR chosen = fmts[0];
    for (const auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    swapFormat_ = chosen.format;

    VkExtent2D ext = caps.currentExtent;
    if (ext.width == std::numeric_limits<uint32_t>::max()) {
        ext.width = extent_.width;
        ext.height = extent_.height;
    }
    extent_ = ext;

    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev_.physical(), dev_.surface(), &pmCount, nullptr);
    std::vector<VkPresentModeKHR> pms(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev_.physical(), dev_.surface(), &pmCount, pms.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto p : pms) {
        if (p == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = p;
            break;
        }
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = dev_.surface();
    sci.minImageCount = imageCount;
    sci.imageFormat = swapFormat_;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = extent_;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    uint32_t qf[] = {dev_.queues().graphics, dev_.queues().present};
    if (qf[0] != qf[1]) {
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = qf;
    } else {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = presentMode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = swap_;

    VkSwapchainKHR newSwap{};
    if (vkCreateSwapchainKHR(dev_.device(), &sci, nullptr, &newSwap) != VK_SUCCESS) {
        err = "vkCreateSwapchainKHR failed";
        return false;
    }
    if (swap_) vkDestroySwapchainKHR(dev_.device(), swap_, nullptr);
    swap_ = newSwap;

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(dev_.device(), swap_, &n, nullptr);
    swapImages_.resize(n);
    vkGetSwapchainImagesKHR(dev_.device(), swap_, &n, swapImages_.data());

    for (auto v : swapViews_) vkDestroyImageView(dev_.device(), v, nullptr);
    swapViews_.clear();
    swapViews_.reserve(n);
    for (auto img : swapImages_) {
        VkImageViewCreateInfo iv{};
        iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image = img;
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format = swapFormat_;
        iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.levelCount = 1;
        iv.subresourceRange.layerCount = 1;
        VkImageView view{};
        if (vkCreateImageView(dev_.device(), &iv, nullptr, &view) != VK_SUCCESS) {
            err = "vkCreateImageView swap failed";
            return false;
        }
        swapViews_.push_back(view);
    }
    return true;
}

bool VulkanRenderer::createDepth(std::string& err) {
    VkDevice device = dev_.device();
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_D32_SFLOAT;
    ici.extent = {extent_.width, extent_.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (vkCreateImage(device, &ici, nullptr, &depthImg_) != VK_SUCCESS) {
        err = "depth image failed";
        return false;
    }
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, depthImg_, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemory(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &mai, nullptr, &depthMem_) != VK_SUCCESS) {
        err = "depth memory failed";
        return false;
    }
    vkBindImageMemory(device, depthImg_, depthMem_, 0);
    VkImageViewCreateInfo iv{};
    iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv.image = depthImg_;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = VK_FORMAT_D32_SFLOAT;
    iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    iv.subresourceRange.levelCount = 1;
    iv.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &iv, nullptr, &depthView_) != VK_SUCCESS) {
        err = "depth view failed";
        return false;
    }
    return true;
}

bool VulkanRenderer::createRenderPass(std::string& err) {
    VkAttachmentDescription color{};
    color.format = swapFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = VK_FORMAT_D32_SFLOAT;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference cref{};
    cref.attachment = 0;
    cref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference dref{};
    dref.attachment = 1;
    dref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &cref;
    sp.pDepthStencilAttachment = &dref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription atts[2] = {color, depth};
    VkRenderPassCreateInfo rpc{};
    rpc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpc.attachmentCount = 2;
    rpc.pAttachments = atts;
    rpc.subpassCount = 1;
    rpc.pSubpasses = &sp;
    rpc.dependencyCount = 1;
    rpc.pDependencies = &dep;
    if (vkCreateRenderPass(dev_.device(), &rpc, nullptr, &renderPass_) != VK_SUCCESS) {
        err = "vkCreateRenderPass failed";
        return false;
    }
    return true;
}

bool VulkanRenderer::createFramebuffers(std::string& err) {
    framebuffers_.resize(swapViews_.size());
    for (size_t i = 0; i < swapViews_.size(); ++i) {
        VkImageView v[] = {swapViews_[i], depthView_};
        VkFramebufferCreateInfo fbi{};
        fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass = renderPass_;
        fbi.attachmentCount = 2;
        fbi.pAttachments = v;
        fbi.width = extent_.width;
        fbi.height = extent_.height;
        fbi.layers = 1;
        if (vkCreateFramebuffer(dev_.device(), &fbi, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            err = "vkCreateFramebuffer failed";
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::createPipeline(std::string& err) {
    VkPipelineCacheCreateInfo pcc{};
    pcc.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(dev_.device(), &pcc, nullptr, &pipeCache_);
    pipeline_ = createMeshGraphicsPipeline(dev_.device(), pipelineLayout_, renderPass_, extent_, vert_, frag_,
                                             pipeCache_, err);
    return pipeline_ != VK_NULL_HANDLE;
}

bool VulkanRenderer::createCommandPool(std::string& err) {
    VkCommandPoolCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = dev_.queues().graphics;
    if (vkCreateCommandPool(dev_.device(), &cpi, nullptr, &cmdPool_) != VK_SUCCESS) {
        err = "vkCreateCommandPool failed";
        return false;
    }
    return true;
}

bool VulkanRenderer::createSync(std::string& err) {
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(dev_.device(), &si, nullptr, &semImg_) != VK_SUCCESS ||
        vkCreateSemaphore(dev_.device(), &si, nullptr, &semRender_) != VK_SUCCESS) {
        err = "vkCreateSemaphore failed";
        return false;
    }
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(dev_.device(), &fi, nullptr, &fence_) != VK_SUCCESS) {
        err = "vkCreateFence failed";
        return false;
    }
    return true;
}

void VulkanRenderer::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    vkDeviceWaitIdle(dev_.device());
    extent_.width = static_cast<uint32_t>(width);
    extent_.height = static_cast<uint32_t>(height);
    for (auto fb : framebuffers_) vkDestroyFramebuffer(dev_.device(), fb, nullptr);
    framebuffers_.clear();
    if (depthView_) vkDestroyImageView(dev_.device(), depthView_, nullptr);
    if (depthImg_) vkDestroyImage(dev_.device(), depthImg_, nullptr);
    if (depthMem_) vkFreeMemory(dev_.device(), depthMem_, nullptr);
    depthView_ = VK_NULL_HANDLE;
    depthImg_ = VK_NULL_HANDLE;
    depthMem_ = VK_NULL_HANDLE;
    std::string err;
    if (!createSwapchain(err)) return;
    if (!createDepth(err)) return;
    if (!createFramebuffers(err)) return;
    if (pipeline_) vkDestroyPipeline(dev_.device(), pipeline_, nullptr);
    pipeline_ = createMeshGraphicsPipeline(dev_.device(), pipelineLayout_, renderPass_, extent_, vert_, frag_,
                                           pipeCache_, err);
}

bool VulkanRenderer::beginFrame() {
    vkWaitForFences(dev_.device(), 1, &fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(dev_.device(), 1, &fence_);
    VkResult r = vkAcquireNextImageKHR(dev_.device(), swap_, UINT64_MAX, semImg_, VK_NULL_HANDLE, &frameIndex_);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;

    vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd_, &bi);

    VkClearValue clears[2]{};
    clears[0].color = {{0.09f, 0.095f, 0.12f, 1.f}};
    clears[1].depthStencil = {1.f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = renderPass_;
    rp.framebuffer = framebuffers_[frameIndex_];
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    vkCmdBeginRenderPass(cmd_, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.x = 0;
    vp.y = 0;
    vp.width = static_cast<float>(extent_.width);
    vp.height = static_cast<float>(extent_.height);
    vp.minDepth = 0;
    vp.maxDepth = 1;
    vkCmdSetViewport(cmd_, 0, 1, &vp);
    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = extent_;
    vkCmdSetScissor(cmd_, 0, 1, &sc);

    return true;
}

void VulkanRenderer::recordMeshPass(const geometry::Mesh& mesh, const glm::mat4& model, const Camera& cam,
                                    const MeshDefectViewParams& defectView) {
    if (gpuMesh_.indexCount == 0) return;

    UboData u{};
    u.model = model;
    u.view = cam.viewMatrix();
    u.proj = cam.projMatrix();
    u.proj[1][1] *= -1.f;
    u.cameraWorld = glm::vec4(cam.eyePosition(), 0.f);
    u.defectScales =
        glm::vec4(defectView.stressScale, defectView.velocityScale, defectView.loadScale, defectView.visualMode);
    u.defectTime = glm::vec4(defectView.timeMix, defectView.heatRangeMin, defectView.heatRangeMax,
                             defectView.strainAlertThreshold);
    u.defectAux = glm::vec4(defectView.dynamicNormalization ? 1.f : 0.f, defectView.strainAlert ? 1.f : 0.f,
                            defectView.strainAlertBlink ? 1.f : 0.f, defectView.vizTimeSec);
    u.defectAux2 = glm::vec4(defectView.directionVizWeight, 0.f, 0.f, 0.f);
    std::memcpy(uboMapped_, &u, sizeof(u));

    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkBuffer vb = gpuMesh_.vbo;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd_, 0, 1, &vb, &off);
    vkCmdBindIndexBuffer(cmd_, gpuMesh_.ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSet_, 0, nullptr);
    vkCmdDrawIndexed(cmd_, gpuMesh_.indexCount, 1, 0, 0, 0);
}

bool VulkanRenderer::endFrame() {
    vkCmdEndRenderPass(cmd_);
    vkEndCommandBuffer(cmd_);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &semImg_;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &semRender_;
    if (vkQueueSubmit(dev_.graphicsQueue(), 1, &si, fence_) != VK_SUCCESS) return false;

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &semRender_;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swap_;
    pi.pImageIndices = &frameIndex_;
    VkResult pr = vkQueuePresentKHR(dev_.presentQueue(), &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
        resize(static_cast<int>(extent_.width), static_cast<int>(extent_.height));
    }
    return pr == VK_SUCCESS || pr == VK_SUBOPTIMAL_KHR;
}

void VulkanRenderer::uploadMesh(const geometry::Mesh& mesh, std::string& err,
                                const std::vector<glm::vec3>* restPositions) {
    VkDevice device = dev_.device();
    gpuMesh_.destroy(device);

    constexpr size_t kVtxStride = 64;
    std::vector<uint8_t> vtx;
    vtx.resize(mesh.positions.size() * kVtxStride);
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
        float* p = reinterpret_cast<float*>(vtx.data() + i * kVtxStride);
        p[0] = mesh.positions[i].x;
        p[1] = mesh.positions[i].y;
        p[2] = mesh.positions[i].z;
        p[3] = 0.f;
        glm::vec3 n = i < mesh.normals.size() ? mesh.normals[i] : glm::vec3(0, 1, 0);
        p[4] = n.x;
        p[5] = n.y;
        p[6] = n.z;
        glm::vec4 dh = i < mesh.defectHighlight.size() ? mesh.defectHighlight[i] : glm::vec4(0.f);
        p[7] = dh.x;
        p[8] = dh.y;
        p[9] = dh.z;
        p[10] = dh.w;
        float pk = (i < mesh.pickHighlight.size()) ? mesh.pickHighlight[i] : 0.f;
        p[11] = pk;
        float pr = (i < mesh.weaknessPropagated.size()) ? mesh.weaknessPropagated[i] : 0.f;
        p[12] = pr;
        glm::vec3 rp = mesh.positions[i];
        if (restPositions && i < restPositions->size()) rp = (*restPositions)[i];
        p[13] = rp.x;
        p[14] = rp.y;
        p[15] = rp.z;
    }

    VkBufferCreateInfo vbci{};
    vbci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vbci.size = vtx.size();
    vbci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vkCreateBuffer(device, &vbci, nullptr, &gpuMesh_.vbo) != VK_SUCCESS) {
        err = "vbo create";
        return;
    }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, gpuMesh_.vbo, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemory(mr.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &mai, nullptr, &gpuMesh_.vboMem) != VK_SUCCESS) {
        err = "vbo mem";
        return;
    }
    vkBindBufferMemory(device, gpuMesh_.vbo, gpuMesh_.vboMem, 0);
    void* m = nullptr;
    vkMapMemory(device, gpuMesh_.vboMem, 0, vtx.size(), 0, &m);
    std::memcpy(m, vtx.data(), vtx.size());
    vkUnmapMemory(device, gpuMesh_.vboMem);

    size_t ibSize = mesh.indices.size() * sizeof(uint32_t);
    VkBufferCreateInfo ibci{};
    ibci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ibci.size = ibSize;
    ibci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (vkCreateBuffer(device, &ibci, nullptr, &gpuMesh_.ibo) != VK_SUCCESS) {
        err = "ibo create";
        return;
    }
    vkGetBufferMemoryRequirements(device, gpuMesh_.ibo, &mr);
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemory(mr.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &mai, nullptr, &gpuMesh_.iboMem) != VK_SUCCESS) {
        err = "ibo mem";
        return;
    }
    vkBindBufferMemory(device, gpuMesh_.ibo, gpuMesh_.iboMem, 0);
    vkMapMemory(device, gpuMesh_.iboMem, 0, ibSize, 0, &m);
    std::memcpy(m, mesh.indices.data(), ibSize);
    vkUnmapMemory(device, gpuMesh_.iboMem);

    gpuMesh_.indexCount = static_cast<uint32_t>(mesh.indices.size());
}

} // namespace physisim::rendering
