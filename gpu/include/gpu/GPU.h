#ifndef GPU_DEVICE_H
#define GPU_DEVICE_H

#include "GPUCapabilities.h"
#include "GPUDescriptors.h"
#include "GPUError.h"
#include "GPUSurface.h"

namespace gpu {

/** @brief Backend-independent device and command interface. */
class Device {
public:
  /** @brief Destroy the device interface. Use destroyDevice for ownership. */
  virtual ~Device() = default;

  /** @brief Return capabilities reported by this device. */
  virtual const GPUCapabilities &capabilities() const = 0;
  /** @brief Return the total number of errors recorded by this device. */
  virtual std::uint64_t totalErrorCount() const = 0;
  /** @brief Return the number of pending errors available from this device. */
  virtual std::uint32_t pendingErrorCount() const = 0;
  /**
   * @brief Read the oldest pending device error.
   * @param error Destination error record.
   * @return `true` when an error was written.
   */
  virtual bool getError(::gpu::GPUError &error) = 0;
  /** @brief Discard all pending device errors. */
  virtual void clearErrors() = 0;
  /** @brief Shut down backend resources owned by this device. */
  virtual void shutdown() = 0;

  /** @brief Create a buffer from a descriptor. */
  virtual BufferHandle createBuffer(const BufferDesc &desc) = 0;
  /** @brief Create a texture from a descriptor. */
  virtual TextureHandle createTexture(const TextureDesc &desc) = 0;
  /** @brief Create a sampler from a descriptor. */
  virtual SamplerHandle createSampler(const SamplerDesc &desc) = 0;
  /** @brief Create a graphics or compute pipeline from a descriptor. */
  virtual PipelineHandle createPipeline(const PipelineDesc &desc) = 0;
  /** @brief Create a query of the requested type. */
  virtual QueryHandle createQuery(QueryType type) = 0;
  /** @brief Destroy a buffer handle. */
  virtual void destroy(BufferHandle handle) = 0;
  /** @brief Destroy a texture handle. */
  virtual void destroy(TextureHandle handle) = 0;
  /** @brief Destroy a sampler handle. */
  virtual void destroy(SamplerHandle handle) = 0;
  /** @brief Destroy a pipeline handle. */
  virtual void destroy(PipelineHandle handle) = 0;
  /** @brief Destroy a query handle. */
  virtual void destroy(QueryHandle handle) = 0;
  /** @brief Destroy a fence handle. */
  virtual void destroy(FenceHandle handle) = 0;

  /** @brief Insert a fence into the device command stream. */
  virtual FenceHandle insertFence() = 0;
  /** @brief Query whether a fence has signaled. */
  virtual bool isFenceSignaled(FenceHandle handle) = 0;

