#ifndef GPU_DEMO_SDL_GL_SURFACE_H
#define GPU_DEMO_SDL_GL_SURFACE_H

#include "../backends/gl/GLSurface.h"

#include <SDL2/SDL.h>

struct SDLGLSurface
{
  SDL_Window *window = nullptr;
  SDL_GLContext context = nullptr;
  gpu::GLSurface gpuSurface;
};

bool createSDLGLSurface(SDLGLSurface &surface, const char *title,
                        std::uint32_t width, std::uint32_t height);
void destroySDLGLSurface(SDLGLSurface &surface);

#endif
