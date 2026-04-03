#include "rendering/VulkanDevice.h"

#include <cstring>
#include <set>
#include <string>

namespace physisim::rendering {

bool VulkanDevice::init(GLFWwindow* window, std::string& err) {
    window_ = window;
    uint32_t glfwExtCount = 0;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    for (uint32_t i = 0; i < glfwExtCount; ++i) instanceExtensions_.push_back(glfwExt[i]);

    if (!createInstance(err)) return false;
    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS) {
        err = "glfwCreateWindowSurface failed";
        return false;
    }
    if (!pickPhysical(err)) return false;
    if (!createLogical(err)) return false;
    return true;
}

void VulkanDevice::shutdown() {
    if (device_) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

bool VulkanDevice::createInstance(std::string& err) {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "PhysiSim CAD";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "PhysiSim";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions_.size());
    ci.ppEnabledExtensionNames = instanceExtensions_.data();

    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) {
        err = "vkCreateInstance failed";
        return false;
    }
    return true;
}

static QueueIndices findQueues(VkPhysicalDevice dev, VkSurfaceKHR surf) {
    QueueIndices q;
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &n, nullptr);
    std::vector<VkQueueFamilyProperties> props(n);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &n, props.data());
    for (uint32_t i = 0; i < n; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) q.graphics = i;
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surf, &present);
        if (present) q.present = i;
        if (q.complete()) break;
    }
    return q;
}

bool VulkanDevice::pickPhysical(std::string& err) {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    if (n == 0) {
        err = "No Vulkan devices";
        return false;
    }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance_, &n, devs.data());
    for (auto d : devs) {
        queues_ = findQueues(d, surface_);
        if (!queues_.complete()) continue;
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, exts.data());
        std::set<std::string> names;
        for (const auto& e : exts) names.insert(e.extensionName);
        bool swapOk = names.count(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        if (!swapOk) continue;
        physical_ = d;
        vkGetPhysicalDeviceProperties(physical_, &props_);
        return true;
    }
    err = "No suitable GPU";
    return false;
}

bool VulkanDevice::createLogical(std::string& err) {
    float prio = 1.f;
    std::vector<VkDeviceQueueCreateInfo> qcis;
    std::set<uint32_t> fam = {queues_.graphics, queues_.present};
    for (uint32_t f : fam) {
        VkDeviceQueueCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex = f;
        q.queueCount = 1;
        q.pQueuePriorities = &prio;
        qcis.push_back(q);
    }

    VkPhysicalDeviceFeatures feats{};
    feats.fillModeNonSolid = VK_TRUE;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = static_cast<uint32_t>(qcis.size());
    dci.pQueueCreateInfos = qcis.data();
    dci.pEnabledFeatures = &feats;
    dci.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions_.size());
    dci.ppEnabledExtensionNames = deviceExtensions_.data();

    if (vkCreateDevice(physical_, &dci, nullptr, &device_) != VK_SUCCESS) {
        err = "vkCreateDevice failed";
        return false;
    }
    vkGetDeviceQueue(device_, queues_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queues_.present, 0, &presentQueue_);
    return true;
}

} // namespace physisim::rendering
