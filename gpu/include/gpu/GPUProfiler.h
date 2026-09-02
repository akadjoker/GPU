#ifndef GPU_PROFILER_H
#define GPU_PROFILER_H

#include "GPU.h"

namespace gpu
{

  /** @brief Timing result for one named profiler scope. */
  struct ProfilerScopeResult
  {
    /** @brief Scope name supplied to beginScope. */
    const char *name = nullptr;
    /** @brief Measured duration in milliseconds. */
    double milliseconds = 0.0;
  };

  /** @brief Optional timestamp-query based frame profiler. */
  class GPUProfiler
  {
  public:
    /** @brief Maximum number of scopes recorded in one frame. */
    static constexpr std::uint32_t MaxScopesPerFrame = 16;
    /** @brief Number of frame slots used to defer query collection. */
    static constexpr std::uint32_t FrameLatency = 3;

    /** @brief Construct a profiler associated with a device. */
    explicit GPUProfiler(Device &device);
    /** @brief Release profiler-owned query resources. */
    ~GPUProfiler();

    /** @brief Copy construction is disabled. */
    GPUProfiler(const GPUProfiler &) = delete;
    /** @brief Copy assignment is disabled. */
    GPUProfiler &operator=(const GPUProfiler &) = delete;

    /** @brief Return whether timestamp profiling is enabled for the device. */
    bool enabled() const { return mEnabled; }

    /** @brief Begin recording scopes for the next frame. */
    void beginFrame();
    /** @brief End recording scopes for the current frame. */
    void endFrame();
    /**
     * @brief Begin a named timestamp scope.
     * @param name Non-owning scope name pointer.
     * @return `true` when the scope was recorded.
     */
    bool beginScope(const char *name);
    /** @brief End the currently active timestamp scope. */
    void endScope();

    /**
     * @brief Collect results from a completed frame when queries are ready.
     * @param results Destination array for timing results.
     * @param maxResults Maximum number of results that may be written.
     * @return Number of results written, or zero when results are unavailable.
     */
    std::uint32_t collectFrame(ProfilerScopeResult *results,
                               std::uint32_t maxResults);

  private:
    struct Scope
    {
      const char *name = nullptr;
      QueryHandle begin;
      QueryHandle end;
    };

    struct FrameSlot
    {
      Scope scopes[MaxScopesPerFrame];
      std::uint32_t scopeCount = 0;
      bool ended = false;
      bool collected = true;
    };

    static constexpr std::uint32_t InvalidScope = 0xffffffffu;

    Device &mDevice;
    FrameSlot mFrames[FrameLatency];
    std::uint32_t mWriteFrame = FrameLatency - 1;
    std::uint32_t mActiveScope = InvalidScope;
    bool mEnabled = false;
    bool mInFrame = false;
  };

} // namespace gpu

#endif
