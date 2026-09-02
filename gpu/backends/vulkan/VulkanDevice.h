#ifndef GPU_VULKAN_DEVICE_H
#define GPU_VULKAN_DEVICE_H

#include "VulkanSurface.h"
#include "../ResourcePool.h"
#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"

#include <vector>

namespace gpu
{

  template <typename T>
  using Vector = std::vector<T>;

  class VulkanDevice final : public Device
  {
  public:
    explicit VulkanDevice(const DeviceDesc &desc);
    ~VulkanDevice() override;

    bool initialize(GPUError *error);

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
    bool updateBuffer(BufferHandle handle, std::uint64_t offset, DataView data) override;
    bool updateTexture(TextureHandle handle, const TextureRegion &region,
                       DataView data, const TextureDataLayout &layout) override;
    bool reflectPipeline(PipelineHandle handle, PipelineReflection &reflection) override;
    bool generateMipmaps(TextureHandle handle) override;
    bool copyTexture(TextureHandle destination, const TextureOrigin &destinationOrigin,
                     TextureHandle source, const TextureRegion &sourceRegion) override;
    bool readTexture(TextureHandle handle, const TextureRegion &region,
                     MutableDataView data, const TextureDataLayout &layout) override;
    bool copyBuffer(BufferHandle destination, std::uint64_t destinationOffset, BufferHandle source, std::uint64_t sourceOffset, std::uint64_t size) override;
    void *mapBuffer(BufferHandle handle, std::uint64_t offset, std::uint64_t size, MapMode mode) override;
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
    bool drawIndexedIndirect(BufferHandle buffer, std::uint64_t offset) override;
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
    template <typename T>
    T unsupported(GPUOperation operation)
    {
      push(GPUErrorCode::UnsupportedFeature, operation,
           "Vulkan operation is not implemented in this phase");
      return T{};
    }

    void unsupported(GPUOperation operation);
    bool unsupportedBool(GPUOperation operation);
    void push(GPUErrorCode code, GPUOperation operation, const char *message,
              std::uint64_t value0 = 0, std::uint64_t value1 = 0);
    struct BufferObject
    {
      VkBuffer buffer = VK_NULL_HANDLE;
      VkDeviceMemory memory = VK_NULL_HANDLE;
      void *mapped = nullptr;
      std::uint64_t size = 0;
      std::uint32_t usage = 0;
    };

    struct SamplerObject
    {
      VkSampler sampler = VK_NULL_HANDLE;
    };

    struct TextureObject
    {
      VkImage image = VK_NULL_HANDLE;
      VkImageView view = VK_NULL_HANDLE;
      VkDeviceMemory memory = VK_NULL_HANDLE;
      Format format = Format::Unknown;
      std::uint32_t width = 0;
      std::uint32_t height = 0;
      std::uint32_t mipCount = 1;
      std::uint32_t layerCount = 1;
      std::uint32_t usage = 0;
      VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
      VkImageLayout mipLayouts[32] = {};
      // A VK_DESCRIPTOR_TYPE_STORAGE_IMAGE descriptor binds one specific
      // mip level, but `view` above spans every level (needed for
      // sampling/color-attachment use) - Vulkan requires every subresource
      // covered by a bound view to actually be in the descriptor's
      // declared layout, so writing a storage-image descriptor with the
      // full-range `view` while only mip 0's layout was transitioned
      // trips validation on the untouched mips as soon as a texture has
      // mipCount > 1 (see doc/PLAN.md, 25_hdr). One single-level view per
      // mip, created only when TextureUsageStorage is set, fixes that.
      VkImageView storageViews[32] = {};
    };

    struct FenceObject
    {
      VkFence fence = VK_NULL_HANDLE;
    };

    struct QueryObject
    {
      VkQueryPool pool = VK_NULL_HANDLE;
      QueryType type = QueryType::Occlusion;
      bool active = false;
      bool written = false;
    };

