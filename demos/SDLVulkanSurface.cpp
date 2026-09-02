#include "SDLVulkanSurface.h"

#include <SDL2/SDL_vulkan.h>

namespace
{

  bool requiredInstanceExtensions(void *userData, const char *const *&extensions,
                                  std::uint32_t &count)
  {
    SDLVulkanSurface *surface = static_cast<SDLVulkanSurface *>(userData);
    unsigned extensionCount = 0;
    if (SDL_Vulkan_GetInstanceExtensions(surface->window, &extensionCount, nullptr) !=
        SDL_TRUE)
      return false;
    surface->instanceExtensions.resize(extensionCount);
    if (SDL_Vulkan_GetInstanceExtensions(surface->window, &extensionCount,
                                         surface->instanceExtensions.data()) != SDL_TRUE)
      return false;
    extensions = surface->instanceExtensions.data();
    count = static_cast<std::uint32_t>(extensionCount);
    return true;
  }

  bool createSurface(void *userData, VkInstance instance, VkSurfaceKHR &surface)
  {
    SDLVulkanSurface *sdlSurface = static_cast<SDLVulkanSurface *>(userData);
    return SDL_Vulkan_CreateSurface(sdlSurface->window, instance, &surface) == SDL_TRUE;
  }

  void drawableSize(void *userData, std::uint32_t &width, std::uint32_t &height)
  {
    SDLVulkanSurface *surface = static_cast<SDLVulkanSurface *>(userData);
    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_Vulkan_GetDrawableSize(surface->window, &drawableWidth, &drawableHeight);
    width = drawableWidth > 0 ? static_cast<std::uint32_t>(drawableWidth) : 0;
    height = drawableHeight > 0 ? static_cast<std::uint32_t>(drawableHeight) : 0;
  }

} // namespace

bool createSDLVulkanSurface(SDLVulkanSurface &surface, const char *title,
                            std::uint32_t width, std::uint32_t height)
{
  surface.window = SDL_CreateWindow(
      title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      static_cast<int>(width), static_cast<int>(height),
      SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (!surface.window)
    return false;
  surface.gpuSurface.userData = &surface;
  surface.gpuSurface.requiredInstanceExtensions = requiredInstanceExtensions;
  surface.gpuSurface.create = createSurface;
  surface.gpuSurface.drawableSize = drawableSize;
  return true;
}

void destroySDLVulkanSurface(SDLVulkanSurface &surface)
{
  if (surface.window)
    SDL_DestroyWindow(surface.window);
  surface = SDLVulkanSurface{};
}
