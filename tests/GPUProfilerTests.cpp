#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"
#include "gpu/GPUProfiler.h"

#include <cassert>
#include <cstring>

namespace {

void testDisabledOnPortableProfile() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(!device->capabilities().timestampQueries);

  {
    gpu::GPUProfiler profiler(*device);
    assert(!profiler.enabled());
    profiler.beginFrame();
    assert(!profiler.beginScope("scope"));
    profiler.endScope();
    profiler.endFrame();
    gpu::ProfilerScopeResult results[4];
    assert(profiler.collectFrame(results, 4) == 0);
  }

  gpu::destroyDevice(device);
}

void testCollectsNamedScopesAfterLatency() {
  gpu::DeviceDesc descriptor;
  descriptor.profile = gpu::RendererProfile::Modern;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(device->capabilities().timestampQueries);

  {
    gpu::GPUProfiler profiler(*device);
    assert(profiler.enabled());

    profiler.beginFrame();
    assert(profiler.beginScope("A"));
    profiler.endScope();
    assert(profiler.beginScope("B"));
    profiler.endScope();
    profiler.endFrame();

    gpu::ProfilerScopeResult results[4];
    assert(profiler.collectFrame(results, 4) == 0);

    for (std::uint32_t frame = 0; frame + 1 < gpu::GPUProfiler::FrameLatency;
        ++frame) {
      profiler.beginFrame();
      profiler.endFrame();
    }

    const std::uint32_t count = profiler.collectFrame(results, 4);
    assert(count == 2);
    assert(std::strcmp(results[0].name, "A") == 0);
    assert(std::strcmp(results[1].name, "B") == 0);
    assert(profiler.collectFrame(results, 4) == 0);
  }

  gpu::destroyDevice(device);
}

void testScopeCapacityAndNesting() {
  gpu::DeviceDesc descriptor;
  descriptor.profile = gpu::RendererProfile::Modern;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);

  {
    gpu::GPUProfiler profiler(*device);
    profiler.beginFrame();
    for (std::uint32_t index = 0; index < gpu::GPUProfiler::MaxScopesPerFrame;
        ++index) {
      assert(profiler.beginScope("scope"));
      profiler.endScope();
    }
    assert(!profiler.beginScope("overflow"));
    profiler.endFrame();

    profiler.beginFrame();
    assert(profiler.beginScope("outer"));
    assert(!profiler.beginScope("inner"));
    profiler.endScope();
    profiler.endFrame();
  }

  gpu::destroyDevice(device);
}

} // namespace

int main() {
  testDisabledOnPortableProfile();
  testCollectsNamedScopesAfterLatency();
  testScopeCapacityAndNesting();
  return 0;
}
