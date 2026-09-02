#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"
#include "gpu/GPUError.h"
#include "gpu/GPUHandles.h"

#include <cassert>

int main() {
  gpu::BufferHandle empty;
  gpu::BufferHandle buffer(42);
  assert(!empty.valid());
  assert(buffer.valid());
  assert(buffer.value() == 42);
  assert(buffer != empty);

  gpu::DeviceDesc descriptor;
  assert(descriptor.backend == gpu::Backend::Null);
  assert(descriptor.profile == gpu::RendererProfile::Portable);
  assert(descriptor.surface.nativeHandle == nullptr);
  assert(descriptor.requiredCapabilities.maxColorAttachments == 1);
  gpu::TextureRegion region;
  assert(region.depthOrLayers == 1);
  gpu::TextureDataLayout layout;
  assert(layout.offset == 0);
  assert(layout.bytesPerRow == 0);
  assert(layout.rowsPerImage == 0);
  return 0;
}
