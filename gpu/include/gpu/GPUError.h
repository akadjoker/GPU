#ifndef GPU_ERROR_H
#define GPU_ERROR_H

#include <cstdint>

namespace gpu
{

  /** @brief Error codes reported by device operations and the error queue. */
  enum class GPUErrorCode : std::uint16_t
  {
    None,
    InvalidArgument,
    InvalidHandle,
    UnsupportedFeature,
    UnsupportedFormat,
    OutOfBounds,
    OutOfMemory,
    ShaderCompilationFailed,
    PipelineCreationFailed,
    DeviceCreationFailed,
    DeviceLost,
    SurfaceLost,
    BackendFailure,
    ErrorQueueOverflow
  };

  /** @brief Severity associated with a GPUError. */
  enum class GPUErrorSeverity : std::uint8_t
  {
    Info,
    Warning,
    Error,
    Fatal
  };

  /** @brief Operation associated with a GPUError. */
  enum class GPUOperation : std::uint16_t
  {
    None,
    CreateBuffer,
    CreateTexture,
    CreateSampler,
    CreatePipeline,
    ReflectPipeline,
    CreateQuery,
    CreateFence,
    CreateDevice,
    UpdateBuffer,
    UpdateTexture,
    GenerateMipmaps,
    MapBuffer,
    ReadBuffer,
    ReadTexture,
    BeginRenderPass,
    SetPipeline,
    BindResource,
    Draw,
    DrawIndirect,
    Dispatch,
    Copy,
    Present,
    Destroy
  };

  /** @brief One diagnostic record returned by a device or error queue. */
  struct GPUError
  {
    /** @brief Error classification. */
    GPUErrorCode code = GPUErrorCode::None;
    /** @brief Diagnostic severity. */
    GPUErrorSeverity severity = GPUErrorSeverity::Error;
    /** @brief Operation that produced the error. */
    GPUOperation operation = GPUOperation::None;
    /** @brief Opaque resource value associated with the error, if any. */
    std::uint64_t resource = 0;
    /** @brief Operation-specific numeric diagnostic value. */
    std::uint64_t value0 = 0;
    /** @brief Operation-specific numeric diagnostic value. */
    std::uint64_t value1 = 0;
    /** @brief Diagnostic message pointer; ownership and lifetime are unspecified. */
    const char *message = nullptr;
  };

  /** @brief Fixed-capacity FIFO queue for GPUError records. */
  class GPUErrorQueue
  {
  public:
    /** @brief Maximum number of error records retained by the queue. */
    static constexpr std::uint32_t Capacity = 256;

    /** @brief Return the total number of errors submitted to the queue. */
    std::uint64_t totalErrorCount() const;
    /** @brief Return the number of records currently available to consume. */
    std::uint32_t pendingErrorCount() const;
    /**
     * @brief Pop the oldest available error.
     * @param error Destination record.
     * @return `true` when a record was written to `error`.
     */
    bool getError(GPUError &error);
    /** @brief Discard all pending records, including an overflow notice. */
    void clearErrors();
    /** @brief Append an error record to the queue. */
    void push(const GPUError &error);

  private:
    GPUError mErrors[Capacity];
    std::uint64_t mTotalErrorCount = 0;
    std::uint64_t mDiscardedErrorCount = 0;
    std::uint32_t mReadIndex = 0;
    std::uint32_t mCount = 0;
  };

} // namespace gpu

#endif
