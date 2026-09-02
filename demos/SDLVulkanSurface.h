#ifndef GPU_DEMO_SDL_VULKAN_SURFACE_H
#define GPU_DEMO_SDL_VULKAN_SURFACE_H

#include "../backends/vulkan/VulkanSurface.h"

#include <SDL2/SDL.h>

#include <vector>

struct SDLVulkanSurface
{
  SDL_Window *window = nullptr;
  gpu::VulkanSurface gpuSurface;
  std::vector<const char *> instanceExtensions;
};

bool createSDLVulkanSurface(SDLVulkanSurface &surface, const char *title,
                            std::uint32_t width, std::uint32_t height);
void destroySDLVulkanSurface(SDLVulkanSurface &surface);

#endif
