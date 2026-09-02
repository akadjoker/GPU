#ifndef GPU_NULL_DEVICE_H
#define GPU_NULL_DEVICE_H

#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"
#include "../ResourcePool.h"

namespace gpu
{

  class NullDevice final : public Device
  {
  public:
    explicit NullDevice(const DeviceDesc &desc);

    const GPUCapabilities &capabilities() const override;
    std::uint64_t totalErrorCount() const override;
    std::uint32_t pendingErrorCount() const override;
    bool getError(::gpu::GPUError &error) override;
    void clearErrors() override;
    void shutdown() override;

    BufferHandle createBuffer(const BufferDesc &desc) override;
    TextureHandle createTexture(const TextureDesc &desc) override;
    SamplerHandle createSampler(const SamplerDesc &desc) override;
    PipelineHandle createPipeline(const PipelineDesc &desc) override;
    QueryHandle createQuery(QueryType type) override;
    void destroy(BufferHandle handle) override;
    void destroy(TextureHandle handle) override;
    void destroy(SamplerHandle handle) override;
    void destroy(PipelineHandle handle) override;
    void destroy(QueryHandle handle) override;
    void destroy(FenceHandle handle) override;

    FenceHandle insertFence() override;
    bool isFenceSignaled(FenceHandle handle) override;

