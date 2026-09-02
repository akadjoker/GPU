#ifndef GPU_GLES_DEVICE_H
#define GPU_GLES_DEVICE_H

#include "../gl/GLSurface.h"
#include "../ResourcePool.h"
#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"

#include <GLES3/gl32.h>

namespace gpu
{

  class GLESDevice final : public Device
  {
  public:
    explicit GLESDevice(const DeviceDesc &desc);
    ~GLESDevice() override;

    bool initialize();
    const GPUCapabilities &capabilities() const override;
    std::uint64_t totalErrorCount() const override;
    std::uint32_t pendingErrorCount() const override;
    bool getError(GPUError &error) override;
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
    struct BufferObject
    {
      GLuint id = 0;
      std::uint64_t size = 0;
      std::uint32_t usage = 0;
      GLenum target = GL_ARRAY_BUFFER;
      bool mapped = false;
    };

    struct TextureObject
    {
      GLuint id = 0;
      GLenum target = GL_TEXTURE_2D;
      Format format = Format::Unknown;
      std::uint32_t width = 0;
      std::uint32_t height = 0;
      std::uint32_t depthOrLayers = 1;
      std::uint32_t mipCount = 1;
      std::uint32_t sampleCount = 1;
      std::uint32_t usage = 0;
    };

    struct SamplerObject
    {
      GLuint id = 0;
    };

    struct PipelineObject
    {
      GLuint program = 0;
      GLuint vertexArray = 0;
      VertexBufferLayout vertexBuffers[PipelineDesc::MaxVertexBuffers];
      RasterState raster;
      DepthStencilState depthStencil;
      ColorTargetState colorTargets[PipelineDesc::MaxColorTargets];
      std::uint32_t vertexBufferCount = 0;
      std::uint32_t colorTargetCount = 0;
      GLenum topology = GL_TRIANGLES;
      bool isCompute = false;
    };

    struct QueryObject
    {
      GLuint id = 0;
      QueryType type = QueryType::Occlusion;
      bool active = false;
      bool written = false;
    };

    struct FenceObject
    {
      GLsync sync = nullptr;
    };

    template <typename HandleType, typename Object>
    HandleType insert(ResourcePool<Object, HandleType> &pool,
                      const Object &object,
                      GPUOperation operation);

    template <typename HandleType, typename Object>
    Object *find(HandleType handle, ResourcePool<Object, HandleType> &pool,
                 GPUOperation operation);

    template <typename HandleType, typename Object>
    const Object *find(HandleType handle,
                       const ResourcePool<Object, HandleType> &pool,
                       GPUOperation operation);

    template <typename HandleType, typename Object>
    void release(HandleType handle, ResourcePool<Object, HandleType> &pool,
                 GPUOperation operation);

    struct FramebufferAttachment
    {
      GLuint texture = 0;
      GLenum target = 0;
      std::uint32_t mipLevel = 0;
      std::uint32_t layer = 0;
      bool stencil = false;
    };

    struct FramebufferKey
    {
      FramebufferAttachment colors[RenderPassDesc::MaxColorAttachments];
      FramebufferAttachment depthStencil;
      std::uint32_t colorCount = 0;
      bool hasDepthStencil = false;
    };

    struct FramebufferCacheEntry
    {
      FramebufferKey key;
      GLuint framebuffer = 0;
      std::uint64_t lastUse = 0;
    };

    static constexpr std::uint32_t FramebufferCacheSize = 16;

    GLuint acquireFramebuffer(const FramebufferKey &key);
    void evictFramebuffers(GLuint texture);
    void destroyFramebufferCache();
    void drainDriverErrors();
    void setObjectLabel(GLenum identifier, GLuint name, const char *label);
    char *nextDiagnostic();
    static void GL_APIENTRY debugMessage(GLenum source, GLenum type,
                                             GLuint id, GLenum severity,
                                             GLsizei length,
                                             const GLchar *message,
                                             const void *userData);
    void assignSamplerUnits(GLuint program);
    void assignUniformBlockBindings(GLuint program);
    GLuint compileShader(GLenum stage, const ShaderDesc &desc);
    PipelineHandle createComputePipeline(const PipelineDesc &desc);
    void push(GPUErrorCode code, GPUOperation operation, std::uint64_t resource,
              std::uint64_t value0, std::uint64_t value1, const char *message);

    const GLSurface *mSurface = nullptr;
    GPUCapabilities mCapabilities;
    GPUErrorQueue mErrors;
    ResourcePool<BufferObject, BufferHandle> mBuffers;
    ResourcePool<TextureObject, TextureHandle> mTextures;
    ResourcePool<SamplerObject, SamplerHandle> mSamplers;
    ResourcePool<PipelineObject, PipelineHandle> mPipelines;
    ResourcePool<QueryObject, QueryHandle> mQueries;
    ResourcePool<FenceObject, FenceHandle> mFences;
    QueryHandle mActiveOcclusionQuery;
    PipelineHandle mPipeline;
    BufferHandle mIndexBuffer;
    GLenum mIndexType = GL_UNSIGNED_SHORT;
    std::uint64_t mIndexOffset = 0;
    GLuint mCopyReadFramebuffer = 0;
    GLuint mCopyDrawFramebuffer = 0;
    SurfaceState mSurfaceState = SurfaceState::Ready;
    std::uint32_t mSurfaceWidth = 0;
    std::uint32_t mSurfaceHeight = 0;
    std::uint32_t mRenderWidth = 0;
    std::uint32_t mRenderHeight = 0;
    std::uint32_t mMaxStorageTextureBindings = 0;
    std::uint32_t mMaxDrawBuffers = 1;
    bool mDebugLabels = false;
    std::uint32_t mMaxLabelLength = 0;
    FramebufferCacheEntry mFramebufferCache[FramebufferCacheSize];
    std::uint32_t mFramebufferCacheCount = 0;
    std::uint64_t mFramebufferCacheClock = 0;
    bool mAnisotropicFiltering = false;
    bool mAlive = false;
    bool mInRenderPass = false;
    // Texturas ligadas como attachment na passada atual. Bindar uma delas como
    // textura amostrada e um feedback loop: undefined behaviour em GL/GLES.
    TextureHandle mPassAttachments[RenderPassDesc::MaxColorAttachments + 1];
    std::uint32_t mPassAttachmentCount = 0;
    bool mRenderSurface = false;
    bool mHasES31 = false;
    bool mHasES32 = false;
    GLenum mDiscardAttachments[RenderPassDesc::MaxColorAttachments + 2] = {};
    std::uint32_t mDiscardAttachmentCount = 0;
    static constexpr std::uint32_t ShaderDiagnosticSlots = 8;
    static constexpr std::uint32_t ShaderDiagnosticSize = 1024;
    char mShaderDiagnostic[ShaderDiagnosticSlots][ShaderDiagnosticSize] = {};
    std::uint32_t mShaderDiagnosticSlot = 0;
  };

  Device *createOpenGLESDevice(const DeviceDesc &desc);

} // namespace gpu

#endif
