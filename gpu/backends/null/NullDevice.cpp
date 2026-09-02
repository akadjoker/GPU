#include "NullDevice.h"

#include "gpu/GPUBackend.h"

#include <cstdlib>
#include <cstring>
#include <limits>

namespace gpu
{

  namespace
  {

    bool depthFormat(Format format)
    {
      return format == Format::Depth16 || format == Format::Depth24 ||
             format == Format::Depth32Float ||
             format == Format::Depth24Stencil8;
    }

    std::uint32_t compressedBlockBytes(Format format)
    {
      switch (format)
      {
      case Format::BC1RGBA:
      case Format::BC1RGBASrgb:
        return 8;
      case Format::BC3RGBA:
      case Format::BC3RGBASrgb:
      case Format::BC5RG:
      case Format::BC7RGBA:
      case Format::BC7RGBASrgb:
      case Format::ETC2RGBA8:
      case Format::ETC2RGBA8Srgb:
      case Format::ASTC4x4RGBA:
      case Format::ASTC4x4RGBASrgb:
        return 16;
      default:
        return 0;
      }
    }

    std::uint32_t uncompressedBytesPerTexel(Format format)
    {
      switch (format)
      {
      case Format::R8:
        return 1;
      case Format::RG8:
      case Format::R16Float:
      case Format::R16Uint:
      case Format::Depth16:
        return 2;
      case Format::RGBA8:
      case Format::RGBA8Srgb:
      case Format::RG16Float:
      case Format::R32Float:
      case Format::R11G11B10Float:
      case Format::RGB10A2:
      case Format::R32Uint:
      case Format::RG16Uint:
      case Format::Depth24:
      case Format::Depth32Float:
      case Format::Depth24Stencil8:
        return 4;
      case Format::RGBA16Float:
      case Format::RGBA16Uint:
      case Format::RG32Float:
      case Format::RG32Uint:
        return 8;
      case Format::RGB32Float:
        return 12;
      case Format::RGBA32Float:
      case Format::RGBA32Uint:
        return 16;
      default:
        return 0;
      }
    }

    bool requiredUploadSize(std::uint64_t offset, std::uint64_t bytesPerRow,
                            std::uint64_t rowsPerImage,
                            std::uint64_t copiedRows,
                            std::uint64_t tightRowBytes,
                            std::uint64_t images, std::uint64_t &required)
    {
      const std::uint64_t max =
          (std::numeric_limits<std::uint64_t>::max)();
      if (rowsPerImage > max / bytesPerRow)
        return false;
      const std::uint64_t imageStride = rowsPerImage * bytesPerRow;
      if (images - 1 > (max - offset) / imageStride)
        return false;
      required = offset + (images - 1) * imageStride;
      if (copiedRows - 1 > (max - required) / bytesPerRow)
        return false;
      required += (copiedRows - 1) * bytesPerRow;
      if (tightRowBytes > max - required)
        return false;
      required += tightRowBytes;
      return true;
    }

    bool compressedFormatSupported(Format format,
                                   const GPUCapabilities &capabilities)
    {
      switch (format)
      {
      case Format::BC1RGBA:
      case Format::BC1RGBASrgb:
        return capabilities.textureCompressionBC1;
      case Format::BC3RGBA:
      case Format::BC3RGBASrgb:
        return capabilities.textureCompressionBC3;
      case Format::BC5RG:
        return capabilities.textureCompressionBC5;
      case Format::BC7RGBA:
      case Format::BC7RGBASrgb:
        return capabilities.textureCompressionBC7;
      case Format::ETC2RGBA8:
      case Format::ETC2RGBA8Srgb:
        return capabilities.textureCompressionETC2;
      case Format::ASTC4x4RGBA:
      case Format::ASTC4x4RGBASrgb:
        return capabilities.textureCompressionASTC;
      default:
        return true;
      }
    }

    bool compressedBaseSize(const TextureDesc &desc, std::uint32_t blockBytes,
                            std::uint64_t &size)
    {
      std::uint64_t images = 1;
      if (desc.dimension == TextureDimension::TextureCube)
        images = 6;
      else if (desc.dimension == TextureDimension::Texture2DArray)
        images = desc.depthOrLayers;
      const std::uint64_t blocksWide =
          (static_cast<std::uint64_t>(desc.width) + 3) / 4;
      const std::uint64_t blocksHigh =
          (static_cast<std::uint64_t>(desc.height) + 3) / 4;
      const std::uint64_t max =
          (std::numeric_limits<std::uint64_t>::max)();
      if (blocksWide > max / blocksHigh)
        return false;
      size = blocksWide * blocksHigh;
      if (size > max / images)
        return false;
      size *= images;
      if (size > max / blockBytes)
        return false;
      size *= blockBytes;
      return true;
    }

    std::uint32_t maximumMipCount(const TextureDesc &desc)
    {
      std::uint32_t largest = desc.width > desc.height ? desc.width : desc.height;
      if (desc.dimension == TextureDimension::Texture3D &&
          desc.depthOrLayers > largest)
        largest = desc.depthOrLayers;
      std::uint32_t count = 0;
      while (largest != 0)
      {
        ++count;
        largest >>= 1;
      }
      return count;
    }

    std::uint32_t mipDimension(std::uint32_t value, std::uint32_t mipLevel)
    {
      const std::uint32_t shifted = value >> mipLevel;
      return shifted == 0 ? 1 : shifted;
    }

    bool validAttachmentLayer(TextureDimension dimension,
                              std::uint32_t depthOrLayers,
                              std::uint32_t mipLevel, std::uint32_t layer)
    {
      switch (dimension)
      {
      case TextureDimension::Texture2D:
        return layer == 0;
      case TextureDimension::TextureCube:
        return layer < 6;
      case TextureDimension::Texture2DArray:
        return layer < depthOrLayers;
      case TextureDimension::Texture3D:
        return layer < mipDimension(depthOrLayers, mipLevel);
      }
      return false;
    }

    GPUCapabilities nullCapabilities(RendererProfile profile)
    {
      GPUCapabilities capabilities;
      capabilities.textureArrays = true;
      capabilities.anisotropicFiltering = true;
      capabilities.maxAnisotropy = 16.0f;
      capabilities.maxColorAttachments = 8;
      capabilities.maxTextureDimension2D = 16384;
      capabilities.maxTextureDimension3D = 2048;
      capabilities.maxTextureArrayLayers = 2048;
      capabilities.maxTextureBindings = 32;
      capabilities.maxSampleCount = 8;
      capabilities.independentBlend = true;
      capabilities.maxUniformBufferBindings = 16;
      capabilities.maxStorageBufferBindings = 8;
      capabilities.maxUniformBufferSize = 65536;
      capabilities.uniformBufferOffsetAlignment = 256;
      capabilities.storageBufferOffsetAlignment = 256;
      if (profile == RendererProfile::Modern)
      {
        capabilities.compute = true;
        capabilities.storageBuffers = true;
        capabilities.storageTextures = true;
        capabilities.memoryBarriers = true;
        capabilities.indirectDraw = true;
        capabilities.indirectCount = true;
        capabilities.asyncReadback = true;
        capabilities.depthReadback = true;
        capabilities.timestampQueries = true;
        capabilities.occlusionQueries = true;
        capabilities.baseInstance = true;
      }
      return capabilities;
    }

  } // namespace

  NullDevice::NullDevice(const DeviceDesc &desc)
      : mCapabilities(nullCapabilities(desc.profile)),
        mSurfaceWidth(desc.surface.width), mSurfaceHeight(desc.surface.height) {}

  const GPUCapabilities &NullDevice::capabilities() const
  {
    return mCapabilities;
  }

  std::uint64_t NullDevice::totalErrorCount() const
  {
    return mErrors.totalErrorCount();
  }

