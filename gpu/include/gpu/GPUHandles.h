#ifndef GPU_HANDLES_H
#define GPU_HANDLES_H

#include <cstdint>

namespace gpu
{

  /**
   * @brief Strongly typed opaque resource handle.
   *
   * A default-constructed handle has value zero and is invalid. The tag type
   * prevents handles for different resource kinds from being mixed.
   */
  template <typename Tag>
  class Handle
  {
  public:
    /** @brief Construct an invalid handle. */
    constexpr Handle() = default;
    /** @brief Construct a handle from a backend-provided numeric value. */
    explicit constexpr Handle(std::uint64_t value) : mValue(value) {}

    /** @brief Return whether this handle contains a non-zero value. */
    constexpr bool valid() const { return mValue != 0; }
    /** @brief Return the underlying opaque numeric value. */
    constexpr std::uint64_t value() const { return mValue; }
    /** @brief Convert to `true` when the handle is valid. */
    explicit constexpr operator bool() const { return valid(); }

    /** @brief Compare two handles of the same resource type. */
    friend constexpr bool operator==(Handle left, Handle right)
    {
      return left.mValue == right.mValue;
    }
    /** @brief Compare two handles of the same resource type. */
    friend constexpr bool operator!=(Handle left, Handle right)
    {
      return left.mValue != right.mValue;
    }

  private:
    std::uint64_t mValue = 0;
  };

  /** @brief Opaque handle for a buffer resource. */
  using BufferHandle = Handle<struct BufferTag>;
  /** @brief Opaque handle for a texture resource. */
  using TextureHandle = Handle<struct TextureTag>;
  /** @brief Opaque handle for a sampler resource. */
  using SamplerHandle = Handle<struct SamplerTag>;
  /** @brief Opaque handle for a pipeline resource. */
  using PipelineHandle = Handle<struct PipelineTag>;
  /** @brief Opaque handle for a query resource. */
  using QueryHandle = Handle<struct QueryTag>;
  /** @brief Opaque handle for a fence resource. */
  using FenceHandle = Handle<struct FenceTag>;

} // namespace gpu

#endif
