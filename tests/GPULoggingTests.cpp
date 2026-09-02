#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"
#include "gpu/GPUDiagnostics.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

int formatLogLine(char *buffer, std::size_t bufferSize,
                  const gpu::GPUError &error) {
  return std::snprintf(
      buffer, bufferSize, "[%s] %s: %s (resource=%llu v0=%llu v1=%llu) %s",
      gpu::toString(error.severity), gpu::toString(error.operation),
      gpu::toString(error.code),
      static_cast<unsigned long long>(error.resource),
      static_cast<unsigned long long>(error.value0),
      static_cast<unsigned long long>(error.value1),
      error.message ? error.message : "");
}

void testRealBackendErrorsFormatCleanly() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(device->pendingErrorCount() == 0);
  assert(device->totalErrorCount() == 0);

  device->destroy(gpu::BufferHandle(0xdeadbeef));

  gpu::BufferDesc emptyBufferDesc;
  emptyBufferDesc.size = 0;
  assert(!device->createBuffer(emptyBufferDesc).valid());

  assert(!device->createQuery(gpu::QueryType::Timestamp).valid());

  gpu::BufferDesc bufferDesc;
  bufferDesc.size = 16;
  bufferDesc.usage = gpu::BufferUsageVertex;
  const gpu::BufferHandle buffer = device->createBuffer(bufferDesc);
  assert(buffer.valid());
  assert(!device->copyBuffer(buffer, 0, buffer, 8, 16));

  assert(device->totalErrorCount() == 4);
  assert(device->pendingErrorCount() == 4);

  std::uint32_t drained = 0;
  gpu::GPUError error;
  while (device->getError(error)) {
    char line[256];
    const int written = formatLogLine(line, sizeof(line), error);
    assert(written > 0);
    assert(static_cast<std::size_t>(written) < sizeof(line));
    assert(std::strstr(line, "Unknown") == nullptr);
    assert(error.code != gpu::GPUErrorCode::None);
    ++drained;
  }
  assert(drained == 4);
  assert(device->pendingErrorCount() == 0);
  assert(device->totalErrorCount() == 4);

  device->destroy(buffer);
  gpu::destroyDevice(device);
}

void testOverflowFromRealRepeatedErrors() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);

  const std::uint32_t triggerCount = gpu::GPUErrorQueue::Capacity + 8;
  for (std::uint32_t index = 0; index < triggerCount; ++index)
    device->destroy(gpu::BufferHandle(index + 1));

  assert(device->totalErrorCount() == triggerCount);
  assert(device->pendingErrorCount() == gpu::GPUErrorQueue::Capacity + 1);

  gpu::GPUError error;
  gpu::GPUError overflowError;
  std::uint32_t drained = 0;
  while (device->getError(error)) {
    overflowError = error;
    ++drained;
  }
  assert(drained == gpu::GPUErrorQueue::Capacity + 1);
  assert(overflowError.code == gpu::GPUErrorCode::ErrorQueueOverflow);
  assert(overflowError.value0 == 8);

  char line[256];
  const int written = formatLogLine(line, sizeof(line), overflowError);
  assert(written > 0);
  assert(std::strstr(line, "ErrorQueueOverflow") != nullptr);

  gpu::destroyDevice(device);
}

void testClearErrorsKeepsTotalForLogging() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);

  device->destroy(gpu::BufferHandle(1));
  device->destroy(gpu::BufferHandle(2));
  assert(device->pendingErrorCount() == 2);

  device->clearErrors();
  assert(device->pendingErrorCount() == 0);
  assert(device->totalErrorCount() == 2);

  gpu::GPUError error;
  assert(!device->getError(error));

  gpu::destroyDevice(device);
}

} // namespace

int main() {
  testRealBackendErrorsFormatCleanly();
  testOverflowFromRealRepeatedErrors();
  testClearErrorsKeepsTotalForLogging();
  return 0;
}