  /** @brief Begin a render pass. */
  virtual bool beginRenderPass(const RenderPassDesc &desc) = 0;
  /** @brief End the current render pass. */
  virtual void endRenderPass() = 0;
  /** @brief Select a pipeline for subsequent commands. */
  virtual bool setPipeline(PipelineHandle handle) = 0;
  /** @brief Set the active viewport. */
  virtual bool setViewport(const Viewport &viewport) = 0;
  /** @brief Set the active scissor rectangle. */
  virtual bool setScissor(const Rect &rect) = 0;
  /** @brief Set the stencil reference value. */
  virtual bool setStencilReference(std::uint32_t reference) = 0;
  /** @brief Bind a vertex buffer at a slot and byte offset. */
  virtual bool bindVertexBuffer(std::uint32_t slot, BufferHandle handle,
                                std::uint64_t offset = 0) = 0;
  /** @brief Bind an index buffer with its index format and byte offset. */
  virtual bool bindIndexBuffer(BufferHandle handle, IndexFormat format,
                               std::uint64_t offset = 0) = 0;
  /** @brief Bind a uniform-buffer range. */
  virtual bool bindUniformBuffer(std::uint32_t slot, BufferHandle handle,
                                 std::uint64_t offset,
                                 std::uint64_t size) = 0;
  /** @brief Return the uniform-block binding index of a named block in a
   * pipeline, or -1 if the pipeline is invalid or the block is not found. */
  virtual std::int32_t uniformBlockSlot(PipelineHandle handle,
                                        const char *name) = 0;
  /** @brief Return the texture unit currently stored in a named sampler
   * uniform of a pipeline, or -1 if the pipeline is invalid or the uniform
   * is not found. */
  virtual std::int32_t textureSlot(PipelineHandle handle,
                                   const char *name) = 0;
  /** @brief Bind a sampled texture and sampler at a slot. */
  virtual bool bindTexture(std::uint32_t slot, TextureHandle texture,
                           SamplerHandle sampler) = 0;
  /**
   * @brief Register a texture and sampler in the bindless descriptor table.
   *
   * Registers a texture+sampler pair into a single large descriptor array
   * shared by every
   * pipeline, returning the index a shader uses to select it dynamically.
   * Unlike bindTexture's small fixed slots, this index is meant to be read
   * from ordinary draw data (a per-instance/per-vertex attribute or a
   * uniform), not baked into which descriptor set is bound. Returns -1 on
   * failure (capability unavailable, texture not sampled-usage, or the table
   * is full). unregisterBindlessTexture frees a slot for reuse; it does not
   * destroy the texture itself.
   */
  virtual std::int32_t registerBindlessTexture(TextureHandle texture,
                                               SamplerHandle sampler) = 0;
  /** @brief Release a bindless texture table index for reuse. */
  virtual void unregisterBindlessTexture(std::int32_t index) = 0;
  /** @brief Bind a storage-buffer range. */
  virtual bool bindStorageBuffer(std::uint32_t slot, BufferHandle handle,
                                 std::uint64_t offset,
                                 std::uint64_t size) = 0;
  /** @brief Bind a storage texture mip level. */
  virtual bool bindStorageTexture(std::uint32_t slot, TextureHandle texture,
                                  std::uint32_t mipLevel) = 0;
  /** @brief Update a byte range in a buffer. */
  virtual bool updateBuffer(BufferHandle handle, std::uint64_t offset,
                            DataView data) = 0;
  /** @brief Update a texture region from a data view and layout. */
  virtual bool updateTexture(TextureHandle handle, const TextureRegion &region,
                             DataView data,
                             const TextureDataLayout &layout = {}) = 0;
  /** @brief Query reflection information for a linked pipeline. */
  virtual bool reflectPipeline(PipelineHandle handle,
                               PipelineReflection &reflection) = 0;
  /** @brief Generate mip levels for a texture. */
  virtual bool generateMipmaps(TextureHandle handle) = 0;
  /** @brief Copy a texture region to another texture. */
  virtual bool copyTexture(TextureHandle destination,
                           const TextureOrigin &destinationOrigin,
                           TextureHandle source,
                           const TextureRegion &sourceRegion) = 0;
  /** @brief Read a texture region into caller-provided memory. */
  virtual bool readTexture(TextureHandle handle, const TextureRegion &region,
                           MutableDataView data,
                           const TextureDataLayout &layout = {}) = 0;
  /** @brief Copy bytes between two buffers. */
  virtual bool copyBuffer(BufferHandle destination, std::uint64_t destinationOffset,
                          BufferHandle source, std::uint64_t sourceOffset,
                          std::uint64_t size) = 0;
  /** @brief Map a buffer range for CPU access. */
  virtual void *mapBuffer(BufferHandle handle, std::uint64_t offset,
                          std::uint64_t size, MapMode mode) = 0;
  /** @brief End a previous buffer mapping. */
  virtual bool unmapBuffer(BufferHandle handle) = 0;
  /** @brief Issue a non-indexed draw. */
  virtual bool draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1,
                    std::uint32_t firstVertex = 0,
                    std::uint32_t firstInstance = 0) = 0;
  /** @brief Issue an indexed draw. */
  virtual bool drawIndexed(std::uint32_t indexCount,
                           std::uint32_t instanceCount = 1,
                           std::uint32_t firstIndex = 0,
                           std::int32_t baseVertex = 0,
                           std::uint32_t firstInstance = 0) = 0;
  /** @brief Insert a visibility barrier for the selected resource classes. */
  virtual bool memoryBarrier(std::uint32_t barriers) = 0;
  /** @brief Dispatch compute workgroups. */
  virtual bool dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
                        std::uint32_t groupCountZ) = 0;
  /** @brief Issue an indirect non-indexed draw. */
  virtual bool drawIndirect(BufferHandle buffer, std::uint64_t offset) = 0;
  /** @brief Issue an indirect indexed draw. */
  virtual bool drawIndexedIndirect(BufferHandle buffer,
                                   std::uint64_t offset) = 0;
  /** @brief Issue multiple indirect draws using a GPU count buffer. */
  virtual bool drawIndirectCount(BufferHandle buffer, std::uint64_t offset,
                                 BufferHandle countBuffer,
                                 std::uint64_t countOffset,
                                 std::uint32_t maxDrawCount,
                                 std::uint32_t stride) = 0;
  /** @brief Issue multiple indexed indirect draws using a GPU count buffer. */
  virtual bool drawIndexedIndirectCount(BufferHandle buffer,
                                        std::uint64_t offset,
                                        BufferHandle countBuffer,
                                        std::uint64_t countOffset,
                                        std::uint32_t maxDrawCount,
                                        std::uint32_t stride) = 0;
  /** @brief Begin an occlusion or timestamp query. */
  virtual bool beginQuery(QueryHandle handle) = 0;
  /** @brief End an active query. */
  virtual void endQuery(QueryHandle handle) = 0;
  /** @brief Write a timestamp query value. */
  virtual bool writeTimestamp(QueryHandle handle) = 0;
  /** @brief Return whether a query result can be read. */
  virtual bool isQueryResultAvailable(QueryHandle handle) = 0;
  /** @brief Read a query result into a caller-provided integer. */
  virtual bool getQueryResult(QueryHandle handle, std::uint64_t &result) = 0;
  /** @brief Return the current presentation-surface state. */
  virtual SurfaceState surfaceState() const = 0;
  /** @brief Resize the presentation surface. */
  virtual bool resizeSurface(std::uint32_t width, std::uint32_t height) = 0;
  /** @brief Temporarily suspend presentation. */
  virtual void suspendSurface() = 0;
  /** @brief Resume presentation after suspension. */
  virtual bool resumeSurface() = 0;
  /** @brief Present the current surface image. */
  virtual bool present() = 0;
};

/**
 * @brief RAII helper that ends a render pass when leaving scope.
 *
 * The wrapper is non-copyable and non-movable and stores a reference to the
 * supplied device.
 */
class RenderPassScope {
public:
  /** @brief Begin a render pass and remember whether it became active. */
  RenderPassScope(Device &device, const RenderPassDesc &desc)
      : mDevice(device), mActive(device.beginRenderPass(desc)) {}
  /** @brief End the pass if beginRenderPass succeeded. */
  ~RenderPassScope() {
    if (mActive)
      mDevice.endRenderPass();
  }

  /** @brief Copy construction is disabled. */
  RenderPassScope(const RenderPassScope &) = delete;
  /** @brief Copy assignment is disabled. */
  RenderPassScope &operator=(const RenderPassScope &) = delete;
  /** @brief Move construction is disabled. */
  RenderPassScope(RenderPassScope &&) = delete;
  /** @brief Move assignment is disabled. */
  RenderPassScope &operator=(RenderPassScope &&) = delete;

  /** @brief Return whether the render pass was successfully started. */
  explicit operator bool() const { return mActive; }

private:
  Device &mDevice;
  bool mActive;
};

} // namespace gpu

#endif
