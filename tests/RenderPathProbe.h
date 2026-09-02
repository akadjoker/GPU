#ifndef GPU_RENDER_PATH_PROBE_H
#define GPU_RENDER_PATH_PROBE_H

#include "gpu/GPU.h"

#include <cassert>

namespace gpu
{
class Device;
}

void runRenderPathProbe(gpu::Device &device, bool es, const char *label);

// Shared assertion helper: pull the next queued error off a device and
// assert one was actually pending. Used by every backend's device-test
// executable to check "an operation failed with the expected error code"
// without each one reimplementing the same three lines.
inline gpu::GPUError takeError(gpu::Device &device)
{
  gpu::GPUError error;
  assert(device.getError(error));
  return error;
}

#endif
