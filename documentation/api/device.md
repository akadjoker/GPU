# Device lifecycle

Select a backend in `gpu::DeviceDesc` and call `gpu::createDevice`. Passing a
`gpu::GPUError` is recommended because creation failures include a stable code,
operation, severity, and message.

```cpp
#include <gpu/GPUBackend.h>

gpu::DeviceDesc desc;
desc.backend = gpu::Backend::Null;

gpu::GPUError error;
gpu::Device *device = gpu::createDevice(desc, &error);
if (!device) {
    // Inspect error.code and error.message.
    return;
}

// Use device while its native surface/context is valid.
gpu::destroyDevice(device);
```

`createDevice` returns an owned pointer. Release it exactly once with
`gpu::destroyDevice`; that call shuts down the backend before deleting the
device. Do not copy a `Device` or retain resource handles after shutdown.

The Null backend does not require a native surface. The OpenGL, OpenGL ES, and
Vulkan backends read a backend-specific object through
`DeviceDesc::surface.nativeHandle`; the concrete callback contracts are
currently internal backend headers. **REVIEW:** promote those surface types to
the documented public API, or add a stable public adapter before documenting
their callback arguments as user-facing API.
