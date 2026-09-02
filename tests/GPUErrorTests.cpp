#include "gpu/GPUError.h"

#include <cassert>

using namespace gpu;

namespace {

GPUError makeError(std::uint64_t value) {
  GPUError error;
  error.code = GPUErrorCode::InvalidArgument;
  error.operation = GPUOperation::CreateBuffer;
  error.value0 = value;
  return error;
}

void testFifo() {
  GPUErrorQueue queue;
  assert(queue.totalErrorCount() == 0);
  assert(queue.pendingErrorCount() == 0);

  queue.push(makeError(1));
  queue.push(makeError(2));
  assert(queue.totalErrorCount() == 2);
  assert(queue.pendingErrorCount() == 2);

  GPUError error;
  assert(queue.getError(error));
  assert(error.value0 == 1);
  assert(queue.getError(error));
  assert(error.value0 == 2);
  assert(!queue.getError(error));
}

void testClearPreservesTotal() {
  GPUErrorQueue queue;
  queue.push(makeError(1));
  queue.clearErrors();
  assert(queue.totalErrorCount() == 1);
  assert(queue.pendingErrorCount() == 0);
}

void testOverflow() {
  GPUErrorQueue queue;
  for (std::uint32_t value = 0; value < GPUErrorQueue::Capacity + 4; ++value)
    queue.push(makeError(value));

  assert(queue.totalErrorCount() == GPUErrorQueue::Capacity + 4);
  assert(queue.pendingErrorCount() == GPUErrorQueue::Capacity + 1);

  GPUError error;
  for (std::uint32_t value = 0; value < GPUErrorQueue::Capacity; ++value) {
    assert(queue.getError(error));
    assert(error.code == GPUErrorCode::InvalidArgument);
    assert(error.value0 == value);
  }

  assert(queue.getError(error));
  assert(error.code == GPUErrorCode::ErrorQueueOverflow);
  assert(error.value0 == 4);
  assert(error.message != nullptr);
  assert(!queue.getError(error));
}

} // namespace

int main() {
  testFifo();
  testClearPreservesTotal();
  testOverflow();
  return 0;
}