    struct PipelineObject
    {
      VkPipeline pipeline = VK_NULL_HANDLE;
      VkPipelineLayout layout = VK_NULL_HANDLE;
      VkFormat colorFormats[PipelineDesc::MaxColorTargets] = {};
      std::uint32_t colorTargetCount = 0;
      VkFormat depthFormat = VK_FORMAT_UNDEFINED;
      bool stencilEnabled = false;
      std::uint32_t vertexBufferCount = 0;
      bool isCompute = false;
      // Computed once from the SPIR-V modules at creation time (see
      // reflectSpirvStage() in VulkanDevice.cpp) rather than kept live -
      // the shader modules themselves don't survive pipeline creation.
      PipelineReflection reflection;
    };

    struct UniformBinding
    {
      BufferHandle handle;
      std::uint64_t offset = 0;
      std::uint64_t size = 0;
    };

    struct TextureBinding
    {
      TextureHandle texture;
      SamplerHandle sampler;
    };

    struct StorageBufferBinding
    {
      BufferHandle handle;
      std::uint64_t offset = 0;
      std::uint64_t size = 0;
    };

    struct StorageTextureBinding
    {
      TextureHandle texture;
      std::uint32_t mipLevel = 0;
    };

    bool createInstance();
    bool createDebugMessenger();
    void destroyDebugMessenger();
    char *nextValidationDiagnostic();
    static VkBool32 VKAPI_PTR validationCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT *data, void *userData);
    bool createSurface();
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createSwapchain();
    void destroySwapchain();
    bool recreateSwapchain();
    bool acquireSwapchainImage();
    bool beginSwapchainCommands(VkCommandBuffer &commandBuffer);
    bool submitSwapchainCommands(VkCommandBuffer commandBuffer);
    void transitionSwapchainImage(VkCommandBuffer commandBuffer,
                                  std::uint32_t imageIndex,
                                  VkImageLayout newLayout);
    bool createDescriptors();
    bool createDescriptorPool();
    bool createBindlessTextureSet();
    void destroyDescriptors();
    bool allocateDescriptorSet(VkDescriptorSetLayout layout,
                              VkDescriptorSet &set);
    PipelineHandle createComputePipeline(const PipelineDesc &desc);
    bool beginCommands(VkCommandBuffer &commandBuffer);
    bool submitCommands(VkCommandBuffer commandBuffer);
    bool createTransferBuffer(VkDeviceSize size, VkBuffer &buffer,
                              VkDeviceMemory &memory, void *&mapped);
    void transitionImage(VkCommandBuffer commandBuffer, TextureObject &texture,
                         VkImageLayout newLayout);
    void transitionImageMip(VkCommandBuffer commandBuffer, TextureObject &texture,
                            std::uint32_t mipLevel, VkImageLayout newLayout);
    std::uint32_t findMemoryType(std::uint32_t typeBits,
                                 VkMemoryPropertyFlags properties) const;
    // Resources can still be referenced by a swapchain command buffer whose
    // completion present() deliberately doesn't wait for (that wait is
    // deferred to the next acquireSwapchainImage(), to keep present()
    // non-blocking). Destroying a resource in between is otherwise a race,
    // so every destroy() waits on the in-flight fence first; if the frame
    // already finished (the common case) the wait returns immediately.
    void waitForGPU();
    void destroyBuffers();
    void destroySamplers();
    void destroyTextures();
    void destroyPipelines();
    void destroyQueries();
    void destroyFences();

