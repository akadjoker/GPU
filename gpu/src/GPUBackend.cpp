#include "gpu/GPUBackend.h"
#include "gpu/GPU.h"

#include "../backends/null/NullDevice.h"

namespace gpu
{

#if defined(GPU_HAS_OPENGL)
  Device *createOpenGLDevice(const DeviceDesc &desc, GPUError *error);
#endif
#if defined(GPU_HAS_OPENGLES)
  Device *createOpenGLESDevice(const DeviceDesc &desc, GPUError *error);
#endif
#if defined(GPU_HAS_VULKAN)
  Device *createVulkanDevice(const DeviceDesc &desc, GPUError *error);
#endif

  void reportDeviceCreationFailure(GPUError *error, GPUErrorCode code,
                                   const char *message)
  {
    if (!error)
      return;
    *error = GPUError{};
    error->code = code;
    error->severity = GPUErrorSeverity::Fatal;
    error->operation = GPUOperation::CreateDevice;
    error->message = message;
  }

  Device *createDevice(const DeviceDesc &desc, GPUError *error)
  {
    if (error)
      *error = GPUError{};
#if defined(GPU_HAS_OPENGL)
    if (desc.backend == Backend::OpenGL)
      return createOpenGLDevice(desc, error);
#endif
#if defined(GPU_HAS_OPENGLES)
    if (desc.backend == Backend::OpenGLES)
      return createOpenGLESDevice(desc, error);
#endif
#if defined(GPU_HAS_VULKAN)
    if (desc.backend == Backend::Vulkan)
      return createVulkanDevice(desc, error);
#endif
#if defined(GPU_HAS_NULL)
    if (desc.backend == Backend::Null)
      return createNullDevice(desc, error);
#endif
    reportDeviceCreationFailure(
        error, GPUErrorCode::UnsupportedFeature,
        "the requested backend is not compiled into this build");
    return nullptr;
  }

  void destroyDevice(Device *device)
  {
    if (!device)
      return;
    device->shutdown();
    delete device;
  }

} // namespace gpu
