#include "gpu/GPUError.h"

#include <limits>

namespace gpu
{

  namespace
  {

    const char *const ErrorQueueOverflowMessage = "GPU error queue overflow";

    void increment(std::uint64_t &value)
    {
      if (value != (std::numeric_limits<std::uint64_t>::max)())
        ++value;
    }

  } // namespace

  std::uint64_t GPUErrorQueue::totalErrorCount() const
  {
    return mTotalErrorCount;
  }

  std::uint32_t GPUErrorQueue::pendingErrorCount() const
  {
    return mCount + (mDiscardedErrorCount != 0 ? 1u : 0u);
  }

  bool GPUErrorQueue::getError(GPUError &error)
  {
    if (mCount != 0)
    {
      error = mErrors[mReadIndex];
      mReadIndex = (mReadIndex + 1) % Capacity;
      --mCount;
      return true;
    }

    if (mDiscardedErrorCount == 0)
      return false;

    error = GPUError{};
    error.code = GPUErrorCode::ErrorQueueOverflow;
    error.severity = GPUErrorSeverity::Warning;
    error.value0 = mDiscardedErrorCount;
    error.message = ErrorQueueOverflowMessage;
    mDiscardedErrorCount = 0;
    return true;
  }

  void GPUErrorQueue::clearErrors()
  {
    mReadIndex = 0;
    mCount = 0;
    mDiscardedErrorCount = 0;
  }

  void GPUErrorQueue::push(const GPUError &error)
  {
    increment(mTotalErrorCount);

    if (mCount == Capacity)
    {
      increment(mDiscardedErrorCount);
      return;
    }

    const std::uint32_t writeIndex = (mReadIndex + mCount) % Capacity;
    mErrors[writeIndex] = error;
    ++mCount;
  }

} // namespace gpu