    DeviceDesc mDesc;
    const VulkanSurface *mSurface = nullptr;
    GPUCapabilities mCapabilities;
    GPUErrorQueue mErrors;
    ResourcePool<BufferObject, BufferHandle> mBuffers;
    ResourcePool<SamplerObject, SamplerHandle> mSamplers;
    ResourcePool<TextureObject, TextureHandle> mTextures;
    ResourcePool<PipelineObject, PipelineHandle> mPipelines;
    ResourcePool<QueryObject, QueryHandle> mQueries;
    ResourcePool<FenceObject, FenceHandle> mFences;
    static constexpr std::uint32_t MaxUniformBindings = 16;
    static constexpr std::uint32_t MaxTextureBindings = 32;
    static constexpr std::uint32_t MaxStorageBufferBindings = 8;
    static constexpr std::uint32_t MaxStorageTextureBindings = 8;
    // Descriptor indexing: one big descriptor array shared by every
    // pipeline (set 4), unlike the small fixed slots above. Unrelated to
    // MaxTextureBindings, which stays the ordinary bindTexture() path.
    static constexpr std::uint32_t MaxBindlessTextures = 1024;
    static constexpr std::uint32_t ValidationDiagnosticSlots = 4;
    static constexpr std::uint32_t ValidationDiagnosticSize = 1024;
    char mValidationDiagnostic[ValidationDiagnosticSlots][ValidationDiagnosticSize] = {};
    std::uint32_t mValidationDiagnosticSlot = 0;
    VkInstance mInstance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT mDebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR mVkSurface = VK_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mMemoryProperties = {};
    VkDevice mDevice = VK_NULL_HANDLE;
    VkQueue mQueue = VK_NULL_HANDLE;
    VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
    Vector<VkImage> mSwapchainImages;
    Vector<VkImageView> mSwapchainViews;
    Vector<VkImageLayout> mSwapchainLayouts;
    Vector<VkCommandBuffer> mCommandBuffers;
    VkCommandPool mCommandPool = VK_NULL_HANDLE;
    VkSemaphore mImageAvailable = VK_NULL_HANDLE;
    VkSemaphore mRenderFinished = VK_NULL_HANDLE;
    VkFence mInFlight = VK_NULL_HANDLE;
    // Reused by submitCommands() for every one-off upload/copy/mip/query
    // submission instead of a fresh vkCreateFence+vkDestroyFence per call.
    VkFence mTransferFence = VK_NULL_HANDLE;
    VkDescriptorSetLayout mUniformSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout mTextureSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout mStorageBufferSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout mStorageImageSetLayout = VK_NULL_HANDLE;
    Vector<VkDescriptorPool> mDescriptorPools;
    // Bindless texture array (set 4): allocated and written once, unlike
    // every other set above which is reallocated fresh on every bind*()
    // call - see registerBindlessTexture()/createDescriptors().
    VkDescriptorSetLayout mBindlessTextureSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool mBindlessDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet mBindlessTextureSet = VK_NULL_HANDLE;
    Vector<std::uint32_t> mBindlessFreeList;
    std::uint32_t mBindlessNextSlot = 0;
    std::uint32_t mQueueFamily = 0;
    std::uint32_t mWidth = 0;
    std::uint32_t mHeight = 0;
    VkFormat mSwapchainFormat = VK_FORMAT_UNDEFINED;
    std::uint32_t mSwapchainImage = UINT32_MAX;
    VkCommandBuffer mActiveCommandBuffer = VK_NULL_HANDLE;
    PipelineHandle mActivePipeline;
    QueryHandle mActiveQuery;
    BufferHandle mIndexBuffer;
    IndexFormat mIndexFormat = IndexFormat::Uint16;
    TextureHandle mPassColors[RenderPassDesc::MaxColorAttachments];
    TextureHandle mPassDepth;
    std::uint32_t mPassColorCount = 0;
    std::uint32_t mPassWidth = 0;
    std::uint32_t mPassHeight = 0;
    UniformBinding mUniformBindings[MaxUniformBindings];
    TextureBinding mTextureBindings[MaxTextureBindings];
    StorageBufferBinding mStorageBufferBindings[MaxStorageBufferBindings];
    StorageTextureBinding mStorageTextureBindings[MaxStorageTextureBindings];
    bool mSamplerAnisotropySupported = false;
    float mMaxSamplerAnisotropy = 1.0f;
    // Sets 0-3 (and 4, when bindless) use the same VkDescriptorSetLayout
    // objects across every graphics pipeline (see createPipeline()), so per
    // the Vulkan pipeline-layout-compatibility rule, switching pipelines
    // never invalidates descriptor sets already bound in the current
    // command buffer. setPipeline() only needs to replay the sticky
    // uniform/texture bindings once per fresh command buffer (tracked by
    // this flag, reset in beginRenderPass()) instead of on every pipeline
    // switch within the same render pass.
    bool mDescriptorsBoundInPass = false;
    bool mInRenderPass = false;
    bool mPassUsesSurface = false;
    bool mFramePendingPresent = false;
    SurfaceState mSurfaceState = SurfaceState::Suspended;
    bool mAlive = false;
  };

  Device *createVulkanDevice(const DeviceDesc &desc, GPUError *error);

} // namespace gpu

#endif