    bool beginRenderPass(const RenderPassDesc &desc) override;
    void endRenderPass() override;
    bool setPipeline(PipelineHandle handle) override;
  bool setViewport(const Viewport &viewport) override;
  bool setScissor(const Rect &rect) override;
  bool setStencilReference(std::uint32_t reference) override;
    bool bindVertexBuffer(std::uint32_t slot, BufferHandle handle,
                          std::uint64_t offset) override;
    bool bindIndexBuffer(BufferHandle handle, IndexFormat format,
                         std::uint64_t offset) override;
    bool bindUniformBuffer(std::uint32_t slot, BufferHandle handle,
                           std::uint64_t offset, std::uint64_t size) override;
    std::int32_t uniformBlockSlot(PipelineHandle handle, const char *name) override;
    std::int32_t textureSlot(PipelineHandle handle, const char *name) override;
    bool bindTexture(std::uint32_t slot, TextureHandle texture,
                     SamplerHandle sampler) override;
    std::int32_t registerBindlessTexture(TextureHandle texture,
                                         SamplerHandle sampler) override;
    void unregisterBindlessTexture(std::int32_t index) override;
    bool bindStorageBuffer(std::uint32_t slot, BufferHandle handle,
                           std::uint64_t offset, std::uint64_t size) override;
    bool bindStorageTexture(std::uint32_t slot, TextureHandle texture,
                            std::uint32_t mipLevel) override;
    bool updateBuffer(BufferHandle handle, std::uint64_t offset,
                      DataView data) override;
    bool updateTexture(TextureHandle handle, const TextureRegion &region,
                       DataView data,
                       const TextureDataLayout &layout) override;
    bool reflectPipeline(PipelineHandle handle,
                         PipelineReflection &reflection) override;
    bool generateMipmaps(TextureHandle handle) override;
    bool copyTexture(TextureHandle destination,
                     const TextureOrigin &destinationOrigin,
                     TextureHandle source,
                     const TextureRegion &sourceRegion) override;
    bool readTexture(TextureHandle handle, const TextureRegion &region,
                     MutableDataView data,
                     const TextureDataLayout &layout) override;
    bool copyBuffer(BufferHandle destination, std::uint64_t destinationOffset,
                    BufferHandle source, std::uint64_t sourceOffset,
                    std::uint64_t size) override;
    void *mapBuffer(BufferHandle handle, std::uint64_t offset,
                    std::uint64_t size, MapMode mode) override;
    bool unmapBuffer(BufferHandle handle) override;
    bool draw(std::uint32_t vertexCount, std::uint32_t instanceCount,
              std::uint32_t firstVertex, std::uint32_t firstInstance) override;
    bool drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount,
                     std::uint32_t firstIndex, std::int32_t baseVertex,
                     std::uint32_t firstInstance) override;
    bool memoryBarrier(std::uint32_t barriers) override;
    bool dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
                 std::uint32_t groupCountZ) override;
    bool drawIndirect(BufferHandle buffer, std::uint64_t offset) override;
    bool drawIndexedIndirect(BufferHandle buffer,
                             std::uint64_t offset) override;
    bool drawIndirectCount(BufferHandle buffer, std::uint64_t offset,
                           BufferHandle countBuffer, std::uint64_t countOffset,
                           std::uint32_t maxDrawCount,
                           std::uint32_t stride) override;
    bool drawIndexedIndirectCount(BufferHandle buffer, std::uint64_t offset,
                                  BufferHandle countBuffer,
                                  std::uint64_t countOffset,
                                  std::uint32_t maxDrawCount,
                                  std::uint32_t stride) override;
    bool beginQuery(QueryHandle handle) override;
    void endQuery(QueryHandle handle) override;
    bool writeTimestamp(QueryHandle handle) override;
    bool isQueryResultAvailable(QueryHandle handle) override;
    bool getQueryResult(QueryHandle handle, std::uint64_t &result) override;
    SurfaceState surfaceState() const override;
    bool resizeSurface(std::uint32_t width, std::uint32_t height) override;
    void suspendSurface() override;
    bool resumeSurface() override;
    bool present() override;

  private:
    struct Slot
    {
      std::uint64_t size = 0;
      std::uint32_t usage = 0;
      Format format = Format::Unknown;
      TextureDimension dimension = TextureDimension::Texture2D;
      std::uint32_t width = 0;
      std::uint32_t height = 0;
      std::uint32_t depthOrLayers = 1;
      std::uint32_t mipCount = 1;
      std::uint32_t sampleCount = 1;
      bool isCompute = false;
      void *mappedScratch = nullptr;
      QueryType queryType = QueryType::Occlusion;
      bool queryActive = false;
      bool queryWritten = false;
    };

    template <typename HandleType>
    HandleType create(ResourcePool<Slot, HandleType> &pool,
                      ::gpu::GPUOperation operation);

    template <typename HandleType>
    bool validate(HandleType handle, const ResourcePool<Slot, HandleType> &pool,
                  ::gpu::GPUOperation operation);

    template <typename HandleType>
    void destroy(HandleType handle, ResourcePool<Slot, HandleType> &pool);

    Slot *validateAttachment(const TargetAttachment &attachment,
                             bool depthStencil,
                             ::gpu::GPUOperation operation);

    void push(::gpu::GPUErrorCode code, ::gpu::GPUOperation operation,
              std::uint64_t resource, std::uint64_t value0, std::uint64_t value1,
              const char *message);

    GPUCapabilities mCapabilities;
    ::gpu::GPUErrorQueue mErrors;
    ResourcePool<Slot, BufferHandle> mBuffers;
    ResourcePool<Slot, TextureHandle> mTextures;
    ResourcePool<Slot, SamplerHandle> mSamplers;
    ResourcePool<Slot, PipelineHandle> mPipelines;
    ResourcePool<Slot, QueryHandle> mQueries;
    ResourcePool<Slot, FenceHandle> mFences;
    QueryHandle mActiveOcclusionQuery;
    PipelineHandle mPipeline;
    BufferHandle mIndexBuffer;
    IndexFormat mIndexFormat = IndexFormat::Uint16;
    SurfaceState mSurfaceState = SurfaceState::Ready;
    std::uint32_t mSurfaceWidth = 0;
    std::uint32_t mSurfaceHeight = 0;
    bool mAlive = true;
    bool mInRenderPass = false;
    TextureHandle mPassAttachments[RenderPassDesc::MaxColorAttachments + 1];
    std::uint32_t mPassAttachmentCount = 0;
  };

  Device *createNullDevice(const DeviceDesc &desc, GPUError *error);

} // namespace gpu

#endif
