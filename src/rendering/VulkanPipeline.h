#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace physisim::rendering {

VkShaderModule loadShaderModule(VkDevice device, const std::vector<uint32_t>& code);
std::vector<uint32_t> readSpvFile(const std::string& path);

VkPipeline createMeshGraphicsPipeline(VkDevice device, VkPipelineLayout layout, VkRenderPass pass,
                                      VkExtent2D extent, VkShaderModule vert, VkShaderModule frag,
                                      VkPipelineCache cache, std::string& err);

} // namespace physisim::rendering
