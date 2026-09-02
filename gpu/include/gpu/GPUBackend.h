#ifndef GPU_BACKEND_H
#define GPU_BACKEND_H

#include "GPUCapabilities.h"
#include "GPUError.h"
#include "GPUSurface.h"
#include "GPUTypes.h"

namespace gpu
{

  /** @brief Backend implementation selected when creating a device. */
  enum class Backend : std::uint8_t
  {
    Null,
    OpenGL,
    OpenGLES,
    WebGPU,
    Vulkan,
    Metal
  };

  /** @brief Parameters used by createDevice. */
  struct DeviceDesc
  {
    /** @brief Backend to instantiate. */
    Backend backend = Backend::Null;
    /** @brief Portability profile requested from the backend. */
    RendererProfile profile = RendererProfile::Portable;
    /** @brief Minimum capabilities required by the application. */
    GPUCapabilities requiredCapabilities;
    /** @brief Native surface information supplied to the selected backend. */
    SurfaceDesc surface;
  };

  /** @brief Opaque device interface implemented by each backend. */
  class Device;

  /** @brief Populate an error with a device-creation failure. */
  void reportDeviceCreationFailure(GPUError *error, GPUErrorCode code,
                                   const char *message);

  /**
   * @brief Create a device for the requested backend.
   * @param desc Backend, profile, capabilities and surface parameters.
   * @param error Optional destination for creation diagnostics.
   * @return An owned device pointer, or `nullptr` when creation fails.
   */
  Device *createDevice(const DeviceDesc &desc, GPUError *error = nullptr);

  /** @brief Shut down and release a device returned by createDevice. */
  void destroyDevice(Device *device);

} // namespace gpu

#endif