  std::uint32_t NullDevice::pendingErrorCount() const
  {
    return mErrors.pendingErrorCount();
  }

  bool NullDevice::getError(::gpu::GPUError &error)
  {
    return mErrors.getError(error);
  }

  void NullDevice::clearErrors() { mErrors.clearErrors(); }

  void NullDevice::shutdown()
  {
    mAlive = false;
    mInRenderPass = false;
    mPipeline = PipelineHandle();
    mIndexBuffer = BufferHandle();
    mActiveOcclusionQuery = QueryHandle();
    mSurfaceState = SurfaceState::Lost;
    mBuffers.forEach([](Slot &buffer) { std::free(buffer.mappedScratch); });
    mBuffers.clear();
    mTextures.clear();
    mSamplers.clear();
    mPipelines.clear();
    mQueries.clear();
    mFences.clear();
  }

  template <typename HandleType>
  HandleType NullDevice::create(ResourcePool<Slot, HandleType> &pool,
                                ::gpu::GPUOperation operation)
  {
    if (!mAlive)
    {
      push(::gpu::GPUErrorCode::DeviceLost, operation, 0, 0, 0,
           "device is shut down");
      return HandleType();
    }

    const HandleType handle = pool.insert(Slot{});
    if (handle.valid())
      return handle;
    push(::gpu::GPUErrorCode::OutOfMemory, operation, 0, pool.size(), 0,
         "resource pool allocation failed");
    return {};
  }

  template <typename HandleType>
  bool NullDevice::validate(HandleType handle,
                            const ResourcePool<Slot, HandleType> &pool,
                            ::gpu::GPUOperation operation)
  {
    const std::uint64_t value = handle.value();
    if (!pool.find(handle))
    {
      push(::gpu::GPUErrorCode::InvalidHandle, operation, value, 0, 0,
           "invalid resource handle");
      return false;
    }
    return true;
  }

  template <typename HandleType>
  void NullDevice::destroy(HandleType handle,
                           ResourcePool<Slot, HandleType> &pool)
  {
    if (!validate(handle, pool, ::gpu::GPUOperation::Destroy))
      return;
    pool.erase(handle);
  }

  NullDevice::Slot *NullDevice::validateAttachment(
      const TargetAttachment &attachment, bool depthStencil,
      ::gpu::GPUOperation operation)
  {
    if (!validate(attachment.texture, mTextures, operation))
      return nullptr;
    Slot *texture = mTextures.find(attachment.texture);
    if (!(texture->usage & TextureUsageRenderTarget) ||
        depthFormat(texture->format) != depthStencil ||
        attachment.mipLevel >= texture->mipCount ||
        !validAttachmentLayer(texture->dimension, texture->depthOrLayers,
                              attachment.mipLevel, attachment.layer))
    {
      push(::gpu::GPUErrorCode::InvalidArgument, operation,
           attachment.texture.value(), attachment.mipLevel, attachment.layer,
           depthStencil ? "invalid depth stencil attachment"
                        : "invalid color attachment");
      return nullptr;
    }
    return texture;
  }

  void NullDevice::push(::gpu::GPUErrorCode code, ::gpu::GPUOperation operation,
                        std::uint64_t resource, std::uint64_t value0,
                        std::uint64_t value1, const char *message)
  {
    ::gpu::GPUError error;
    error.code = code;
    error.operation = operation;
    error.resource = resource;
    error.value0 = value0;
    error.value1 = value1;
    error.message = message;
    mErrors.push(error);
  }

