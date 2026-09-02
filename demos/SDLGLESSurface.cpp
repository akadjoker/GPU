#include "SDLGLESSurface.h"

namespace
{

  bool makeCurrent(void *userData)
  {
    SDLGLESSurface *surface = static_cast<SDLGLESSurface *>(userData);
    return SDL_GL_MakeCurrent(surface->window, surface->context) == 0;
  }

  void *getProcAddress(void *, const char *name)
  {
    return SDL_GL_GetProcAddress(name);
  }

  void present(void *userData)
  {
    SDLGLESSurface *surface = static_cast<SDLGLESSurface *>(userData);
    SDL_GL_SwapWindow(surface->window);
  }

  void drawableSize(void *userData, std::uint32_t &width,
                    std::uint32_t &height)
  {
    SDLGLESSurface *surface = static_cast<SDLGLESSurface *>(userData);
    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_GL_GetDrawableSize(surface->window, &drawableWidth, &drawableHeight);
    width = drawableWidth > 0 ? static_cast<std::uint32_t>(drawableWidth) : 0;
    height = drawableHeight > 0 ? static_cast<std::uint32_t>(drawableHeight) : 0;
  }

} // namespace

bool createSDLGLESSurface(SDLGLESSurface &surface, const char *title,
                          std::uint32_t width, std::uint32_t height)
{
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  surface.window = SDL_CreateWindow(
      title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      static_cast<int>(width), static_cast<int>(height),
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!surface.window)
    return false;
  surface.context = SDL_GL_CreateContext(surface.window);
  if (!surface.context)
  {
    SDL_DestroyWindow(surface.window);
    surface.window = nullptr;
    return false;
  }
  SDL_GL_SetSwapInterval(1);
  surface.gpuSurface.userData = &surface;
  surface.gpuSurface.makeCurrent = makeCurrent;
  surface.gpuSurface.getProcAddress = getProcAddress;
  surface.gpuSurface.present = present;
  surface.gpuSurface.drawableSize = drawableSize;
  return true;
}

void destroySDLGLESSurface(SDLGLESSurface &surface)
{
  if (surface.context)
    SDL_GL_DeleteContext(surface.context);
  if (surface.window)
    SDL_DestroyWindow(surface.window);
  surface = SDLGLESSurface{};
}
