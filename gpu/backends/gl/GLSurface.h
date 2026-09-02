#ifndef GPU_GL_SURFACE_H
#define GPU_GL_SURFACE_H

#include <cstdint>

namespace gpu
{

  struct GLSurface
  {
    void *userData = nullptr;
    bool (*makeCurrent)(void *userData) = nullptr;
    void *(*getProcAddress)(void *userData, const char *name) = nullptr;
    void (*present)(void *userData) = nullptr;
    void (*drawableSize)(void *userData, std::uint32_t &width,
                         std::uint32_t &height) = nullptr;
  };

} // namespace gpu

#endif
