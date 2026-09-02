#ifndef GPU_VULKAN_SURFACE_H
#define GPU_VULKAN_SURFACE_H

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gpu
{

  struct VulkanSurface
  {
    void *userData = nullptr;
    bool (*requiredInstanceExtensions)(void *userData,
                                       const char *const *&extensions,
                                       std::uint32_t &count) = nullptr;
    bool (*create)(void *userData, VkInstance instance,
                   VkSurfaceKHR &surface) = nullptr;
    void (*drawableSize)(void *userData, std::uint32_t &width,
                         std::uint32_t &height) = nullptr;
  };

} // namespace gpu

#endif
