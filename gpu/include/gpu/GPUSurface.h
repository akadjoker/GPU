#ifndef GPU_SURFACE_H
#define GPU_SURFACE_H

#include "GPUTypes.h"

#include <cstdint>

namespace gpu
{

  /** @brief Native surface information passed to a device backend. */
  struct SurfaceDesc
  {
    /** @brief Backend-specific native window or surface handle. */
    void *nativeHandle = nullptr;
    /** @brief Requested surface width in pixels. */
    std::uint32_t width = 0;
    /** @brief Requested surface height in pixels. */
    std::uint32_t height = 0;
    /** @brief Requested surface color format. */
    Format format = Format::RGBA8Srgb;
  };

  /** @brief Current presentation-surface state. */
  enum class SurfaceState : std::uint8_t
  {
    Ready,
    Suspended,
    Lost
  };

} // namespace gpu

#endif