  BufferHandle NullDevice::createBuffer(const BufferDesc &desc)
  {
    if (desc.size == 0 || desc.usage == 0 ||
        (desc.initialData.data != nullptr && desc.initialData.size > desc.size))
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::CreateBuffer, 0, desc.size, desc.initialData.size,
           "invalid buffer descriptor");
      return BufferHandle();
    }
    if ((desc.usage & BufferUsageStorage) && !mCapabilities.storageBuffers)
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature,
           ::gpu::GPUOperation::CreateBuffer, 0, desc.usage, 0,
           "storage buffers are unavailable on this device");
      return BufferHandle();
    }
    if ((desc.usage & BufferUsageIndirect) && !mCapabilities.indirectDraw)
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature,
           ::gpu::GPUOperation::CreateBuffer, 0, desc.usage, 0,
           "indirect draw buffers are unavailable on this device");
      return BufferHandle();
    }
    const BufferHandle handle =
        create<BufferHandle>(mBuffers, ::gpu::GPUOperation::CreateBuffer);
    if (handle.valid())
    {
      Slot *buffer = mBuffers.find(handle);
      buffer->size = desc.size;
      buffer->usage = desc.usage;
    }
    return handle;
  }

  TextureHandle NullDevice::createTexture(const TextureDesc &desc)
  {
    const std::uint32_t blockBytes = compressedBlockBytes(desc.format);
    const bool compressed = blockBytes != 0;
    if (!compressed && uncompressedBytesPerTexel(desc.format) == 0)
    {
      push(::gpu::GPUErrorCode::UnsupportedFormat,
           ::gpu::GPUOperation::CreateTexture, 0,
           static_cast<std::uint64_t>(desc.format), 0,
           "texture format is unsupported");
      return TextureHandle();
    }
    if (compressed && !compressedFormatSupported(desc.format, mCapabilities))
    {
      push(::gpu::GPUErrorCode::UnsupportedFormat,
           ::gpu::GPUOperation::CreateTexture, 0,
           static_cast<std::uint64_t>(desc.format), 0,
           "compressed texture format is unavailable on this device");
      return TextureHandle();
    }
    if (desc.width == 0 || desc.height == 0 || desc.depthOrLayers == 0 ||
        desc.mipCount == 0 || desc.sampleCount == 0 || desc.usage == 0)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::CreateTexture, 0, desc.width, desc.height,
           "invalid texture descriptor");
      return TextureHandle();
    }
    const bool hasInitialData = desc.initialData.data != nullptr;
    const bool invalidInitialData =
        hasInitialData != (desc.initialData.size != 0);
    const bool invalid2D = desc.dimension == TextureDimension::Texture2D &&
                           desc.depthOrLayers != 1;
    const bool invalidCube = desc.dimension == TextureDimension::TextureCube &&
                             (desc.depthOrLayers != 1 ||
                              desc.width != desc.height);
    const bool invalidArray =
        desc.dimension == TextureDimension::Texture2DArray &&
        desc.depthOrLayers > mCapabilities.maxTextureArrayLayers;
    const bool invalid3D = desc.dimension == TextureDimension::Texture3D &&
                           desc.depthOrLayers >
                               mCapabilities.maxTextureDimension3D;
    const bool invalidSamples =
        desc.sampleCount > 1 &&
        (desc.dimension != TextureDimension::Texture2D || desc.mipCount != 1 ||
         hasInitialData || !(desc.usage & TextureUsageRenderTarget));
    std::uint64_t compressedSize = 0;
    const bool invalidCompressed =
        compressed &&
        (desc.dimension == TextureDimension::Texture3D ||
         desc.sampleCount > 1 || (desc.usage & TextureUsageRenderTarget) ||
         (desc.usage & TextureUsageStorage));
    const bool invalidCompressedData =
        compressed && hasInitialData &&
        (!compressedBaseSize(desc, blockBytes, compressedSize) ||
         desc.initialData.size < compressedSize);
    if (desc.width > mCapabilities.maxTextureDimension2D ||
        desc.height > mCapabilities.maxTextureDimension2D || invalid2D ||
        invalidCube || invalidArray || invalid3D || invalidSamples ||
        invalidCompressed || invalidCompressedData || invalidInitialData ||
        desc.mipCount > maximumMipCount(desc) ||
        desc.sampleCount > mCapabilities.maxSampleCount ||
        (desc.sampleCount & (desc.sampleCount - 1)) != 0)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::CreateTexture, 0, desc.width, desc.height,
           "invalid texture descriptor");
      return TextureHandle();
    }
    const TextureHandle handle =
        create<TextureHandle>(mTextures, ::gpu::GPUOperation::CreateTexture);
    if (handle.valid())
    {
      Slot *texture = mTextures.find(handle);
      texture->usage = desc.usage;
      texture->format = desc.format;
      texture->dimension = desc.dimension;
      texture->width = desc.width;
      texture->height = desc.height;
      texture->depthOrLayers = desc.depthOrLayers;
      texture->mipCount = desc.mipCount;
      texture->sampleCount = desc.sampleCount;
    }
    return handle;
  }

  SamplerHandle NullDevice::createSampler(const SamplerDesc &desc)
  {
    const bool wantsBorder = desc.addressU == AddressMode::ClampToBorder ||
                             desc.addressV == AddressMode::ClampToBorder ||
                             desc.addressW == AddressMode::ClampToBorder;
    if (wantsBorder && !mCapabilities.samplerBorderColor)
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature,
           ::gpu::GPUOperation::CreateSampler, 0, 0, 0,
           "border address mode is unavailable on this device");
      return SamplerHandle();
    }
    if (desc.maxAnisotropy < 1.0f || desc.lodMin < 0.0f ||
        desc.lodMax < desc.lodMin)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::CreateSampler, 0, 0, 0,
           "invalid sampler descriptor");
      return SamplerHandle();
    }
    return create<SamplerHandle>(mSamplers, ::gpu::GPUOperation::CreateSampler);
  }

  PipelineHandle NullDevice::createPipeline(const PipelineDesc &desc)
  {
    const bool hasCompute =
        desc.compute.source.data != nullptr && desc.compute.source.size != 0;
    const bool hasGraphics =
        desc.vertex.source.data != nullptr && desc.vertex.source.size != 0 &&
        desc.fragment.source.data != nullptr && desc.fragment.source.size != 0;
    if (hasCompute == hasGraphics)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::CreatePipeline, 0, 0, 0,
           "pipeline requires one shader mode");
      return PipelineHandle();
    }
    const ShaderDesc *stages[3] = {&desc.vertex, &desc.fragment,
                                   &desc.compute};
    for (const ShaderDesc *stage : stages)
      if (stage->source.data != nullptr && stage->entryPoint != nullptr &&
          std::strcmp(stage->entryPoint, "main") != 0)
      {
        push(::gpu::GPUErrorCode::UnsupportedFeature,
             ::gpu::GPUOperation::CreatePipeline, 0, 0, 0,
             "GLSL shaders must use \"main\" as the entry point");
        return PipelineHandle();
      }
    if (hasCompute && !mCapabilities.compute)
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature,
           ::gpu::GPUOperation::CreatePipeline, 0, 0, 0,
           "compute is unavailable in the selected profile");
      return PipelineHandle();
    }
    // This backend never reports tessellationShader/geometryShader
    // capability - reject a request for those stages (or Topology::Patches
    // without them) instead of silently accepting a descriptor no real
    // backend could actually run, matching the Vulkan backend's validation.
    if (desc.tessControl.source.data || desc.tessEvaluation.source.data ||
        desc.geometry.source.data || desc.topology == Topology::Patches)
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature,
           ::gpu::GPUOperation::CreatePipeline, 0, 0, 0,
           "tessellation and geometry shaders are unavailable on this device");
      return PipelineHandle();
    }
    if (desc.vertexBufferCount > PipelineDesc::MaxVertexBuffers ||
        desc.colorTargetCount > PipelineDesc::MaxColorTargets ||
        desc.colorTargetCount > mCapabilities.maxColorAttachments)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::CreatePipeline, 0, desc.vertexBufferCount,
           desc.colorTargetCount, "invalid pipeline layout");
      return PipelineHandle();
    }
    for (std::uint32_t slot = 0; slot < desc.vertexBufferCount; ++slot)
      if (desc.vertexBuffers[slot].stride == 0 ||
          desc.vertexBuffers[slot].attributeCount >
              VertexBufferLayout::MaxAttributes)
      {
        push(::gpu::GPUErrorCode::InvalidArgument,
             ::gpu::GPUOperation::CreatePipeline, 0, slot,
             desc.vertexBuffers[slot].attributeCount,
             "invalid vertex buffer layout");
        return PipelineHandle();
      }
    const PipelineHandle handle = create<PipelineHandle>(
        mPipelines, ::gpu::GPUOperation::CreatePipeline);
    if (handle.valid())
      mPipelines.find(handle)->isCompute = hasCompute;
    return handle;
  }

  QueryHandle NullDevice::createQuery(QueryType type)
  {
    if ((type == QueryType::Timestamp && !mCapabilities.timestampQueries) ||
        (type == QueryType::Occlusion && !mCapabilities.occlusionQueries))
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature,
           ::gpu::GPUOperation::CreateQuery, 0,
           static_cast<std::uint64_t>(type), 0,
           "query type is unavailable on this device");
      return QueryHandle();
    }
    const QueryHandle handle =
        create<QueryHandle>(mQueries, ::gpu::GPUOperation::CreateQuery);
    if (handle.valid())
      mQueries.find(handle)->queryType = type;
    return handle;
  }

  FenceHandle NullDevice::insertFence()
  {
    if (!mAlive || !mCapabilities.asyncReadback)
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature,
           ::gpu::GPUOperation::CreateFence, 0, 0, 0,
           mAlive ? "fences are unavailable on this device"
                  : "device is shut down");
      return FenceHandle();
    }
    return create<FenceHandle>(mFences, ::gpu::GPUOperation::CreateFence);
  }

  bool NullDevice::isFenceSignaled(FenceHandle handle)
  {
    if (!validate(handle, mFences, ::gpu::GPUOperation::CreateFence))
      return false;
    return true;
  }

  void NullDevice::destroy(BufferHandle handle)
  {
    Slot *buffer = mBuffers.find(handle);
    if (buffer)
      std::free(buffer->mappedScratch);
    destroy(handle, mBuffers);
    if (mIndexBuffer == handle)
      mIndexBuffer = BufferHandle();
  }
  void NullDevice::destroy(TextureHandle handle) { destroy(handle, mTextures); }
  void NullDevice::destroy(SamplerHandle handle) { destroy(handle, mSamplers); }
  void NullDevice::destroy(PipelineHandle handle)
  {
    destroy(handle, mPipelines);
    if (mPipeline == handle)
      mPipeline = PipelineHandle();
  }
  void NullDevice::destroy(QueryHandle handle)
  {
    destroy(handle, mQueries);
    if (mActiveOcclusionQuery == handle)
      mActiveOcclusionQuery = QueryHandle();
  }
  void NullDevice::destroy(FenceHandle handle) { destroy(handle, mFences); }

  bool NullDevice::beginRenderPass(const RenderPassDesc &desc)
  {
    if (!mAlive || mInRenderPass ||
        desc.colorCount > RenderPassDesc::MaxColorAttachments ||
        desc.colorCount > mCapabilities.maxColorAttachments ||
        (desc.colorCount == 0 && !desc.hasDepthStencil))
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BeginRenderPass, 0, desc.colorCount, 0,
           "invalid render pass state");
      return false;
    }
    bool usesSurface = false;
    for (std::uint32_t index = 0; index < desc.colorCount; ++index)
      usesSurface = usesSurface || desc.colors[index].surface;
    if (usesSurface &&
        (desc.colorCount != 1 || !desc.colors[0].surface ||
         desc.colors[0].target.texture.valid() || desc.hasDepthStencil))
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BeginRenderPass, 0, desc.colorCount,
           desc.hasDepthStencil,
           "surface attachments cannot be mixed with textures");
      return false;
    }
    for (std::uint32_t index = 0; index < desc.colorCount; ++index)
      if (!desc.colors[index].surface &&
          !validateAttachment(desc.colors[index].target, false,
                              ::gpu::GPUOperation::BeginRenderPass))
        return false;
    if (desc.hasDepthStencil &&
        !validateAttachment(desc.depthStencil.target, true,
                            ::gpu::GPUOperation::BeginRenderPass))
      return false;
    mPassAttachmentCount = 0;
    for (std::uint32_t index = 0; index < desc.colorCount; ++index)
      if (desc.colors[index].target.texture.valid())
        mPassAttachments[mPassAttachmentCount++] =
            desc.colors[index].target.texture;
    if (desc.hasDepthStencil && desc.depthStencil.target.texture.valid())
      mPassAttachments[mPassAttachmentCount++] =
          desc.depthStencil.target.texture;
    mInRenderPass = true;
    mPipeline = PipelineHandle();
    return true;
  }

  void NullDevice::endRenderPass()
  {
    if (!mInRenderPass)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BeginRenderPass, 0, 0, 0,
           "no render pass is active");
      return;
    }
    mInRenderPass = false;
  }

  bool NullDevice::setPipeline(PipelineHandle handle)
  {
    if (!validate(handle, mPipelines, ::gpu::GPUOperation::SetPipeline))
      return false;
    const Slot &pipeline = *mPipelines.find(handle);
    if (pipeline.isCompute && mInRenderPass)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::SetPipeline, handle.value(), 0, 0,
           "compute pipeline cannot be set inside a render pass");
      return false;
    }
    if (!pipeline.isCompute && !mInRenderPass)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::SetPipeline, handle.value(), 0, 0,
           "pipeline must be set inside a render pass");
      return false;
    }
    if (mPipeline != handle)
      mIndexBuffer = BufferHandle();
    mPipeline = handle;
    return true;
  }

  bool NullDevice::setViewport(const Viewport &viewport)
  {
    if (!mInRenderPass || viewport.width <= 0.0f || viewport.height <= 0.0f ||
        viewport.minDepth < 0.0f || viewport.maxDepth > 1.0f ||
        viewport.minDepth > viewport.maxDepth)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, 0, 0, 0, "invalid viewport");
      return false;
    }
    return true;
  }

  bool NullDevice::setScissor(const Rect &rect)
  {
    if (!mInRenderPass || rect.width == 0 || rect.height == 0)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, 0, rect.width, rect.height,
           "invalid scissor rectangle");
      return false;
    }
    return true;
  }

  bool NullDevice::setStencilReference(std::uint32_t)
  {
    if (!mInRenderPass || !mPipeline.valid())
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, mPipeline.value(), 0, 0,
           "stencil reference requires an active pipeline");
      return false;
    }
    return true;
  }

  bool NullDevice::bindVertexBuffer(std::uint32_t slot, BufferHandle handle,
                                    std::uint64_t offset)
  {
    if (!mInRenderPass || slot >= PipelineDesc::MaxVertexBuffers)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, handle.value(), slot,
           mInRenderPass, "vertex buffer binding requires a render pass slot");
      return false;
    }
    if (!validate(handle, mBuffers, ::gpu::GPUOperation::BindResource))
      return false;
    const Slot &buffer = *mBuffers.find(handle);
    if (!(buffer.usage & BufferUsageVertex) || offset >= buffer.size)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, handle.value(), offset, buffer.size,
           "invalid vertex buffer binding");
      return false;
    }
    return true;
  }

  bool NullDevice::bindIndexBuffer(BufferHandle handle, IndexFormat format,
                                   std::uint64_t offset)
  {
    if (!mInRenderPass)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, handle.value(), 0, 0,
           "index buffer binding requires an active render pass");
      return false;
    }
    if (!validate(handle, mBuffers, ::gpu::GPUOperation::BindResource))
      return false;
    const Slot &buffer = *mBuffers.find(handle);
    if (!(buffer.usage & BufferUsageIndex) || offset >= buffer.size)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, handle.value(), offset, buffer.size,
           "invalid index buffer binding");
      return false;
    }
    mIndexBuffer = handle;
    mIndexFormat = format;
    return true;
  }

  bool NullDevice::bindUniformBuffer(std::uint32_t slot, BufferHandle handle,
                                     std::uint64_t offset, std::uint64_t size)
  {
    if (!mInRenderPass || slot >= mCapabilities.maxUniformBufferBindings)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, handle.value(), slot,
           mCapabilities.maxUniformBufferBindings,
           "uniform buffer binding requires a render pass slot");
      return false;
    }
    if (!validate(handle, mBuffers, ::gpu::GPUOperation::BindResource))
      return false;
    const Slot &buffer = *mBuffers.find(handle);
    if (!(buffer.usage & BufferUsageUniform) || size == 0 ||
        (offset % mCapabilities.uniformBufferOffsetAlignment) != 0 ||
        offset > buffer.size || size > buffer.size - offset)
    {
      push(::gpu::GPUErrorCode::OutOfBounds,
           ::gpu::GPUOperation::BindResource, handle.value(), offset, size,
           "invalid uniform buffer range");
      return false;
    }
    return true;
  }

  std::int32_t NullDevice::uniformBlockSlot(PipelineHandle handle, const char *name)
  {
    if (!name || !mPipelines.find(handle))
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, handle.value(), 0, 0,
           "uniform block lookup needs a valid pipeline");
    }
    return -1;
  }

  std::int32_t NullDevice::textureSlot(PipelineHandle handle, const char *name)
  {
    if (!name || !mPipelines.find(handle))
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, handle.value(), 0, 0,
           "texture slot lookup needs a valid pipeline");
    }
    return -1;
  }

  bool NullDevice::bindTexture(std::uint32_t slot, TextureHandle texture,
                               SamplerHandle sampler)
  {
    if (!mInRenderPass || slot >= mCapabilities.maxTextureBindings)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, texture.value(), slot,
           mCapabilities.maxTextureBindings,
           "texture binding requires a render pass slot");
      return false;
    }
    if (!validate(texture, mTextures, ::gpu::GPUOperation::BindResource) ||
        !validate(sampler, mSamplers, ::gpu::GPUOperation::BindResource))
      return false;
    for (std::uint32_t index = 0; index < mPassAttachmentCount; ++index)
    {
      if (mPassAttachments[index] != texture)
        continue;
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, texture.value(), slot, 0,
           "texture is an attachment of the current render pass (feedback loop)");
      return false;
    }
    return true;
  }

  std::int32_t NullDevice::registerBindlessTexture(TextureHandle texture,
                                                   SamplerHandle sampler)
  {
    (void)texture;
    (void)sampler;
    push(::gpu::GPUErrorCode::UnsupportedFeature, ::gpu::GPUOperation::BindResource,
         0, 0, 0, "bindless textures are unavailable on the null backend");
    return -1;
  }

  void NullDevice::unregisterBindlessTexture(std::int32_t index) { (void)index; }

  bool NullDevice::bindStorageBuffer(std::uint32_t slot, BufferHandle handle,
                                     std::uint64_t offset, std::uint64_t size)
  {
    if (!validate(handle, mBuffers, ::gpu::GPUOperation::BindResource))
      return false;
    const Slot &buffer = *mBuffers.find(handle);
    if (!(buffer.usage & BufferUsageStorage) || size == 0 ||
        slot >= mCapabilities.maxStorageBufferBindings ||
        (offset % mCapabilities.storageBufferOffsetAlignment) != 0 ||
        offset > buffer.size || size > buffer.size - offset)
    {
      push(::gpu::GPUErrorCode::OutOfBounds, ::gpu::GPUOperation::BindResource,
           handle.value(), offset, size, "invalid storage buffer range");
      return false;
    }
    return true;
  }

  bool NullDevice::bindStorageTexture(std::uint32_t slot, TextureHandle texture,
                                      std::uint32_t mipLevel)
  {
    if (!validate(texture, mTextures, ::gpu::GPUOperation::BindResource))
      return false;
    const Slot &textureSlot = *mTextures.find(texture);
    if (!(textureSlot.usage & TextureUsageStorage) ||
        mipLevel >= textureSlot.mipCount || slot >= 8)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::BindResource, texture.value(), slot, mipLevel,
           "invalid storage texture binding");
      return false;
    }
    return true;
  }

  bool NullDevice::updateBuffer(BufferHandle handle, std::uint64_t offset,
                                DataView data)
  {
    if (mInRenderPass || !data.data || data.size == 0 ||
        !validate(handle, mBuffers, ::gpu::GPUOperation::UpdateBuffer))
      return false;
    const Slot &buffer = *mBuffers.find(handle);
    if (offset > buffer.size || data.size > buffer.size - offset)
    {
      push(::gpu::GPUErrorCode::OutOfBounds,
           ::gpu::GPUOperation::UpdateBuffer, handle.value(), offset, data.size,
           "buffer update is out of bounds");
      return false;
    }
    return true;
  }

  bool NullDevice::updateTexture(TextureHandle handle,
                                 const TextureRegion &region, DataView data,
                                 const TextureDataLayout &layout)
  {
    if (!validate(handle, mTextures, ::gpu::GPUOperation::UpdateTexture))
      return false;
    const Slot &texture = *mTextures.find(handle);
    if (mInRenderPass || !(texture.usage & TextureUsageCopyDestination) ||
        texture.sampleCount != 1 || !data.data || data.size == 0 ||
        region.width == 0 || region.height == 0 ||
        region.depthOrLayers == 0 || region.mipLevel >= texture.mipCount)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::UpdateTexture, handle.value(), region.mipLevel,
           data.size, "invalid texture update state or descriptor");
      return false;
    }
    const std::uint32_t mipWidth = mipDimension(texture.width, region.mipLevel);
    const std::uint32_t mipHeight =
        mipDimension(texture.height, region.mipLevel);
    std::uint32_t mipDepth = 1;
    if (texture.dimension == TextureDimension::TextureCube)
      mipDepth = 6;
    else if (texture.dimension == TextureDimension::Texture2DArray)
      mipDepth = texture.depthOrLayers;
    else if (texture.dimension == TextureDimension::Texture3D)
      mipDepth = mipDimension(texture.depthOrLayers, region.mipLevel);
    if (region.x > mipWidth || region.width > mipWidth - region.x ||
        region.y > mipHeight || region.height > mipHeight - region.y ||
        region.z > mipDepth || region.depthOrLayers > mipDepth - region.z ||
        (texture.dimension == TextureDimension::Texture2D &&
         (region.z != 0 || region.depthOrLayers != 1)))
    {
      push(::gpu::GPUErrorCode::OutOfBounds,
           ::gpu::GPUOperation::UpdateTexture, handle.value(), region.width,
           region.height, "texture update region is out of bounds");
      return false;
    }
    const std::uint32_t blockBytes = compressedBlockBytes(texture.format);
    const bool compressed = blockBytes != 0;
    const std::uint64_t copiedRows =
        compressed ? (static_cast<std::uint64_t>(region.height) + 3) / 4
                   : region.height;
    const std::uint64_t tightRowBytes =
        compressed
            ? ((static_cast<std::uint64_t>(region.width) + 3) / 4) * blockBytes
            : static_cast<std::uint64_t>(region.width) *
                  uncompressedBytesPerTexel(texture.format);
    const std::uint64_t bytesPerRow =
        layout.bytesPerRow == 0 ? tightRowBytes : layout.bytesPerRow;
    const std::uint64_t rowsPerImage =
        layout.rowsPerImage == 0 ? copiedRows : layout.rowsPerImage;
    const bool invalidBlockRegion =
        compressed &&
        ((region.x & 3u) != 0 || (region.y & 3u) != 0 ||
         ((region.width & 3u) != 0 && region.x + region.width != mipWidth) ||
         ((region.height & 3u) != 0 && region.y + region.height != mipHeight));
    std::uint64_t required = 0;
    if (invalidBlockRegion || bytesPerRow < tightRowBytes ||
        rowsPerImage < copiedRows ||
        (!compressed &&
         bytesPerRow % uncompressedBytesPerTexel(texture.format) != 0) ||
        !requiredUploadSize(layout.offset, bytesPerRow, rowsPerImage,
                            copiedRows, tightRowBytes, region.depthOrLayers,
                            required) ||
        required > data.size)
    {
      push(::gpu::GPUErrorCode::OutOfBounds,
           ::gpu::GPUOperation::UpdateTexture, handle.value(), required,
           data.size, "texture upload layout is invalid or too small");
      return false;
    }
    return true;
  }

  bool NullDevice::reflectPipeline(PipelineHandle handle,
                                   PipelineReflection &reflection)
  {
    reflection = PipelineReflection();
    if (!validate(handle, mPipelines, ::gpu::GPUOperation::ReflectPipeline))
      return false;
    // O backend Null nao compila shaders, por isso nao ha recursos ativos
    // para enumerar. Devolve uma reflection vazia em vez de falhar, para que
    // o codigo do chamador possa correr sem GPU.
    return true;
  }

  bool NullDevice::generateMipmaps(TextureHandle handle)
  {
    if (mInRenderPass ||
        !validate(handle, mTextures, ::gpu::GPUOperation::GenerateMipmaps))
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::GenerateMipmaps, handle.value(), 0, 0,
           "mipmap generation requires a valid texture outside a render pass");
      return false;
    }
    const Slot &texture = *mTextures.find(handle);
    if (texture.mipCount <= 1)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::GenerateMipmaps, handle.value(),
           texture.mipCount, 0,
           "texture was created with a single mip level");
      return false;
    }
    if (texture.sampleCount != 1)
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature,
           ::gpu::GPUOperation::GenerateMipmaps, handle.value(),
           texture.sampleCount, 0,
           "multisample textures cannot generate mipmaps");
      return false;
    }
    return true;
  }

  bool NullDevice::copyTexture(TextureHandle destination,
                               const TextureOrigin &destinationOrigin,
                               TextureHandle source,
                               const TextureRegion &sourceRegion)
  {
    if (mInRenderPass ||
        !validate(destination, mTextures, ::gpu::GPUOperation::Copy) ||
        !validate(source, mTextures, ::gpu::GPUOperation::Copy))
      return false;
    const Slot &destinationTexture = *mTextures.find(destination);
    const Slot &sourceTexture = *mTextures.find(source);
    if (sourceRegion.width == 0 || sourceRegion.height == 0 ||
        sourceRegion.depthOrLayers == 0 ||
        sourceRegion.mipLevel >= sourceTexture.mipCount ||
        destinationOrigin.mipLevel >= destinationTexture.mipCount ||
        destinationTexture.format != sourceTexture.format ||
        !(sourceTexture.usage & TextureUsageCopySource) ||
        !(destinationTexture.usage & TextureUsageCopyDestination))
    {
      push(::gpu::GPUErrorCode::InvalidArgument, ::gpu::GPUOperation::Copy,
           destination.value(), sourceRegion.mipLevel,
           destinationOrigin.mipLevel,
           "invalid texture copy state or descriptor");
      return false;
    }
    const std::uint32_t sourceMipWidth =
        mipDimension(sourceTexture.width, sourceRegion.mipLevel);
    const std::uint32_t sourceMipHeight =
        mipDimension(sourceTexture.height, sourceRegion.mipLevel);
    const std::uint32_t destinationMipWidth =
        mipDimension(destinationTexture.width, destinationOrigin.mipLevel);
    const std::uint32_t destinationMipHeight =
        mipDimension(destinationTexture.height, destinationOrigin.mipLevel);
    std::uint32_t sourceMipDepth = 1;
    if (sourceTexture.dimension == TextureDimension::TextureCube)
      sourceMipDepth = 6;
    else if (sourceTexture.dimension == TextureDimension::Texture2DArray)
      sourceMipDepth = sourceTexture.depthOrLayers;
    else if (sourceTexture.dimension == TextureDimension::Texture3D)
      sourceMipDepth =
          mipDimension(sourceTexture.depthOrLayers, sourceRegion.mipLevel);
    std::uint32_t destinationMipDepth = 1;
    if (destinationTexture.dimension == TextureDimension::TextureCube)
      destinationMipDepth = 6;
    else if (destinationTexture.dimension == TextureDimension::Texture2DArray)
      destinationMipDepth = destinationTexture.depthOrLayers;
    else if (destinationTexture.dimension == TextureDimension::Texture3D)
      destinationMipDepth = mipDimension(destinationTexture.depthOrLayers,
                                         destinationOrigin.mipLevel);
    if (sourceRegion.x > sourceMipWidth ||
        sourceRegion.width > sourceMipWidth - sourceRegion.x ||
        sourceRegion.y > sourceMipHeight ||
        sourceRegion.height > sourceMipHeight - sourceRegion.y ||
        sourceRegion.z > sourceMipDepth ||
        sourceRegion.depthOrLayers > sourceMipDepth - sourceRegion.z ||
        destinationOrigin.x > destinationMipWidth ||
        sourceRegion.width > destinationMipWidth - destinationOrigin.x ||
        destinationOrigin.y > destinationMipHeight ||
        sourceRegion.height > destinationMipHeight - destinationOrigin.y ||
        destinationOrigin.z > destinationMipDepth ||
        sourceRegion.depthOrLayers > destinationMipDepth - destinationOrigin.z ||
        (sourceTexture.dimension == TextureDimension::Texture2D &&
         (sourceRegion.z != 0 || sourceRegion.depthOrLayers != 1)) ||
        (destinationTexture.dimension == TextureDimension::Texture2D &&
         destinationOrigin.z != 0))
    {
      push(::gpu::GPUErrorCode::OutOfBounds, ::gpu::GPUOperation::Copy,
           destination.value(), sourceRegion.width, sourceRegion.height,
           "texture copy region is out of bounds");
      return false;
    }
    return true;
  }

  bool NullDevice::readTexture(TextureHandle handle, const TextureRegion &region,
                               MutableDataView data,
                               const TextureDataLayout &layout)
  {
    if (!validate(handle, mTextures, ::gpu::GPUOperation::ReadTexture))
      return false;
    const Slot &texture = *mTextures.find(handle);
    if (mInRenderPass || !(texture.usage & TextureUsageCopySource) ||
        !data.data || data.size == 0 || region.width == 0 ||
        region.height == 0 || region.depthOrLayers == 0 ||
        region.mipLevel >= texture.mipCount)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::ReadTexture, handle.value(), region.mipLevel,
           data.size, "invalid texture read state or descriptor");
      return false;
    }
    const std::uint32_t mipWidth = mipDimension(texture.width, region.mipLevel);
    const std::uint32_t mipHeight =
        mipDimension(texture.height, region.mipLevel);
    std::uint32_t mipDepth = 1;
    if (texture.dimension == TextureDimension::TextureCube)
      mipDepth = 6;
    else if (texture.dimension == TextureDimension::Texture2DArray)
      mipDepth = texture.depthOrLayers;
    else if (texture.dimension == TextureDimension::Texture3D)
      mipDepth = mipDimension(texture.depthOrLayers, region.mipLevel);
    if (region.x > mipWidth || region.width > mipWidth - region.x ||
        region.y > mipHeight || region.height > mipHeight - region.y ||
        region.z > mipDepth || region.depthOrLayers > mipDepth - region.z ||
        (texture.dimension == TextureDimension::Texture2D &&
         (region.z != 0 || region.depthOrLayers != 1)))
    {
      push(::gpu::GPUErrorCode::OutOfBounds, ::gpu::GPUOperation::ReadTexture,
           handle.value(), region.width, region.height,
           "texture read region is out of bounds");
      return false;
    }
    const std::uint32_t blockBytes = compressedBlockBytes(texture.format);
    const bool compressed = blockBytes != 0;
    const std::uint64_t copiedRows =
        compressed ? (static_cast<std::uint64_t>(region.height) + 3) / 4
                   : region.height;
    const std::uint64_t tightRowBytes =
        compressed
            ? ((static_cast<std::uint64_t>(region.width) + 3) / 4) * blockBytes
            : static_cast<std::uint64_t>(region.width) *
                  uncompressedBytesPerTexel(texture.format);
    const std::uint64_t bytesPerRow =
        layout.bytesPerRow == 0 ? tightRowBytes : layout.bytesPerRow;
    const std::uint64_t rowsPerImage =
        layout.rowsPerImage == 0 ? copiedRows : layout.rowsPerImage;
    std::uint64_t required = 0;
    if (bytesPerRow < tightRowBytes || rowsPerImage < copiedRows ||
        (!compressed &&
         bytesPerRow % uncompressedBytesPerTexel(texture.format) != 0) ||
        !requiredUploadSize(layout.offset, bytesPerRow, rowsPerImage,
                            copiedRows, tightRowBytes, region.depthOrLayers,
                            required) ||
        required > data.size)
    {
      push(::gpu::GPUErrorCode::OutOfBounds, ::gpu::GPUOperation::ReadTexture,
           handle.value(), required, data.size,
           "texture read layout is invalid or too small");
      return false;
    }
    return true;
  }

  bool NullDevice::copyBuffer(BufferHandle destination,
                              std::uint64_t destinationOffset,
                              BufferHandle source, std::uint64_t sourceOffset,
                              std::uint64_t size)
  {
    if (mInRenderPass || size == 0 ||
        !validate(destination, mBuffers, ::gpu::GPUOperation::Copy) ||
        !validate(source, mBuffers, ::gpu::GPUOperation::Copy))
      return false;
    const Slot &destinationBuffer = *mBuffers.find(destination);
    const Slot &sourceBuffer = *mBuffers.find(source);
    if (destinationOffset > destinationBuffer.size ||
        size > destinationBuffer.size - destinationOffset ||
        sourceOffset > sourceBuffer.size ||
        size > sourceBuffer.size - sourceOffset)
    {
      push(::gpu::GPUErrorCode::OutOfBounds, ::gpu::GPUOperation::Copy,
           destination.value(), destinationOffset, size,
           "buffer copy is out of bounds");
      return false;
    }
    return true;
  }

  void *NullDevice::mapBuffer(BufferHandle handle, std::uint64_t offset,
                              std::uint64_t size, MapMode mode)
  {
    if (!validate(handle, mBuffers, ::gpu::GPUOperation::MapBuffer))
      return nullptr;
    Slot *buffer = mBuffers.find(handle);
    const std::uint32_t requiredUsage =
        mode == MapMode::Read ? BufferUsageReadback : BufferUsageStaging;
    if (buffer->mappedScratch || !(buffer->usage & requiredUsage) ||
        size == 0 || offset > buffer->size || size > buffer->size - offset)
    {
      push(::gpu::GPUErrorCode::InvalidArgument, ::gpu::GPUOperation::MapBuffer,
           handle.value(), offset, size, "invalid buffer map range or usage");
      return nullptr;
    }
    buffer->mappedScratch = std::calloc(1, static_cast<std::size_t>(size));
    if (!buffer->mappedScratch)
    {
      push(::gpu::GPUErrorCode::OutOfMemory, ::gpu::GPUOperation::MapBuffer,
           handle.value(), offset, size, "buffer map allocation failed");
      return nullptr;
    }
    return buffer->mappedScratch;
  }

  bool NullDevice::unmapBuffer(BufferHandle handle)
  {
    if (!validate(handle, mBuffers, ::gpu::GPUOperation::MapBuffer))
      return false;
    Slot *buffer = mBuffers.find(handle);
    if (!buffer->mappedScratch)
    {
      push(::gpu::GPUErrorCode::InvalidArgument, ::gpu::GPUOperation::MapBuffer,
           handle.value(), 0, 0, "buffer is not mapped");
      return false;
    }
    std::free(buffer->mappedScratch);
    buffer->mappedScratch = nullptr;
    return true;
  }

  bool NullDevice::draw(std::uint32_t vertexCount, std::uint32_t instanceCount,
                        std::uint32_t, std::uint32_t firstInstance)
  {
    if (firstInstance != 0 && !mCapabilities.baseInstance)
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature, ::gpu::GPUOperation::Draw,
           mPipeline.value(), firstInstance, 0,
           "a non-zero first instance is unavailable on this device");
      return false;
    }
    if (!mInRenderPass || !mPipeline.valid() || vertexCount == 0 ||
        instanceCount == 0)
    {
      push(::gpu::GPUErrorCode::InvalidArgument, ::gpu::GPUOperation::Draw,
           mPipeline.value(), vertexCount, instanceCount, "invalid draw state");
      return false;
    }
    return true;
  }

  bool NullDevice::drawIndexed(std::uint32_t indexCount,
                               std::uint32_t instanceCount, std::uint32_t,
                               std::int32_t, std::uint32_t firstInstance)
  {
    if (firstInstance != 0 && !mCapabilities.baseInstance)
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature, ::gpu::GPUOperation::Draw,
           mIndexBuffer.value(), firstInstance, 0,
           "a non-zero first instance is unavailable on this device");
      return false;
    }
    if (!mInRenderPass || !mPipeline.valid() || !mBuffers.find(mIndexBuffer) ||
        indexCount == 0 || instanceCount == 0)
    {
      push(::gpu::GPUErrorCode::InvalidArgument, ::gpu::GPUOperation::Draw,
           mIndexBuffer.value(), indexCount, instanceCount,
           "invalid indexed draw state");
      return false;
    }
    return true;
  }

  bool NullDevice::memoryBarrier(std::uint32_t barriers)
  {
    if (!mCapabilities.memoryBarriers || barriers == 0)
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature, ::gpu::GPUOperation::Dispatch,
           0, barriers, 0, "memory barriers are unavailable on this device");
      return false;
    }
    return true;
  }

  bool NullDevice::dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
                            std::uint32_t groupCountZ)
  {
    if (mInRenderPass || !mPipeline.valid() || groupCountX == 0 ||
        groupCountY == 0 || groupCountZ == 0)
    {
      push(::gpu::GPUErrorCode::InvalidArgument, ::gpu::GPUOperation::Dispatch,
           mPipeline.value(), groupCountX, groupCountY,
           "invalid dispatch state");
      return false;
    }
    if (!validate(mPipeline, mPipelines, ::gpu::GPUOperation::Dispatch))
      return false;
    const Slot &pipeline = *mPipelines.find(mPipeline);
    if (!pipeline.isCompute)
    {
      push(::gpu::GPUErrorCode::InvalidArgument, ::gpu::GPUOperation::Dispatch,
           mPipeline.value(), 0, 0, "bound pipeline is not a compute pipeline");
      return false;
    }
    return true;
  }

  bool NullDevice::drawIndirect(BufferHandle buffer, std::uint64_t offset)
  {
    if (!validate(buffer, mBuffers, ::gpu::GPUOperation::DrawIndirect))
      return false;
    const Slot &indirectBuffer = *mBuffers.find(buffer);
    if (!mInRenderPass || !mPipeline.valid() ||
        !(indirectBuffer.usage & BufferUsageIndirect) || (offset % 4) != 0 ||
        offset > indirectBuffer.size ||
        indirectBuffer.size - offset < sizeof(DrawIndirectArgs))
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::DrawIndirect, buffer.value(), offset, 0,
           "invalid indirect draw state");
      return false;
    }
    if (!validate(mPipeline, mPipelines, ::gpu::GPUOperation::DrawIndirect))
      return false;
    const Slot &pipeline = *mPipelines.find(mPipeline);
    if (pipeline.isCompute)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::DrawIndirect, mPipeline.value(), 0, 0,
           "bound pipeline is not a graphics pipeline");
      return false;
    }
    return true;
  }

  bool NullDevice::drawIndexedIndirect(BufferHandle buffer, std::uint64_t offset)
  {
    if (!validate(buffer, mBuffers, ::gpu::GPUOperation::DrawIndirect))
      return false;
    const Slot &indirectBuffer = *mBuffers.find(buffer);
    if (!mInRenderPass || !mPipeline.valid() || !mBuffers.find(mIndexBuffer) ||
        !(indirectBuffer.usage & BufferUsageIndirect) || (offset % 4) != 0 ||
        offset > indirectBuffer.size ||
        indirectBuffer.size - offset < sizeof(DrawIndexedIndirectArgs))
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::DrawIndirect, buffer.value(), offset, 0,
           "invalid indexed indirect draw state");
      return false;
    }
    if (!validate(mPipeline, mPipelines, ::gpu::GPUOperation::DrawIndirect))
      return false;
    const Slot &pipeline = *mPipelines.find(mPipeline);
    if (pipeline.isCompute)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::DrawIndirect, mPipeline.value(), 0, 0,
           "bound pipeline is not a graphics pipeline");
      return false;
    }
    return true;
  }

  bool NullDevice::drawIndirectCount(BufferHandle buffer, std::uint64_t offset,
                                     BufferHandle countBuffer,
                                     std::uint64_t countOffset,
                                     std::uint32_t maxDrawCount,
                                     std::uint32_t stride)
  {
    if (!mCapabilities.indirectCount)
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature,
           ::gpu::GPUOperation::DrawIndirect, buffer.value(), 0, 0,
           "indirect draw count is unavailable on this device");
      return false;
    }
    if (!validate(buffer, mBuffers, ::gpu::GPUOperation::DrawIndirect) ||
        !validate(countBuffer, mBuffers, ::gpu::GPUOperation::DrawIndirect))
      return false;
    const Slot &indirectBuffer = *mBuffers.find(buffer);
    const Slot &countBufferSlot = *mBuffers.find(countBuffer);
    const std::uint64_t maxU64 =
        (std::numeric_limits<std::uint64_t>::max)();
    const std::uint64_t lastEntryOffset =
        maxDrawCount == 0
            ? 0
            : static_cast<std::uint64_t>(maxDrawCount - 1) * stride;
    const bool spanOverflows =
        offset > maxU64 - sizeof(DrawIndirectArgs) ||
        lastEntryOffset > maxU64 - offset - sizeof(DrawIndirectArgs);
    if (!mInRenderPass || !mPipeline.valid() ||
        !(indirectBuffer.usage & BufferUsageIndirect) ||
        !(countBufferSlot.usage & BufferUsageIndirect) || maxDrawCount == 0 ||
        stride < sizeof(DrawIndirectArgs) || (offset % 4) != 0 ||
        (countOffset % 4) != 0 || spanOverflows ||
        offset + lastEntryOffset + sizeof(DrawIndirectArgs) >
            indirectBuffer.size ||
        countOffset > countBufferSlot.size ||
        countBufferSlot.size - countOffset < sizeof(std::uint32_t))
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::DrawIndirect, buffer.value(), offset,
           maxDrawCount, "invalid indirect draw count state");
      return false;
    }
    if (!validate(mPipeline, mPipelines, ::gpu::GPUOperation::DrawIndirect))
      return false;
    const Slot &pipeline = *mPipelines.find(mPipeline);
    if (pipeline.isCompute)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::DrawIndirect, mPipeline.value(), 0, 0,
           "bound pipeline is not a graphics pipeline");
      return false;
    }
    return true;
  }

  bool NullDevice::drawIndexedIndirectCount(BufferHandle buffer,
                                            std::uint64_t offset,
                                            BufferHandle countBuffer,
                                            std::uint64_t countOffset,
                                            std::uint32_t maxDrawCount,
                                            std::uint32_t stride)
  {
    if (!mCapabilities.indirectCount)
    {
      push(::gpu::GPUErrorCode::UnsupportedFeature,
           ::gpu::GPUOperation::DrawIndirect, buffer.value(), 0, 0,
           "indirect draw count is unavailable on this device");
      return false;
    }
    if (!validate(buffer, mBuffers, ::gpu::GPUOperation::DrawIndirect) ||
        !validate(countBuffer, mBuffers, ::gpu::GPUOperation::DrawIndirect))
      return false;
    const Slot &indirectBuffer = *mBuffers.find(buffer);
    const Slot &countBufferSlot = *mBuffers.find(countBuffer);
    const std::uint64_t maxU64 =
        (std::numeric_limits<std::uint64_t>::max)();
    const std::uint64_t lastEntryOffset =
        maxDrawCount == 0
            ? 0
            : static_cast<std::uint64_t>(maxDrawCount - 1) * stride;
    const bool spanOverflows =
        offset > maxU64 - sizeof(DrawIndexedIndirectArgs) ||
        lastEntryOffset > maxU64 - offset - sizeof(DrawIndexedIndirectArgs);
    if (!mInRenderPass || !mPipeline.valid() || !mBuffers.find(mIndexBuffer) ||
        !(indirectBuffer.usage & BufferUsageIndirect) ||
        !(countBufferSlot.usage & BufferUsageIndirect) || maxDrawCount == 0 ||
        stride < sizeof(DrawIndexedIndirectArgs) || (offset % 4) != 0 ||
        (countOffset % 4) != 0 || spanOverflows ||
        offset + lastEntryOffset + sizeof(DrawIndexedIndirectArgs) >
            indirectBuffer.size ||
        countOffset > countBufferSlot.size ||
        countBufferSlot.size - countOffset < sizeof(std::uint32_t))
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::DrawIndirect, buffer.value(), offset,
           maxDrawCount, "invalid indexed indirect draw count state");
      return false;
    }
    if (!validate(mPipeline, mPipelines, ::gpu::GPUOperation::DrawIndirect))
      return false;
    const Slot &pipeline = *mPipelines.find(mPipeline);
    if (pipeline.isCompute)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::DrawIndirect, mPipeline.value(), 0, 0,
           "bound pipeline is not a graphics pipeline");
      return false;
    }
    return true;
  }

  bool NullDevice::beginQuery(QueryHandle handle)
  {
    if (!validate(handle, mQueries, ::gpu::GPUOperation::CreateQuery))
      return false;
    Slot *query = mQueries.find(handle);
    if (!mInRenderPass || query->queryType != QueryType::Occlusion ||
        query->queryActive || mActiveOcclusionQuery.valid())
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::CreateQuery, handle.value(), 0, 0,
           "invalid occlusion query begin state");
      return false;
    }
    query->queryActive = true;
    query->queryWritten = false;
    mActiveOcclusionQuery = handle;
    return true;
  }

  void NullDevice::endQuery(QueryHandle handle)
  {
    if (!validate(handle, mQueries, ::gpu::GPUOperation::CreateQuery))
      return;
    Slot *query = mQueries.find(handle);
    if (!query->queryActive || mActiveOcclusionQuery != handle)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::CreateQuery, handle.value(), 0, 0,
           "invalid occlusion query end state");
      return;
    }
    query->queryActive = false;
    query->queryWritten = true;
    mActiveOcclusionQuery = QueryHandle();
  }

  bool NullDevice::writeTimestamp(QueryHandle handle)
  {
    if (!validate(handle, mQueries, ::gpu::GPUOperation::CreateQuery))
      return false;
    Slot *query = mQueries.find(handle);
    if (query->queryType != QueryType::Timestamp || query->queryActive)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::CreateQuery, handle.value(), 0, 0,
           "invalid timestamp query state");
      return false;
    }
    query->queryWritten = true;
    return true;
  }

  bool NullDevice::isQueryResultAvailable(QueryHandle handle)
  {
    if (!validate(handle, mQueries, ::gpu::GPUOperation::CreateQuery))
      return false;
    Slot *query = mQueries.find(handle);
    if (!query->queryWritten || query->queryActive)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::CreateQuery, handle.value(), 0, 0,
           "query has no pending result");
      return false;
    }
    return true;
  }

  bool NullDevice::getQueryResult(QueryHandle handle, std::uint64_t &result)
  {
    if (!validate(handle, mQueries, ::gpu::GPUOperation::CreateQuery))
      return false;
    Slot *query = mQueries.find(handle);
    if (!query->queryWritten || query->queryActive)
    {
      push(::gpu::GPUErrorCode::InvalidArgument,
           ::gpu::GPUOperation::CreateQuery, handle.value(), 0, 0,
           "query has no pending result");
      return false;
    }
    result = 0;
    query->queryWritten = false;
    return true;
  }

  SurfaceState NullDevice::surfaceState() const { return mSurfaceState; }

  bool NullDevice::resizeSurface(std::uint32_t width, std::uint32_t height)
  {
    if (!mAlive || width == 0 || height == 0)
    {
      push(::gpu::GPUErrorCode::InvalidArgument, ::gpu::GPUOperation::Present, 0,
           width, height, "invalid surface size");
      return false;
    }
    mSurfaceWidth = width;
    mSurfaceHeight = height;
    mSurfaceState = SurfaceState::Ready;
    return true;
  }

  void NullDevice::suspendSurface()
  {
    if (!mAlive)
    {
      push(::gpu::GPUErrorCode::DeviceLost, ::gpu::GPUOperation::Present, 0, 0,
           0, "device is shut down");
      return;
    }
    mSurfaceState = SurfaceState::Suspended;
  }

  bool NullDevice::resumeSurface()
  {
    if (!mAlive || mSurfaceState == SurfaceState::Lost)
    {
      push(mAlive ? ::gpu::GPUErrorCode::SurfaceLost
                  : ::gpu::GPUErrorCode::DeviceLost,
           ::gpu::GPUOperation::Present, 0,
           static_cast<std::uint64_t>(mSurfaceState), 0,
           mAlive ? "surface is lost and cannot be resumed"
                  : "device is shut down");
      return false;
    }
    mSurfaceState = SurfaceState::Ready;
    return true;
  }

  bool NullDevice::present()
  {
    if (!mAlive || mInRenderPass || mSurfaceState != SurfaceState::Ready)
    {
      push(::gpu::GPUErrorCode::InvalidArgument, ::gpu::GPUOperation::Present, 0,
           0, 0, "presentation requires an idle device");
      return false;
    }
    return true;
  }

  Device *createNullDevice(const DeviceDesc &desc, GPUError *error)
  {
    if (desc.backend != Backend::Null)
    {
      reportDeviceCreationFailure(error, GPUErrorCode::InvalidArgument,
                                  "descriptor does not select the Null backend");
      return nullptr;
    }
    const GPUCapabilities capabilities = nullCapabilities(desc.profile);
    if (!requirementsMet(desc.requiredCapabilities, capabilities))
    {
      reportDeviceCreationFailure(
          error, GPUErrorCode::UnsupportedFeature,
          "required capabilities are unavailable in the selected profile");
      return nullptr;
    }
    return new NullDevice(desc);
  }

} // namespace gpu
