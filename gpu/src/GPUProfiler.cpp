#include "gpu/GPUProfiler.h"

namespace gpu
{

  GPUProfiler::GPUProfiler(Device &device)
      : mDevice(device), mEnabled(device.capabilities().timestampQueries)
  {
  }

  GPUProfiler::~GPUProfiler()
  {
    for (FrameSlot &frame : mFrames)
      for (std::uint32_t index = 0; index < MaxScopesPerFrame; ++index)
      {
        if (frame.scopes[index].begin.valid())
          mDevice.destroy(frame.scopes[index].begin);
        if (frame.scopes[index].end.valid())
          mDevice.destroy(frame.scopes[index].end);
      }
  }

  void GPUProfiler::beginFrame()
  {
    if (!mEnabled)
      return;
    mWriteFrame = (mWriteFrame + 1) % FrameLatency;
    FrameSlot &frame = mFrames[mWriteFrame];
    frame.scopeCount = 0;
    frame.ended = false;
    mActiveScope = InvalidScope;
    mInFrame = true;
  }

  void GPUProfiler::endFrame()
  {
    if (!mEnabled || !mInFrame)
      return;
    mFrames[mWriteFrame].ended = true;
    mFrames[mWriteFrame].collected = false;
    mInFrame = false;
  }

  bool GPUProfiler::beginScope(const char *name)
  {
    if (!mEnabled || !mInFrame || mActiveScope != InvalidScope)
      return false;
    FrameSlot &frame = mFrames[mWriteFrame];
    if (frame.scopeCount >= MaxScopesPerFrame)
      return false;
    Scope &scope = frame.scopes[frame.scopeCount];
    if (!scope.begin.valid())
      scope.begin = mDevice.createQuery(QueryType::Timestamp);
    if (!scope.end.valid())
      scope.end = mDevice.createQuery(QueryType::Timestamp);
    if (!scope.begin.valid() || !scope.end.valid())
      return false;
    if (!mDevice.writeTimestamp(scope.begin))
      return false;
    scope.name = name;
    mActiveScope = frame.scopeCount;
    ++frame.scopeCount;
    return true;
  }

  void GPUProfiler::endScope()
  {
    if (!mEnabled || mActiveScope == InvalidScope)
      return;
    mDevice.writeTimestamp(mFrames[mWriteFrame].scopes[mActiveScope].end);
    mActiveScope = InvalidScope;
  }

  std::uint32_t GPUProfiler::collectFrame(ProfilerScopeResult *results,
                                          std::uint32_t maxResults)
  {
    if (!mEnabled)
      return 0;
    const std::uint32_t readFrame = (mWriteFrame + 1) % FrameLatency;
    FrameSlot &frame = mFrames[readFrame];
    if (!frame.ended || frame.collected)
      return 0;
    for (std::uint32_t index = 0; index < frame.scopeCount; ++index)
    {
      if (!mDevice.isQueryResultAvailable(frame.scopes[index].begin) ||
          !mDevice.isQueryResultAvailable(frame.scopes[index].end))
        return 0;
    }
    std::uint64_t beginNanoseconds[MaxScopesPerFrame];
    std::uint64_t endNanoseconds[MaxScopesPerFrame];
    for (std::uint32_t index = 0; index < frame.scopeCount; ++index)
    {
      if (!mDevice.getQueryResult(frame.scopes[index].begin,
                                  beginNanoseconds[index]) ||
          !mDevice.getQueryResult(frame.scopes[index].end,
                                  endNanoseconds[index]))
        return 0;
    }
    const std::uint32_t count =
        frame.scopeCount < maxResults ? frame.scopeCount : maxResults;
    for (std::uint32_t index = 0; index < count; ++index)
    {
      results[index].name = frame.scopes[index].name;
      results[index].milliseconds =
          static_cast<double>(endNanoseconds[index] - beginNanoseconds[index]) /
          1000000.0;
    }
    frame.collected = true;
    return count;
  }

} // namespace gpu
