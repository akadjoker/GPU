#include "VulkanDevice.h"

#include "thirdparty/spirv-reflect/spirv_reflect.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace gpu
{

  namespace
  {

    // Runs `fn` when it goes out of scope unless dismiss() was called first.
    // Used by the create*() functions below to undo whatever Vulkan objects
    // were already created when a later step fails, instead of each
    // function re-listing the same growing "destroy what we have so far"
    // sequence in every early-return branch.
    template <typename F>
    class ScopeGuard
    {
    public:
      explicit ScopeGuard(F fn) : mFn(std::move(fn)) {}
      ScopeGuard(const ScopeGuard &) = delete;
      ScopeGuard &operator=(const ScopeGuard &) = delete;
      ~ScopeGuard()
      {
        if (mActive)
          mFn();
      }
      void dismiss() { mActive = false; }

    private:
      F mFn;
      bool mActive = true;
    };

    template <typename F>
    ScopeGuard<F> onFailure(F fn)
    {
      return ScopeGuard<F>(std::move(fn));
    }

    // Reflects one SPIR-V stage's descriptor bindings into `out`, appending
    // to whatever earlier stages of the same pipeline already contributed
    // (a resource bound in both vertex and fragment, say, is only reported
    // once). `isCompute` selects which descriptor set index maps to which
    // ShaderResourceType, matching the fixed set layout createDescriptors()
    // builds: sets {uniform, texture, storageBuffer, storageImage, bindless}
    // for graphics, {storageBuffer, storageImage} for compute. Set 4
    // (bindless) has no equivalent in this reflection API and is skipped,
    // same as any other unrecognized set/descriptor-type combination.
    void reflectSpirvStage(const ShaderDesc &stage, bool isCompute,
                           PipelineReflection &out)
    {
      if (!stage.source.data || stage.source.size == 0)
        return;
      SpvReflectShaderModule module = {};
      if (spvReflectCreateShaderModule(static_cast<std::size_t>(stage.source.size),
                                       stage.source.data, &module) !=
          SPV_REFLECT_RESULT_SUCCESS)
        return;
      std::uint32_t count = 0;
      spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
      if (count > 0)
      {
        std::vector<SpvReflectDescriptorBinding *> bindings(count);
        spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());
        for (std::uint32_t index = 0; index < count; ++index)
        {
          const SpvReflectDescriptorBinding *binding = bindings[index];
          // Graphics sets: 0=uniform, 1=texture, 2=storageBuffer,
          // 3=storageImage. Compute sets: 0=storageBuffer, 1=storageImage
          // (see createDescriptors()/createComputePipeline()).
          const std::uint32_t storageBufferSet = isCompute ? 0u : 2u;
          const std::uint32_t storageImageSet = isCompute ? 1u : 3u;
          ShaderResourceType type;
          if (!isCompute && binding->set == 0 &&
              binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            type = ShaderResourceType::UniformBuffer;
          else if (!isCompute && binding->set == 1 &&
                  binding->descriptor_type ==
                      SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            type = ShaderResourceType::Sampler;
          else if (binding->set == storageBufferSet &&
                  binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            type = ShaderResourceType::StorageBuffer;
          else if (binding->set == storageImageSet &&
                  binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            type = ShaderResourceType::StorageTexture;
          else
            continue;
          bool alreadyReported = false;
          for (std::uint32_t existing = 0; existing < out.resourceCount; ++existing)
            if (out.resources[existing].type == type &&
                out.resources[existing].slot == binding->binding)
            {
              alreadyReported = true;
              break;
            }
          if (alreadyReported)
            continue;
          if (out.resourceCount >= PipelineReflection::MaxResources)
          {
            out.truncated = true;
            continue;
          }
          ShaderResource &resource = out.resources[out.resourceCount++];
          const char *name = binding->name ? binding->name : "";
          std::size_t length = 0;
          while (name[length] != '\0' && length + 1 < ShaderResource::MaxNameLength)
          {
            resource.name[length] = name[length];
            ++length;
          }
          resource.name[length] = '\0';
          resource.type = type;
          resource.slot = binding->binding;
          resource.elementCount = binding->count > 0 ? binding->count : 1;
          resource.blockSize =
              type == ShaderResourceType::UniformBuffer ? binding->block.size : 0;
        }
      }
      spvReflectDestroyShaderModule(&module);
    }

    bool hasDeviceExtension(VkPhysicalDevice device, const char *name)
    {
      std::uint32_t count = 0;
      if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) !=
          VK_SUCCESS)
        return false;
      Vector<VkExtensionProperties> extensions(count);
      if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
                                               extensions.data()) != VK_SUCCESS)
        return false;
      for (const VkExtensionProperties &extension : extensions)
      {
        if (std::strcmp(extension.extensionName, name) == 0)
          return true;
      }
      return false;
    }

    bool chooseSurfaceFormat(const Vector<VkSurfaceFormatKHR> &formats,
                             VkFormat requested,
                             VkSurfaceFormatKHR &selected)
    {
    for (const VkSurfaceFormatKHR &format : formats)
      {
        if (format.format == requested &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
          selected = format;
          return true;
        }
      }
      selected = formats.front();
      return true;
    }

    VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(
        VkCompositeAlphaFlagsKHR supported)
    {
      const VkCompositeAlphaFlagBitsKHR candidates[] = {
          VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
          VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
      for (VkCompositeAlphaFlagBitsKHR candidate : candidates)
        if (supported & candidate)
          return candidate;
      return static_cast<VkCompositeAlphaFlagBitsKHR>(0);
    }

    VkFilter vulkanFilter(Filter filter)
    {
      return filter == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    }

    VkSamplerMipmapMode vulkanMipmapMode(Filter filter)
    {
      return filter == Filter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                       : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    VkSamplerAddressMode vulkanAddressMode(AddressMode mode)
    {
      switch (mode)
      {
      case AddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
      case AddressMode::MirrorRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      case AddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      case AddressMode::ClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
      }
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

    VkCompareOp vulkanCompareOp(CompareOp op)
    {
      return static_cast<VkCompareOp>(static_cast<std::uint32_t>(op));
    }

    VkFormat vulkanFormat(Format format)
    {
      switch (format)
      {
      case Format::R8: return VK_FORMAT_R8_UNORM;
      case Format::RG8: return VK_FORMAT_R8G8_UNORM;
      case Format::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
      case Format::RGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
      case Format::R16Float: return VK_FORMAT_R16_SFLOAT;
      case Format::RG16Float: return VK_FORMAT_R16G16_SFLOAT;
      case Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
      case Format::R32Float: return VK_FORMAT_R32_SFLOAT;
      case Format::RG32Float: return VK_FORMAT_R32G32_SFLOAT;
      case Format::RGBA32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
      case Format::RGB32Float: return VK_FORMAT_R32G32B32_SFLOAT;
      case Format::R11G11B10Float: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
      case Format::RGB10A2: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
      case Format::R16Uint: return VK_FORMAT_R16_UINT;
      case Format::RG16Uint: return VK_FORMAT_R16G16_UINT;
      case Format::RGBA16Uint: return VK_FORMAT_R16G16B16A16_UINT;
      case Format::R32Uint: return VK_FORMAT_R32_UINT;
      case Format::RG32Uint: return VK_FORMAT_R32G32_UINT;
      case Format::RGBA32Uint: return VK_FORMAT_R32G32B32A32_UINT;
      case Format::Depth16: return VK_FORMAT_D16_UNORM;
      case Format::Depth32Float: return VK_FORMAT_D32_SFLOAT;
      case Format::Depth24Stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
      default: return VK_FORMAT_UNDEFINED;
      }
    }

    bool depthFormat(Format format)
    {
      return format == Format::Depth16 || format == Format::Depth32Float ||
             format == Format::Depth24Stencil8;
    }

    std::uint32_t maximumMipCount(std::uint32_t width, std::uint32_t height)
    {
      std::uint32_t largest = std::max(width, height);
      std::uint32_t count = 1;
      while (largest > 1)
      {
        largest >>= 1;
        ++count;
      }
      return count;
    }

    std::uint32_t mipDimension(std::uint32_t dimension,
                               std::uint32_t mipLevel)
    {
      return std::max(1u, dimension >> mipLevel);
    }

    VkImageLayout finalTextureLayout(std::uint32_t usage)
    {
      return usage & TextureUsageSampled
                 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                 : VK_IMAGE_LAYOUT_GENERAL;
    }

    std::uint32_t bytesPerTexel(Format format)
    {
      switch (format)
      {
      case Format::R8: return 1;
      case Format::RG8:
      case Format::R16Float:
      case Format::R16Uint:
      case Format::Depth16: return 2;
      case Format::RGBA8:
      case Format::RGBA8Srgb:
      case Format::RG16Float:
      case Format::RG16Uint:
      case Format::R32Float:
      case Format::R32Uint:
      case Format::R11G11B10Float:
      case Format::RGB10A2:
      case Format::Depth32Float:
      case Format::Depth24Stencil8: return 4;
      case Format::RGBA16Float:
      case Format::RGBA16Uint:
      case Format::RG32Float:
      case Format::RG32Uint: return 8;
      case Format::RGB32Float: return 12;
      case Format::RGBA32Float:
      case Format::RGBA32Uint: return 16;
      default: return 0;
      }
    }

    bool requiredDataSize(std::uint64_t offset, std::uint64_t bytesPerRow,
                          std::uint64_t rowsPerImage,
                          std::uint64_t copiedRows,
                          std::uint64_t tightRowBytes,
                          std::uint64_t &required)
    {
      const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
      if (copiedRows == 0 || rowsPerImage < copiedRows ||
          bytesPerRow < tightRowBytes || rowsPerImage > maximum / bytesPerRow)
        return false;
      const std::uint64_t imageStride = rowsPerImage * bytesPerRow;
      if (copiedRows - 1 > (maximum - offset) / bytesPerRow)
        return false;
      required = offset + (copiedRows - 1) * bytesPerRow;
      if (tightRowBytes > maximum - required)
        return false;
      required += tightRowBytes;
      return imageStride != 0;
    }

    VkPrimitiveTopology vulkanTopology(Topology topology)
    {
      switch (topology)
      {
      case Topology::Triangles: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      case Topology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
      case Topology::Lines: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
      case Topology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
      case Topology::Points: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
      case Topology::Patches: return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
      }
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

    VkVertexInputRate vulkanVertexInputRate(VertexStepMode mode)
    {
      return mode == VertexStepMode::Instance ? VK_VERTEX_INPUT_RATE_INSTANCE
                                              : VK_VERTEX_INPUT_RATE_VERTEX;
    }

    VkFormat vulkanVertexFormat(VertexFormat format)
    {
      switch (format)
      {
      case VertexFormat::Float32: return VK_FORMAT_R32_SFLOAT;
      case VertexFormat::Float32x2: return VK_FORMAT_R32G32_SFLOAT;
      case VertexFormat::Float32x3: return VK_FORMAT_R32G32B32_SFLOAT;
      case VertexFormat::Float32x4: return VK_FORMAT_R32G32B32A32_SFLOAT;
      case VertexFormat::Uint32: return VK_FORMAT_R32_UINT;
      case VertexFormat::Uint32x2: return VK_FORMAT_R32G32_UINT;
      case VertexFormat::Uint32x3: return VK_FORMAT_R32G32B32_UINT;
      case VertexFormat::Uint32x4: return VK_FORMAT_R32G32B32A32_UINT;
      case VertexFormat::Unorm8x4: return VK_FORMAT_R8G8B8A8_UNORM;
      case VertexFormat::Snorm8x4: return VK_FORMAT_R8G8B8A8_SNORM;
      }
      return VK_FORMAT_UNDEFINED;
    }

    VkCullModeFlags vulkanCullMode(CullMode mode)
    {
      switch (mode)
      {
      case CullMode::None: return VK_CULL_MODE_NONE;
      case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
      case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
      }
      return VK_CULL_MODE_NONE;
    }

    VkFrontFace vulkanFrontFace(FrontFace face)
    {
      return face == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE
                                          : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }

    VkBlendFactor vulkanBlendFactor(BlendFactor factor)
    {
      switch (factor)
      {
      case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
      case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
      case BlendFactor::SourceColor: return VK_BLEND_FACTOR_SRC_COLOR;
      case BlendFactor::OneMinusSourceColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
      case BlendFactor::SourceAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
      case BlendFactor::OneMinusSourceAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      case BlendFactor::DestinationColor: return VK_BLEND_FACTOR_DST_COLOR;
      case BlendFactor::OneMinusDestinationColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
      case BlendFactor::DestinationAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
      case BlendFactor::OneMinusDestinationAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
      }
      return VK_BLEND_FACTOR_ONE;
    }

    VkBlendOp vulkanBlendOperation(BlendOperation operation)
    {
      switch (operation)
      {
      case BlendOperation::Add: return VK_BLEND_OP_ADD;
      case BlendOperation::Subtract: return VK_BLEND_OP_SUBTRACT;
      case BlendOperation::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
      case BlendOperation::Minimum: return VK_BLEND_OP_MIN;
      case BlendOperation::Maximum: return VK_BLEND_OP_MAX;
      }
      return VK_BLEND_OP_ADD;
    }

    VkColorComponentFlags vulkanColorWriteMask(std::uint8_t mask)
    {
      VkColorComponentFlags result = 0;
      if (mask & ColorWriteRed)
        result |= VK_COLOR_COMPONENT_R_BIT;
      if (mask & ColorWriteGreen)
        result |= VK_COLOR_COMPONENT_G_BIT;
      if (mask & ColorWriteBlue)
        result |= VK_COLOR_COMPONENT_B_BIT;
      if (mask & ColorWriteAlpha)
        result |= VK_COLOR_COMPONENT_A_BIT;
      return result;
    }

    VkStencilOp vulkanStencilOperation(StencilOperation operation)
    {
      switch (operation)
      {
      case StencilOperation::Keep: return VK_STENCIL_OP_KEEP;
      case StencilOperation::Zero: return VK_STENCIL_OP_ZERO;
      case StencilOperation::Replace: return VK_STENCIL_OP_REPLACE;
      case StencilOperation::IncrementClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
      case StencilOperation::DecrementClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
      case StencilOperation::Invert: return VK_STENCIL_OP_INVERT;
      case StencilOperation::IncrementWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
      case StencilOperation::DecrementWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
      }
      return VK_STENCIL_OP_KEEP;
    }

    bool spirv(const ShaderDesc &desc)
    {
      if (!desc.source.data || desc.source.size < 20 ||
          desc.source.size % sizeof(std::uint32_t) != 0)
        return false;
      const std::uint32_t *words =
          static_cast<const std::uint32_t *>(desc.source.data);
      return words[0] == 0x07230203;
    }

  } // namespace

  VulkanDevice::VulkanDevice(const DeviceDesc &desc)
      : mDesc(desc), mWidth(desc.surface.width), mHeight(desc.surface.height)
  {
    if (desc.surface.nativeHandle)
      mSurface = static_cast<const VulkanSurface *>(desc.surface.nativeHandle);
  }

  VulkanDevice::~VulkanDevice() { shutdown(); }

  bool VulkanDevice::initialize(GPUError *error)
  {
    if (!createInstance() || !createSurface() || !selectPhysicalDevice() ||
        !createLogicalDevice())
    {
      reportDeviceCreationFailure(error, GPUErrorCode::DeviceCreationFailed,
                                 "failed to initialize the Vulkan device");
      shutdown();
      return false;
    }

    if (!requirementsMet(mDesc.requiredCapabilities, mCapabilities))
    {
      reportDeviceCreationFailure(
          error, GPUErrorCode::UnsupportedFeature,
          "the Vulkan device does not satisfy the requested capabilities");
      shutdown();
      return false;
    }

    if (mSurface && !createSwapchain())
    {
      reportDeviceCreationFailure(error, GPUErrorCode::DeviceCreationFailed,
                                 "failed to create the Vulkan swapchain");
      shutdown();
      return false;
    }

    mAlive = true;
    return true;
  }

  const GPUCapabilities &VulkanDevice::capabilities() const
  {
    return mCapabilities;
  }

  std::uint64_t VulkanDevice::totalErrorCount() const
  {
    return mErrors.totalErrorCount();
  }

  std::uint32_t VulkanDevice::pendingErrorCount() const
  {
    return mErrors.pendingErrorCount();
  }

  bool VulkanDevice::getError(GPUError &error) { return mErrors.getError(error); }

  void VulkanDevice::clearErrors() { mErrors.clearErrors(); }

  SamplerHandle VulkanDevice::createSampler(const SamplerDesc &desc)
  {
    if (!mAlive || desc.maxAnisotropy < 1.0f || desc.lodMin > desc.lodMax)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::CreateSampler,
           "invalid Vulkan sampler descriptor");
      return {};
    }
    if (desc.maxAnisotropy > 1.0f && !mSamplerAnisotropySupported)
    {
      push(GPUErrorCode::UnsupportedFeature, GPUOperation::CreateSampler,
           "anisotropic filtering is unavailable on this device");
      return {};
    }
    const bool anisotropyEnabled = mSamplerAnisotropySupported && desc.maxAnisotropy > 1.0f;
    VkSamplerCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    createInfo.magFilter = vulkanFilter(desc.magFilter);
    createInfo.minFilter = vulkanFilter(desc.minFilter);
    createInfo.mipmapMode = vulkanMipmapMode(desc.mipFilter);
    createInfo.addressModeU = vulkanAddressMode(desc.addressU);
    createInfo.addressModeV = vulkanAddressMode(desc.addressV);
    createInfo.addressModeW = vulkanAddressMode(desc.addressW);
    createInfo.mipLodBias = 0.0f;
    createInfo.anisotropyEnable = anisotropyEnabled ? VK_TRUE : VK_FALSE;
    createInfo.maxAnisotropy =
        anisotropyEnabled ? std::min(desc.maxAnisotropy, mMaxSamplerAnisotropy) : 1.0f;
    createInfo.compareEnable = desc.compareEnabled ? VK_TRUE : VK_FALSE;
    createInfo.compareOp = vulkanCompareOp(desc.compare);
    createInfo.minLod = desc.lodMin;
    createInfo.maxLod = desc.lodMax;
    createInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    createInfo.unnormalizedCoordinates = VK_FALSE;
    SamplerObject object;
    auto cleanup = onFailure([&] {
      if (object.sampler)
        vkDestroySampler(mDevice, object.sampler, nullptr);
    });
    if (vkCreateSampler(mDevice, &createInfo, nullptr, &object.sampler) != VK_SUCCESS)
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateSampler,
           "failed to create a Vulkan sampler");
      return {};
    }
    const SamplerHandle handle = mSamplers.insert(object);
    if (!handle.valid())
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateSampler,
           "failed to allocate a Vulkan sampler handle");
      return {};
    }
    cleanup.dismiss();
    return handle;
  }

  PipelineHandle VulkanDevice::createPipeline(const PipelineDesc &desc)
  {
    const bool hasCompute = desc.compute.source.data != nullptr;
    const bool hasGraphics =
        desc.vertex.source.data != nullptr || desc.fragment.source.data != nullptr;
    if (!mAlive || hasCompute == hasGraphics)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::CreatePipeline,
           "a Vulkan pipeline must be either compute or graphics");
      return {};
    }
    if (hasCompute)
      return createComputePipeline(desc);
    // colorTargetCount == 0 is only valid for a depth/stencil-only
    // pipeline (a shadow map depth pre-pass, say) - a pipeline with
    // neither a color nor a depth/stencil attachment writes nothing.
    if (!spirv(desc.vertex) || !spirv(desc.fragment) || !desc.vertex.entryPoint ||
        !desc.fragment.entryPoint ||
        (desc.colorTargetCount == 0 && !desc.depthStencil.depthTestEnabled &&
         !desc.depthStencil.stencilEnabled) ||
        desc.colorTargetCount > PipelineDesc::MaxColorTargets ||
        desc.vertexBufferCount > PipelineDesc::MaxVertexBuffers)
    {
      push(GPUErrorCode::ShaderCompilationFailed, GPUOperation::CreatePipeline,
           "Vulkan graphics pipelines require vertex and fragment SPIR-V shaders");
      return {};
    }
    const bool hasTessControl = desc.tessControl.source.data != nullptr;
    const bool hasTessEvaluation = desc.tessEvaluation.source.data != nullptr;
    const bool hasTessellation = hasTessControl || hasTessEvaluation;
    const bool hasGeometry = desc.geometry.source.data != nullptr;
    if (hasTessellation &&
        (!mCapabilities.tessellationShader || !hasTessControl || !hasTessEvaluation ||
         !spirv(desc.tessControl) || !spirv(desc.tessEvaluation) ||
         !desc.tessControl.entryPoint || !desc.tessEvaluation.entryPoint ||
         desc.topology != Topology::Patches || desc.patchControlPoints == 0 ||
         desc.patchControlPoints > 32))
    {
      push(GPUErrorCode::UnsupportedFeature, GPUOperation::CreatePipeline,
           "Vulkan tessellation requires both control and evaluation SPIR-V "
           "shaders, Topology::Patches and device support");
      return {};
    }
    if (!hasTessellation && desc.topology == Topology::Patches)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::CreatePipeline,
           "Topology::Patches requires a tessellation control/evaluation pair");
      return {};
    }
    if (hasGeometry &&
        (!mCapabilities.geometryShader || !spirv(desc.geometry) || !desc.geometry.entryPoint))
    {
      push(GPUErrorCode::UnsupportedFeature, GPUOperation::CreatePipeline,
           "Vulkan geometry shaders require a valid SPIR-V module and device support");
      return {};
    }
    VkFormat colorFormats[PipelineDesc::MaxColorTargets] = {};
    for (std::uint32_t index = 0; index < desc.colorTargetCount; ++index)
    {
      if (desc.colorTargets[index].surface)
      {
        if (mSwapchainFormat == VK_FORMAT_UNDEFINED)
        {
          push(GPUErrorCode::InvalidArgument, GPUOperation::CreatePipeline,
               "the Vulkan pipeline targets the surface but the device has "
               "no active swapchain");
          return {};
        }
        colorFormats[index] = mSwapchainFormat;
        continue;
      }
      colorFormats[index] = vulkanFormat(desc.colorTargets[index].format);
      if (colorFormats[index] == VK_FORMAT_UNDEFINED ||
          depthFormat(desc.colorTargets[index].format))
      {
        push(GPUErrorCode::UnsupportedFormat, GPUOperation::CreatePipeline,
             "the Vulkan pipeline color format is unsupported");
        return {};
      }
    }
    const bool usesDepthStencil = desc.depthStencil.depthTestEnabled ||
                                  desc.depthStencil.stencilEnabled;
    const VkFormat nativeDepthFormat = usesDepthStencil
                                           ? vulkanFormat(desc.depthStencil.format)
                                           : VK_FORMAT_UNDEFINED;
    if (usesDepthStencil &&
        (nativeDepthFormat == VK_FORMAT_UNDEFINED ||
         !depthFormat(desc.depthStencil.format)))
    {
      push(GPUErrorCode::UnsupportedFormat, GPUOperation::CreatePipeline,
           "the Vulkan pipeline depth format is unsupported");
      return {};
    }
    if (usesDepthStencil)
    {
      VkFormatProperties properties = {};
      vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, nativeDepthFormat,
                                          &properties);
      if (!(properties.optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
      {
        push(GPUErrorCode::UnsupportedFormat, GPUOperation::CreatePipeline,
             "the Vulkan device does not support the depth stencil format");
        return {};
      }
    }
    Vector<VkVertexInputBindingDescription> bindings;
    Vector<VkVertexInputAttributeDescription> attributes;
    bindings.reserve(desc.vertexBufferCount);
    for (std::uint32_t binding = 0; binding < desc.vertexBufferCount; ++binding)
    {
      const VertexBufferLayout &layout = desc.vertexBuffers[binding];
      if (layout.stride == 0 ||
          layout.attributeCount > VertexBufferLayout::MaxAttributes)
      {
        push(GPUErrorCode::InvalidArgument, GPUOperation::CreatePipeline,
             "the Vulkan vertex buffer layout is invalid");
        return {};
      }
      VkVertexInputBindingDescription bindingInfo = {};
      bindingInfo.binding = binding;
      bindingInfo.stride = layout.stride;
      bindingInfo.inputRate = vulkanVertexInputRate(layout.stepMode);
      bindings.push_back(bindingInfo);
      for (std::uint32_t attribute = 0; attribute < layout.attributeCount;
           ++attribute)
      {
        const VertexAttribute &source = layout.attributes[attribute];
        const VkFormat format = vulkanVertexFormat(source.format);
        if (format == VK_FORMAT_UNDEFINED || source.offset >= layout.stride)
        {
          push(GPUErrorCode::InvalidArgument, GPUOperation::CreatePipeline,
               "the Vulkan vertex attribute is invalid");
          return {};
        }
        VkVertexInputAttributeDescription attributeInfo = {};
        attributeInfo.location = source.shaderLocation;
        attributeInfo.binding = binding;
        attributeInfo.format = format;
        attributeInfo.offset = source.offset;
        attributes.push_back(attributeInfo);
      }
    }
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    VkShaderModule tessControlModule = VK_NULL_HANDLE;
    VkShaderModule tessEvaluationModule = VK_NULL_HANDLE;
    VkShaderModule geometryModule = VK_NULL_HANDLE;
    const auto createModule = [this](const ShaderDesc &shader,
                                     VkShaderModule &module) {
      VkShaderModuleCreateInfo createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      createInfo.codeSize = static_cast<std::size_t>(shader.source.size);
      createInfo.pCode = static_cast<const std::uint32_t *>(shader.source.data);
      return vkCreateShaderModule(mDevice, &createInfo, nullptr, &module) == VK_SUCCESS;
    };
    const auto destroyModules = [this, &vertexModule, &fragmentModule, &tessControlModule,
                                 &tessEvaluationModule, &geometryModule]() {
      if (vertexModule)
        vkDestroyShaderModule(mDevice, vertexModule, nullptr);
      if (fragmentModule)
        vkDestroyShaderModule(mDevice, fragmentModule, nullptr);
      if (tessControlModule)
        vkDestroyShaderModule(mDevice, tessControlModule, nullptr);
      if (tessEvaluationModule)
        vkDestroyShaderModule(mDevice, tessEvaluationModule, nullptr);
      if (geometryModule)
        vkDestroyShaderModule(mDevice, geometryModule, nullptr);
    };
    if (!createModule(desc.vertex, vertexModule) ||
        !createModule(desc.fragment, fragmentModule) ||
        (hasTessControl && !createModule(desc.tessControl, tessControlModule)) ||
        (hasTessEvaluation && !createModule(desc.tessEvaluation, tessEvaluationModule)) ||
        (hasGeometry && !createModule(desc.geometry, geometryModule)))
    {
      destroyModules();
      push(GPUErrorCode::ShaderCompilationFailed, GPUOperation::CreatePipeline,
           "failed to create Vulkan shader modules from SPIR-V");
      return {};
    }
    PipelineObject object;
    object.colorTargetCount = desc.colorTargetCount;
    for (std::uint32_t index = 0; index < desc.colorTargetCount; ++index)
      object.colorFormats[index] = colorFormats[index];
    object.depthFormat = nativeDepthFormat;
    object.stencilEnabled = desc.depthStencil.stencilEnabled;
    object.vertexBufferCount = desc.vertexBufferCount;
    // Undoes whatever of layout/pipeline got created below if any later
    // step fails - dismissed once mPipelines.insert() commits `object` to
    // the resource pool. The shader modules above have their own
    // self-contained destroyModules() since they're always torn down
    // right after vkCreateGraphicsPipelines() either way.
    auto cleanup = onFailure([&] {
      if (object.pipeline)
        vkDestroyPipeline(mDevice, object.pipeline, nullptr);
      if (object.layout)
        vkDestroyPipelineLayout(mDevice, object.layout, nullptr);
    });
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    // Sets 2/3 (storage buffer/image) let a graphics pipeline's shaders do
    // vertex pulling or other SSBO/image access, same layouts compute
    // pipelines use at sets 0/1 - see bindStorageBuffer(). Set 4 (only
    // when the device supports descriptor indexing) is the bindless
    // texture array every pipeline shares - see registerBindlessTexture().
    const VkDescriptorSetLayout setLayouts[] = {mUniformSetLayout, mTextureSetLayout,
                                                mStorageBufferSetLayout,
                                                mStorageImageSetLayout,
                                                mBindlessTextureSetLayout};
    layoutInfo.setLayoutCount = mCapabilities.bindlessTextures ? 5 : 4;
    layoutInfo.pSetLayouts = setLayouts;
    if (vkCreatePipelineLayout(mDevice, &layoutInfo, nullptr, &object.layout) !=
        VK_SUCCESS)
    {
      destroyModules();
      push(GPUErrorCode::PipelineCreationFailed, GPUOperation::CreatePipeline,
           "failed to create a Vulkan pipeline layout");
      return {};
    }
    VkPipelineShaderStageCreateInfo stages[5] = {};
    std::uint32_t stageCount = 0;
    const auto addStage = [&stages, &stageCount](VkShaderStageFlagBits stage,
                                                 VkShaderModule module,
                                                 const char *entryPoint) {
      stages[stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[stageCount].stage = stage;
      stages[stageCount].module = module;
      stages[stageCount].pName = entryPoint;
      ++stageCount;
    };
    addStage(VK_SHADER_STAGE_VERTEX_BIT, vertexModule, desc.vertex.entryPoint);
    if (hasTessControl)
      addStage(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, tessControlModule,
               desc.tessControl.entryPoint);
    if (hasTessEvaluation)
      addStage(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, tessEvaluationModule,
               desc.tessEvaluation.entryPoint);
    if (hasGeometry)
      addStage(VK_SHADER_STAGE_GEOMETRY_BIT, geometryModule, desc.geometry.entryPoint);
    addStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule, desc.fragment.entryPoint);
    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount =
        static_cast<std::uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions =
        bindings.empty() ? nullptr : bindings.data();
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions =
        attributes.empty() ? nullptr : attributes.data();
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = vulkanTopology(desc.topology);
    VkPipelineViewportStateCreateInfo viewport = {};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.depthClampEnable = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = vulkanCullMode(desc.raster.cullMode);
    raster.frontFace = vulkanFrontFace(desc.raster.frontFace);
    raster.depthBiasEnable = desc.raster.depthBiasConstant != 0.0f ||
                            desc.raster.depthBiasSlope != 0.0f;
    raster.depthBiasConstantFactor = desc.raster.depthBiasConstant;
    raster.depthBiasSlopeFactor = desc.raster.depthBiasSlope;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depthStencil.depthTestEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = vulkanCompareOp(desc.depthStencil.depthCompare);
    depthStencil.stencilTestEnable = desc.depthStencil.stencilEnabled ? VK_TRUE : VK_FALSE;
    const auto stencilState = [&desc](const StencilFaceState &source) {
      VkStencilOpState state = {};
      state.failOp = vulkanStencilOperation(source.failOperation);
      state.passOp = vulkanStencilOperation(source.passOperation);
      state.depthFailOp = vulkanStencilOperation(source.depthFailOperation);
      state.compareOp = vulkanCompareOp(source.compare);
      state.compareMask = desc.depthStencil.stencilReadMask;
      state.writeMask = desc.depthStencil.stencilWriteMask;
      return state;
    };
    depthStencil.front = stencilState(desc.depthStencil.stencilFront);
    depthStencil.back = stencilState(desc.depthStencil.stencilBack);
    VkPipelineColorBlendAttachmentState colorBlends[PipelineDesc::MaxColorTargets] = {};
    for (std::uint32_t index = 0; index < desc.colorTargetCount; ++index)
    {
      const ColorTargetState &target = desc.colorTargets[index];
      colorBlends[index].blendEnable = target.blendEnabled ? VK_TRUE : VK_FALSE;
      colorBlends[index].srcColorBlendFactor = vulkanBlendFactor(target.colorBlend.sourceFactor);
      colorBlends[index].dstColorBlendFactor = vulkanBlendFactor(target.colorBlend.destinationFactor);
      colorBlends[index].colorBlendOp = vulkanBlendOperation(target.colorBlend.operation);
      colorBlends[index].srcAlphaBlendFactor = vulkanBlendFactor(target.alphaBlend.sourceFactor);
      colorBlends[index].dstAlphaBlendFactor = vulkanBlendFactor(target.alphaBlend.destinationFactor);
      colorBlends[index].alphaBlendOp = vulkanBlendOperation(target.alphaBlend.operation);
      colorBlends[index].colorWriteMask = vulkanColorWriteMask(target.writeMask);
    }
    VkPipelineColorBlendStateCreateInfo colorBlendState = {};
    colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendState.attachmentCount = desc.colorTargetCount;
    colorBlendState.pAttachments = colorBlends;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                             VK_DYNAMIC_STATE_SCISSOR,
                                             VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dynamic = {};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = desc.depthStencil.stencilEnabled ? 3 : 2;
    dynamic.pDynamicStates = dynamicStates;
    VkPipelineRenderingCreateInfo rendering = {};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = desc.colorTargetCount;
    rendering.pColorAttachmentFormats = colorFormats;
    rendering.depthAttachmentFormat = nativeDepthFormat;
    rendering.stencilAttachmentFormat = desc.depthStencil.stencilEnabled
                                            ? nativeDepthFormat
                                            : VK_FORMAT_UNDEFINED;
    VkPipelineTessellationStateCreateInfo tessellationState = {};
    tessellationState.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tessellationState.patchControlPoints = desc.patchControlPoints;
    VkGraphicsPipelineCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    createInfo.pNext = &rendering;
    createInfo.stageCount = stageCount;
    createInfo.pStages = stages;
    createInfo.pVertexInputState = &vertexInput;
    createInfo.pInputAssemblyState = &inputAssembly;
    createInfo.pTessellationState = hasTessellation ? &tessellationState : nullptr;
    createInfo.pViewportState = &viewport;
    createInfo.pRasterizationState = &raster;
    createInfo.pMultisampleState = &multisample;
    createInfo.pDepthStencilState = usesDepthStencil
                                        ? &depthStencil
                                        : nullptr;
    createInfo.pColorBlendState = &colorBlendState;
    createInfo.pDynamicState = &dynamic;
    createInfo.layout = object.layout;
    const VkResult result = vkCreateGraphicsPipelines(
        mDevice, VK_NULL_HANDLE, 1, &createInfo, nullptr, &object.pipeline);
    destroyModules();
    if (result != VK_SUCCESS)
    {
      push(GPUErrorCode::PipelineCreationFailed, GPUOperation::CreatePipeline,
           "failed to create a Vulkan graphics pipeline");
      return {};
    }
    reflectSpirvStage(desc.vertex, false, object.reflection);
    reflectSpirvStage(desc.fragment, false, object.reflection);
    if (hasTessControl)
      reflectSpirvStage(desc.tessControl, false, object.reflection);
    if (hasTessEvaluation)
      reflectSpirvStage(desc.tessEvaluation, false, object.reflection);
    if (hasGeometry)
      reflectSpirvStage(desc.geometry, false, object.reflection);
    const PipelineHandle handle = mPipelines.insert(object);
    if (!handle.valid())
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreatePipeline,
           "failed to allocate a Vulkan pipeline handle");
      return {};
    }
    cleanup.dismiss();
    return handle;
  }

  PipelineHandle VulkanDevice::createComputePipeline(const PipelineDesc &desc)
  {
    if (!mCapabilities.compute || !spirv(desc.compute) || !desc.compute.entryPoint)
    {
      push(GPUErrorCode::ShaderCompilationFailed, GPUOperation::CreatePipeline,
           "Vulkan compute pipelines require a compute SPIR-V shader");
      return {};
    }
    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = static_cast<std::size_t>(desc.compute.source.size);
    moduleInfo.pCode = static_cast<const std::uint32_t *>(desc.compute.source.data);
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(mDevice, &moduleInfo, nullptr, &module) != VK_SUCCESS)
    {
      push(GPUErrorCode::ShaderCompilationFailed, GPUOperation::CreatePipeline,
           "failed to create a Vulkan compute shader module from SPIR-V");
      return {};
    }
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    const VkDescriptorSetLayout setLayouts[] = {mStorageBufferSetLayout,
                                                mStorageImageSetLayout};
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    PipelineObject object;
    object.isCompute = true;
    // Undoes whatever of layout/pipeline got created below if any later
    // step fails - dismissed once mPipelines.insert() commits `object` to
    // the resource pool. `module` isn't covered here: it's always torn
    // down right after vkCreateComputePipelines() below either way.
    auto cleanup = onFailure([&] {
      if (object.pipeline)
        vkDestroyPipeline(mDevice, object.pipeline, nullptr);
      if (object.layout)
        vkDestroyPipelineLayout(mDevice, object.layout, nullptr);
    });
    if (vkCreatePipelineLayout(mDevice, &layoutInfo, nullptr, &object.layout) !=
        VK_SUCCESS)
    {
      vkDestroyShaderModule(mDevice, module, nullptr);
      push(GPUErrorCode::PipelineCreationFailed, GPUOperation::CreatePipeline,
           "failed to create a Vulkan compute pipeline layout");
      return {};
    }
    VkPipelineShaderStageCreateInfo stageInfo = {};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = module;
    stageInfo.pName = desc.compute.entryPoint;
    VkComputePipelineCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    createInfo.stage = stageInfo;
    createInfo.layout = object.layout;
    const VkResult result = vkCreateComputePipelines(
        mDevice, VK_NULL_HANDLE, 1, &createInfo, nullptr, &object.pipeline);
    vkDestroyShaderModule(mDevice, module, nullptr);
    if (result != VK_SUCCESS)
    {
      push(GPUErrorCode::PipelineCreationFailed, GPUOperation::CreatePipeline,
           "failed to create a Vulkan compute pipeline");
      return {};
    }
    reflectSpirvStage(desc.compute, true, object.reflection);
    const PipelineHandle handle = mPipelines.insert(object);
    if (!handle.valid())
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreatePipeline,
           "failed to allocate a Vulkan compute pipeline handle");
      return {};
    }
    cleanup.dismiss();
    return handle;
  }

  void VulkanDevice::destroy(PipelineHandle handle)
  {
    waitForGPU();
    PipelineObject *object = mPipelines.find(handle);
    if (!object || mActivePipeline == handle)
    {
      push(!object ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::Destroy, "invalid Vulkan pipeline destroy");
      return;
    }
    vkDestroyPipeline(mDevice, object->pipeline, nullptr);
    vkDestroyPipelineLayout(mDevice, object->layout, nullptr);
    mPipelines.erase(handle);
  }

  bool VulkanDevice::reflectPipeline(PipelineHandle handle, PipelineReflection &reflection)
  {
    PipelineObject *pipeline = mPipelines.find(handle);
    if (!pipeline)
    {
      push(GPUErrorCode::InvalidHandle, GPUOperation::ReflectPipeline,
           "invalid Vulkan pipeline handle");
      return false;
    }
    reflection = pipeline->reflection;
    return true;
  }

  QueryHandle VulkanDevice::createQuery(QueryType type)
  {
    const bool supported = type == QueryType::Occlusion
                               ? mCapabilities.occlusionQueries
                               : type == QueryType::Timestamp &&
                                     mCapabilities.timestampQueries;
    if (!mAlive || !supported)
    {
      push(GPUErrorCode::UnsupportedFeature, GPUOperation::CreateQuery,
           "the Vulkan query type is unavailable on this device");
      return {};
    }
    QueryObject object;
    object.type = type;
    auto cleanup = onFailure([&] {
      if (object.pool)
        vkDestroyQueryPool(mDevice, object.pool, nullptr);
    });
    VkQueryPoolCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    createInfo.queryType = type == QueryType::Occlusion ? VK_QUERY_TYPE_OCCLUSION
                                                         : VK_QUERY_TYPE_TIMESTAMP;
    createInfo.queryCount = 1;
    if (vkCreateQueryPool(mDevice, &createInfo, nullptr, &object.pool) != VK_SUCCESS)
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateQuery,
           "failed to create a Vulkan query pool");
      return {};
    }
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (!beginCommands(commandBuffer))
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::CreateQuery,
           "failed to initialize a Vulkan query pool");
      return {};
    }
    vkCmdResetQueryPool(commandBuffer, object.pool, 0, 1);
    if (!submitCommands(commandBuffer))
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::CreateQuery,
           "failed to reset a Vulkan query pool");
      return {};
    }
    const QueryHandle handle = mQueries.insert(object);
    if (!handle.valid())
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateQuery,
           "failed to allocate a Vulkan query handle");
      return {};
    }
    cleanup.dismiss();
    return handle;
  }

  void VulkanDevice::destroy(QueryHandle handle)
  {
    waitForGPU();
    QueryObject *query = mQueries.find(handle);
    if (!query || query->active)
    {
      push(!query ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::Destroy, "invalid Vulkan query destroy");
      return;
    }
    vkDestroyQueryPool(mDevice, query->pool, nullptr);
    mQueries.erase(handle);
  }

  TextureHandle VulkanDevice::createTexture(const TextureDesc &desc)
  {
    const VkFormat format = vulkanFormat(desc.format);
    const bool isCube = desc.dimension == TextureDimension::TextureCube;
    if (!mAlive || (desc.dimension != TextureDimension::Texture2D && !isCube) ||
        desc.width == 0 || desc.height == 0 || desc.depthOrLayers != 1 ||
        (isCube && desc.width != desc.height) ||
        (isCube && desc.initialData.data != nullptr) ||
        desc.mipCount == 0 ||
        desc.mipCount > maximumMipCount(desc.width, desc.height) ||
        desc.sampleCount != 1 || desc.usage == 0 ||
        format == VK_FORMAT_UNDEFINED ||
        (desc.initialData.data == nullptr) != (desc.initialData.size == 0))
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::CreateTexture,
           "the Vulkan texture descriptor is not supported in this phase");
      return {};
    }
    const std::uint32_t texelBytes = bytesPerTexel(desc.format);
    const std::uint64_t initialSize =
        static_cast<std::uint64_t>(desc.width) * desc.height * texelBytes;
    if ((desc.initialData.data &&
         (texelBytes == 0 || desc.initialData.size < initialSize)) ||
        (desc.initialData.data && depthFormat(desc.format)) ||
        (depthFormat(desc.format) && desc.mipCount != 1))
    {
      push(GPUErrorCode::UnsupportedFormat, GPUOperation::CreateTexture,
           "the Vulkan texture format does not support initial data");
      return {};
    }
    if (depthFormat(desc.format))
    {
      VkFormatProperties properties = {};
      vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, format, &properties);
      if (!(properties.optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
      {
        push(GPUErrorCode::UnsupportedFormat, GPUOperation::CreateTexture,
             "the Vulkan device does not support the depth stencil format");
        return {};
      }
    }
    TextureObject object;
    object.format = desc.format;
    object.width = desc.width;
    object.height = desc.height;
    object.mipCount = desc.mipCount;
    object.layerCount = isCube ? 6 : 1;
    object.usage = desc.usage;
    object.aspectMask = depthFormat(desc.format) ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                 : VK_IMAGE_ASPECT_COLOR_BIT;
    if (desc.format == Format::Depth24Stencil8)
      object.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    // Undoes whatever of image/memory/view/storageViews got created below
    // if any later step fails - dismissed once mTextures.insert() commits
    // `object` to the resource pool.
    auto cleanup = onFailure([&] {
      for (VkImageView storageView : object.storageViews)
        if (storageView)
          vkDestroyImageView(mDevice, storageView, nullptr);
      if (object.view)
        vkDestroyImageView(mDevice, object.view, nullptr);
      if (object.memory)
        vkFreeMemory(mDevice, object.memory, nullptr);
      if (object.image)
        vkDestroyImage(mDevice, object.image, nullptr);
    });
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = isCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {desc.width, desc.height, 1};
    imageInfo.mipLevels = desc.mipCount;
    imageInfo.arrayLayers = object.layerCount;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (desc.usage & TextureUsageSampled)
      imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (desc.usage & TextureUsageStorage)
      imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (desc.usage & TextureUsageRenderTarget)
      imageInfo.usage |= depthFormat(desc.format)
                             ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                             : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(mDevice, &imageInfo, nullptr, &object.image) != VK_SUCCESS)
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateTexture,
           "failed to create a Vulkan image");
      return {};
    }
    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(mDevice, object.image, &requirements);
    const std::uint32_t memoryType = findMemoryType(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX)
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateTexture,
           "no device-local Vulkan memory type supports the texture");
      return {};
    }
    VkMemoryAllocateInfo allocation = {};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(mDevice, &allocation, nullptr, &object.memory) != VK_SUCCESS ||
        vkBindImageMemory(mDevice, object.image, object.memory, 0) != VK_SUCCESS)
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateTexture,
           "failed to allocate Vulkan texture memory");
      return {};
    }
    if (desc.usage & (TextureUsageSampled | TextureUsageStorage |
                      TextureUsageRenderTarget))
    {
      VkImageViewCreateInfo viewInfo = {};
      viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewInfo.image = object.image;
      viewInfo.viewType = isCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = format;
      viewInfo.subresourceRange.aspectMask = object.aspectMask;
      viewInfo.subresourceRange.levelCount = desc.mipCount;
      viewInfo.subresourceRange.layerCount = object.layerCount;
      if (vkCreateImageView(mDevice, &viewInfo, nullptr, &object.view) != VK_SUCCESS)
      {
        push(GPUErrorCode::OutOfMemory, GPUOperation::CreateTexture,
             "failed to create a Vulkan texture view");
        return {};
      }
    }
    if (desc.usage & TextureUsageStorage)
    {
      VkImageViewCreateInfo storageViewInfo = {};
      storageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      storageViewInfo.image = object.image;
      storageViewInfo.viewType = isCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
      storageViewInfo.format = format;
      storageViewInfo.subresourceRange.aspectMask = object.aspectMask;
      storageViewInfo.subresourceRange.levelCount = 1;
      storageViewInfo.subresourceRange.layerCount = object.layerCount;
      bool storageViewsOk = true;
      for (std::uint32_t level = 0; storageViewsOk && level < desc.mipCount; ++level)
      {
        storageViewInfo.subresourceRange.baseMipLevel = level;
        storageViewsOk = vkCreateImageView(mDevice, &storageViewInfo, nullptr,
                                           &object.storageViews[level]) == VK_SUCCESS;
      }
      if (!storageViewsOk)
      {
        push(GPUErrorCode::OutOfMemory, GPUOperation::CreateTexture,
             "failed to create a Vulkan storage image view");
        return {};
      }
    }
    const TextureHandle handle = mTextures.insert(object);
    if (!handle.valid())
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateTexture,
           "failed to allocate a Vulkan texture handle");
      return {};
    }
    cleanup.dismiss();
    if (!desc.initialData.data)
      return handle;
    mTextures.find(handle)->usage |= TextureUsageCopyDestination;
    TextureRegion region;
    region.width = desc.width;
    region.height = desc.height;
    const bool uploaded = updateTexture(handle, region, desc.initialData, {});
    mTextures.find(handle)->usage = desc.usage;
    if (uploaded)
      return handle;
    destroy(handle);
    return {};
  }

  void VulkanDevice::destroy(TextureHandle handle)
  {
    waitForGPU();
    TextureObject *object = mTextures.find(handle);
    if (!object || mInRenderPass)
    {
      push(!object ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::Destroy,
           "invalid Vulkan texture handle");
      return;
    }
    vkDestroyImageView(mDevice, object->view, nullptr);
    for (VkImageView storageView : object->storageViews)
      if (storageView)
        vkDestroyImageView(mDevice, storageView, nullptr);
    vkDestroyImage(mDevice, object->image, nullptr);
    vkFreeMemory(mDevice, object->memory, nullptr);
    mTextures.erase(handle);
    for (TextureBinding &binding : mTextureBindings)
      if (binding.texture == handle)
        binding.texture = TextureHandle();
    for (StorageTextureBinding &binding : mStorageTextureBindings)
      if (binding.texture == handle)
        binding = StorageTextureBinding{};
  }

  void VulkanDevice::destroy(SamplerHandle handle)
  {
    waitForGPU();
    SamplerObject *object = mSamplers.find(handle);
    if (!object || mInRenderPass)
    {
      push(!object ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::Destroy,
           "invalid Vulkan sampler handle");
      return;
    }
    vkDestroySampler(mDevice, object->sampler, nullptr);
    mSamplers.erase(handle);
    for (TextureBinding &binding : mTextureBindings)
      if (binding.sampler == handle)
        binding.sampler = SamplerHandle();
  }

  BufferHandle VulkanDevice::createBuffer(const BufferDesc &desc)
  {
    if (!mAlive || desc.size == 0 || desc.usage == 0 ||
        (desc.initialData.data && desc.initialData.size > desc.size))
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::CreateBuffer,
           "invalid Vulkan buffer descriptor");
      return {};
    }

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (desc.usage & BufferUsageVertex)
      usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (desc.usage & BufferUsageIndex)
      usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (desc.usage & BufferUsageUniform)
      usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (desc.usage & BufferUsageStorage)
      usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (desc.usage & BufferUsageIndirect)
      usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    VkBufferCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.size = desc.size;
    createInfo.usage = usage;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    BufferObject object;
    object.size = desc.size;
    object.usage = desc.usage;
    // Undoes whatever of buffer/memory/mapping got created below if any
    // later step fails - dismissed once mBuffers.insert() commits `object`
    // to the resource pool.
    auto cleanup = onFailure([&] {
      if (object.mapped)
        vkUnmapMemory(mDevice, object.memory);
      if (object.memory)
        vkFreeMemory(mDevice, object.memory, nullptr);
      if (object.buffer)
        vkDestroyBuffer(mDevice, object.buffer, nullptr);
    });
    if (vkCreateBuffer(mDevice, &createInfo, nullptr, &object.buffer) != VK_SUCCESS)
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateBuffer,
           "failed to create a Vulkan buffer");
      return {};
    }
    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(mDevice, object.buffer, &requirements);
    const std::uint32_t memoryType = findMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memoryType == UINT32_MAX)
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateBuffer,
           "no host-visible Vulkan memory type supports the buffer");
      return {};
    }
    VkMemoryAllocateInfo allocationInfo = {};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(mDevice, &allocationInfo, nullptr, &object.memory) != VK_SUCCESS ||
        vkBindBufferMemory(mDevice, object.buffer, object.memory, 0) != VK_SUCCESS ||
        vkMapMemory(mDevice, object.memory, 0, VK_WHOLE_SIZE, 0,
                    &object.mapped) != VK_SUCCESS)
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateBuffer,
           "failed to allocate Vulkan buffer memory");
      return {};
    }
    if (desc.initialData.data && desc.initialData.size)
      std::memcpy(object.mapped, desc.initialData.data,
                  static_cast<std::size_t>(desc.initialData.size));
    const BufferHandle handle = mBuffers.insert(object);
    if (!handle.valid())
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateBuffer,
           "failed to allocate a Vulkan buffer handle");
      return {};
    }
    cleanup.dismiss();
    return handle;
  }

  void VulkanDevice::destroy(BufferHandle handle)
  {
    waitForGPU();
    BufferObject *object = mBuffers.find(handle);
    if (!object || mInRenderPass)
    {
      push(!object ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::Destroy,
           "invalid Vulkan buffer handle");
      return;
    }
    if (object->mapped)
      vkUnmapMemory(mDevice, object->memory);
    vkDestroyBuffer(mDevice, object->buffer, nullptr);
    vkFreeMemory(mDevice, object->memory, nullptr);
    mBuffers.erase(handle);
    for (UniformBinding &binding : mUniformBindings)
      if (binding.handle == handle)
        binding = UniformBinding{};
    for (StorageBufferBinding &binding : mStorageBufferBindings)
      if (binding.handle == handle)
        binding = StorageBufferBinding{};
  }

  bool VulkanDevice::updateBuffer(BufferHandle handle, std::uint64_t offset,
                                  DataView data)
  {
    BufferObject *object = mBuffers.find(handle);
    if (!object || !data.data || data.size == 0 || offset > object->size ||
        data.size > object->size - offset)
    {
      push(!object ? GPUErrorCode::InvalidHandle : GPUErrorCode::OutOfBounds,
           GPUOperation::UpdateBuffer, "invalid Vulkan buffer update");
      return false;
    }
    // object->mapped is a persistent host-visible pointer with no sync of
    // its own - present() deliberately returns without blocking (see
    // GPU.h), so without this wait a previous frame's still-in-flight
    // command buffer could still be reading this same memory (e.g. as a
    // bound vertex/uniform/storage buffer) while the memcpy below
    // clobbers it. destroy(BufferHandle) already waits for the same
    // reason; do the same here rather than let the two paths diverge.
    waitForGPU();
    std::memcpy(static_cast<std::uint8_t *>(object->mapped) + offset, data.data,
                static_cast<std::size_t>(data.size));
    return true;
  }

  bool VulkanDevice::updateTexture(TextureHandle handle,
                                   const TextureRegion &region, DataView data,
                                   const TextureDataLayout &layout)
  {
    TextureObject *texture = mTextures.find(handle);
    const std::uint32_t mipWidth = texture && region.mipLevel < texture->mipCount
                                       ? mipDimension(texture->width, region.mipLevel)
                                       : 0;
    const std::uint32_t mipHeight = texture && region.mipLevel < texture->mipCount
                                        ? mipDimension(texture->height, region.mipLevel)
                                        : 0;
    if (!mAlive || !texture || !(texture->usage & TextureUsageCopyDestination) ||
        depthFormat(texture ? texture->format : Format::Unknown) || !data.data ||
        data.size == 0 || region.mipLevel >= texture->mipCount || region.x > mipWidth ||
        region.y > mipHeight || region.width == 0 || region.height == 0 ||
        region.depthOrLayers == 0 || region.z >= texture->layerCount ||
        region.depthOrLayers > texture->layerCount - region.z ||
        region.width > mipWidth - region.x ||
        region.height > mipHeight - region.y)
    {
      push(!texture ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::UpdateTexture, "invalid Vulkan texture update");
      return false;
    }
    const std::uint32_t texelBytes = bytesPerTexel(texture->format);
    const std::uint64_t tightRowBytes =
        static_cast<std::uint64_t>(region.width) * texelBytes;
    const std::uint64_t bytesPerRow =
        layout.bytesPerRow == 0 ? tightRowBytes : layout.bytesPerRow;
    const std::uint64_t rowsPerImage =
        layout.rowsPerImage == 0 ? region.height : layout.rowsPerImage;
    std::uint64_t required = 0;
    const std::uint64_t imageStride = bytesPerRow * rowsPerImage;
    const std::uint64_t extraLayers = region.depthOrLayers - 1;
    if (texelBytes == 0 || bytesPerRow > UINT32_MAX ||
        rowsPerImage > UINT32_MAX || bytesPerRow % texelBytes != 0 ||
        !requiredDataSize(layout.offset, bytesPerRow, rowsPerImage,
                          region.height, tightRowBytes, required) ||
        (extraLayers > 0 &&
         (imageStride == 0 ||
          extraLayers > ((std::numeric_limits<std::uint64_t>::max)() - required) / imageStride)) ||
        required + extraLayers * imageStride > data.size)
    {
      push(GPUErrorCode::OutOfBounds, GPUOperation::UpdateTexture,
           "invalid Vulkan texture upload layout");
      return false;
    }
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    if (!createTransferBuffer(data.size, staging, stagingMemory, mapped))
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::UpdateTexture,
           "failed to allocate a Vulkan texture upload buffer");
      return false;
    }
    std::memcpy(mapped, data.data, static_cast<std::size_t>(data.size));
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    bool success = beginCommands(commandBuffer);
    if (success)
    {
      transitionImageMip(commandBuffer, *texture, region.mipLevel,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      VkBufferImageCopy copy = {};
      copy.bufferOffset = layout.offset;
      copy.bufferRowLength = static_cast<std::uint32_t>(bytesPerRow / texelBytes);
      copy.bufferImageHeight = static_cast<std::uint32_t>(rowsPerImage);
      copy.imageSubresource.aspectMask = texture->aspectMask;
      copy.imageSubresource.mipLevel = region.mipLevel;
      copy.imageSubresource.baseArrayLayer = region.z;
      copy.imageSubresource.layerCount = region.depthOrLayers;
      copy.imageOffset = {static_cast<std::int32_t>(region.x),
                          static_cast<std::int32_t>(region.y), 0};
      copy.imageExtent = {region.width, region.height, 1};
      vkCmdCopyBufferToImage(commandBuffer, staging, texture->image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
      transitionImageMip(commandBuffer, *texture, region.mipLevel,
                         finalTextureLayout(texture->usage));
      success = submitCommands(commandBuffer);
    }
    vkUnmapMemory(mDevice, stagingMemory);
    vkDestroyBuffer(mDevice, staging, nullptr);
    vkFreeMemory(mDevice, stagingMemory, nullptr);
    if (!success)
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::UpdateTexture,
           "failed to submit a Vulkan texture update");
      return false;
    }
    return true;
  }

  bool VulkanDevice::generateMipmaps(TextureHandle handle)
  {
    TextureObject *texture = mTextures.find(handle);
    if (!mAlive || !texture || mInRenderPass || texture->mipCount < 2 ||
        depthFormat(texture ? texture->format : Format::Unknown))
    {
      push(!texture ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::GenerateMipmaps, "invalid Vulkan mipmap generation");
      return false;
    }
    VkFormatProperties properties = {};
    vkGetPhysicalDeviceFormatProperties(mPhysicalDevice,
                                        vulkanFormat(texture->format),
                                        &properties);
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                          VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
    {
      push(GPUErrorCode::UnsupportedFormat, GPUOperation::GenerateMipmaps,
           "the Vulkan texture format does not support linear mipmap blits");
      return false;
    }
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (!beginCommands(commandBuffer))
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::GenerateMipmaps,
           "failed to begin Vulkan mipmap generation");
      return false;
    }
    const VkImageLayout finalLayout = finalTextureLayout(texture->usage);
    for (std::uint32_t level = 1; level < texture->mipCount; ++level)
    {
      transitionImageMip(commandBuffer, *texture, level - 1,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      transitionImageMip(commandBuffer, *texture, level,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      VkImageBlit blit = {};
      blit.srcSubresource.aspectMask = texture->aspectMask;
      blit.srcSubresource.mipLevel = level - 1;
      blit.srcSubresource.layerCount = texture->layerCount;
      blit.srcOffsets[1] = {static_cast<std::int32_t>(mipDimension(texture->width, level - 1)),
                            static_cast<std::int32_t>(mipDimension(texture->height, level - 1)), 1};
      blit.dstSubresource.aspectMask = texture->aspectMask;
      blit.dstSubresource.mipLevel = level;
      blit.dstSubresource.layerCount = texture->layerCount;
      blit.dstOffsets[1] = {static_cast<std::int32_t>(mipDimension(texture->width, level)),
                            static_cast<std::int32_t>(mipDimension(texture->height, level)), 1};
      vkCmdBlitImage(commandBuffer, texture->image,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                     VK_FILTER_LINEAR);
      transitionImageMip(commandBuffer, *texture, level - 1, finalLayout);
    }
    transitionImageMip(commandBuffer, *texture, texture->mipCount - 1, finalLayout);
    if (!submitCommands(commandBuffer))
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::GenerateMipmaps,
           "failed to submit Vulkan mipmap generation");
      return false;
    }
    return true;
  }

  bool VulkanDevice::copyTexture(TextureHandle destination,
                                 const TextureOrigin &destinationOrigin,
                                 TextureHandle source,
                                 const TextureRegion &sourceRegion)
  {
    TextureObject *destinationTexture = mTextures.find(destination);
    TextureObject *sourceTexture = mTextures.find(source);
    const std::uint32_t sourceMipWidth =
        sourceTexture && sourceRegion.mipLevel < sourceTexture->mipCount
            ? mipDimension(sourceTexture->width, sourceRegion.mipLevel)
            : 0;
    const std::uint32_t sourceMipHeight =
        sourceTexture && sourceRegion.mipLevel < sourceTexture->mipCount
            ? mipDimension(sourceTexture->height, sourceRegion.mipLevel)
            : 0;
    const std::uint32_t destinationMipWidth =
        destinationTexture && destinationOrigin.mipLevel < destinationTexture->mipCount
            ? mipDimension(destinationTexture->width, destinationOrigin.mipLevel)
            : 0;
    const std::uint32_t destinationMipHeight =
        destinationTexture && destinationOrigin.mipLevel < destinationTexture->mipCount
            ? mipDimension(destinationTexture->height, destinationOrigin.mipLevel)
            : 0;
    if (!mAlive || !destinationTexture || !sourceTexture ||
        destination == source ||
        destinationTexture->format != sourceTexture->format ||
        depthFormat(sourceTexture->format) ||
        !(destinationTexture->usage & TextureUsageCopyDestination) ||
        !(sourceTexture->usage & TextureUsageCopySource) ||
        sourceRegion.mipLevel >= sourceTexture->mipCount ||
        destinationOrigin.mipLevel >= destinationTexture->mipCount ||
        sourceRegion.width == 0 || sourceRegion.height == 0 ||
        sourceRegion.depthOrLayers != 1 || sourceRegion.z != 0 ||
        destinationOrigin.z != 0 || sourceRegion.x > sourceMipWidth ||
        sourceRegion.y > sourceMipHeight ||
        destinationOrigin.x > destinationMipWidth ||
        destinationOrigin.y > destinationMipHeight ||
        sourceRegion.width > sourceMipWidth - sourceRegion.x ||
        sourceRegion.height > sourceMipHeight - sourceRegion.y ||
        sourceRegion.width > destinationMipWidth - destinationOrigin.x ||
        sourceRegion.height > destinationMipHeight - destinationOrigin.y)
    {
      push((!destinationTexture || !sourceTexture) ? GPUErrorCode::InvalidHandle
                                                    : GPUErrorCode::InvalidArgument,
           GPUOperation::Copy, "invalid Vulkan texture copy");
      return false;
    }
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (!beginCommands(commandBuffer))
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::Copy,
           "failed to begin a Vulkan texture copy");
      return false;
    }
    transitionImageMip(commandBuffer, *sourceTexture, sourceRegion.mipLevel,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transitionImageMip(commandBuffer, *destinationTexture,
                       destinationOrigin.mipLevel,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy = {};
    copy.srcSubresource.aspectMask = sourceTexture->aspectMask;
    copy.srcSubresource.mipLevel = sourceRegion.mipLevel;
    copy.srcSubresource.layerCount = 1;
    copy.srcOffset = {static_cast<std::int32_t>(sourceRegion.x),
                      static_cast<std::int32_t>(sourceRegion.y), 0};
    copy.dstSubresource.aspectMask = destinationTexture->aspectMask;
    copy.dstSubresource.mipLevel = destinationOrigin.mipLevel;
    copy.dstSubresource.layerCount = 1;
    copy.dstOffset = {static_cast<std::int32_t>(destinationOrigin.x),
                      static_cast<std::int32_t>(destinationOrigin.y), 0};
    copy.extent = {sourceRegion.width, sourceRegion.height, 1};
    vkCmdCopyImage(commandBuffer, sourceTexture->image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destinationTexture->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    transitionImageMip(commandBuffer, *sourceTexture, sourceRegion.mipLevel,
                       finalTextureLayout(sourceTexture->usage));
    transitionImageMip(commandBuffer, *destinationTexture,
                       destinationOrigin.mipLevel,
                       finalTextureLayout(destinationTexture->usage));
    if (!submitCommands(commandBuffer))
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::Copy,
           "failed to submit a Vulkan texture copy");
      return false;
    }
    return true;
  }

  bool VulkanDevice::readTexture(TextureHandle handle,
                                 const TextureRegion &region,
                                 MutableDataView data,
                                 const TextureDataLayout &layout)
  {
    TextureObject *texture = mTextures.find(handle);
    const std::uint32_t mipWidth = texture && region.mipLevel < texture->mipCount
                                       ? mipDimension(texture->width, region.mipLevel)
                                       : 0;
    const std::uint32_t mipHeight = texture && region.mipLevel < texture->mipCount
                                        ? mipDimension(texture->height, region.mipLevel)
                                        : 0;
    if (!mAlive || !texture || !(texture->usage & TextureUsageCopySource) ||
        depthFormat(texture ? texture->format : Format::Unknown) || !data.data ||
        data.size == 0 || region.mipLevel >= texture->mipCount || region.x > mipWidth ||
        region.y > mipHeight || region.width == 0 || region.height == 0 ||
        region.depthOrLayers != 1 || region.z != 0 ||
        region.width > mipWidth - region.x ||
        region.height > mipHeight - region.y)
    {
      push(!texture ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::ReadTexture, "invalid Vulkan texture read");
      return false;
    }
    const std::uint32_t texelBytes = bytesPerTexel(texture->format);
    const std::uint64_t tightRowBytes =
        static_cast<std::uint64_t>(region.width) * texelBytes;
    const std::uint64_t bytesPerRow =
        layout.bytesPerRow == 0 ? tightRowBytes : layout.bytesPerRow;
    const std::uint64_t rowsPerImage =
        layout.rowsPerImage == 0 ? region.height : layout.rowsPerImage;
    std::uint64_t required = 0;
    if (texelBytes == 0 || bytesPerRow > UINT32_MAX ||
        rowsPerImage > UINT32_MAX || bytesPerRow % texelBytes != 0 ||
        !requiredDataSize(layout.offset, bytesPerRow, rowsPerImage,
                          region.height, tightRowBytes, required) ||
        required > data.size)
    {
      push(GPUErrorCode::OutOfBounds, GPUOperation::ReadTexture,
           "invalid Vulkan texture read layout");
      return false;
    }
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    if (!createTransferBuffer(data.size, staging, stagingMemory, mapped))
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::ReadTexture,
           "failed to allocate a Vulkan texture readback buffer");
      return false;
    }
    std::memcpy(mapped, data.data, static_cast<std::size_t>(data.size));
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    bool success = beginCommands(commandBuffer);
    if (success)
    {
      transitionImageMip(commandBuffer, *texture, region.mipLevel,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      VkBufferImageCopy copy = {};
      copy.bufferOffset = layout.offset;
      copy.bufferRowLength = static_cast<std::uint32_t>(bytesPerRow / texelBytes);
      copy.bufferImageHeight = static_cast<std::uint32_t>(rowsPerImage);
      copy.imageSubresource.aspectMask = texture->aspectMask;
      copy.imageSubresource.mipLevel = region.mipLevel;
      copy.imageSubresource.layerCount = 1;
      copy.imageOffset = {static_cast<std::int32_t>(region.x),
                          static_cast<std::int32_t>(region.y), 0};
      copy.imageExtent = {region.width, region.height, 1};
      vkCmdCopyImageToBuffer(commandBuffer, texture->image,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1,
                             &copy);
      transitionImageMip(commandBuffer, *texture, region.mipLevel,
                         finalTextureLayout(texture->usage));
      success = submitCommands(commandBuffer);
    }
    if (success)
      std::memcpy(data.data, mapped, static_cast<std::size_t>(data.size));
    vkUnmapMemory(mDevice, stagingMemory);
    vkDestroyBuffer(mDevice, staging, nullptr);
    vkFreeMemory(mDevice, stagingMemory, nullptr);
    if (!success)
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::ReadTexture,
           "failed to submit a Vulkan texture readback");
      return false;
    }
    return true;
  }

  bool VulkanDevice::copyBuffer(BufferHandle destination,
                                std::uint64_t destinationOffset,
                                BufferHandle source, std::uint64_t sourceOffset,
                                std::uint64_t size)
  {
    BufferObject *destinationObject = mBuffers.find(destination);
    BufferObject *sourceObject = mBuffers.find(source);
    if (!destinationObject || !sourceObject || size == 0 ||
        destinationOffset > destinationObject->size ||
        sourceOffset > sourceObject->size ||
        size > destinationObject->size - destinationOffset ||
        size > sourceObject->size - sourceOffset)
    {
      push((!destinationObject || !sourceObject) ? GPUErrorCode::InvalidHandle
                                                  : GPUErrorCode::OutOfBounds,
           GPUOperation::Copy, "invalid Vulkan buffer copy");
      return false;
    }
    // See the matching comment in updateBuffer(): both buffers' memory is
    // persistently mapped with no sync of its own.
    waitForGPU();
    std::memmove(static_cast<std::uint8_t *>(destinationObject->mapped) +
                     destinationOffset,
                 static_cast<std::uint8_t *>(sourceObject->mapped) + sourceOffset,
                 static_cast<std::size_t>(size));
    return true;
  }

  void *VulkanDevice::mapBuffer(BufferHandle handle, std::uint64_t offset,
                                std::uint64_t size, MapMode)
  {
    BufferObject *object = mBuffers.find(handle);
    if (!object || size == 0 || offset > object->size ||
        size > object->size - offset)
    {
      push(!object ? GPUErrorCode::InvalidHandle : GPUErrorCode::OutOfBounds,
           GPUOperation::MapBuffer, "invalid Vulkan buffer map");
      return nullptr;
    }
    return static_cast<std::uint8_t *>(object->mapped) + offset;
  }

  bool VulkanDevice::unmapBuffer(BufferHandle handle)
  {
    if (!mBuffers.find(handle))
    {
      push(GPUErrorCode::InvalidHandle, GPUOperation::MapBuffer,
           "invalid Vulkan buffer handle");
      return false;
    }
    return true;
  }

  bool VulkanDevice::beginRenderPass(const RenderPassDesc &desc)
  {
    // colorCount == 0 is only valid alongside a depth/stencil attachment
    // (a shadow map pass, say) - dynamic rendering allows zero color
    // attachments as long as there is a depth one.
    if (!mAlive || mInRenderPass ||
        (desc.colorCount == 0 && !desc.hasDepthStencil) ||
        desc.colorCount > RenderPassDesc::MaxColorAttachments ||
        mFramePendingPresent)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::BeginRenderPass,
           "the Vulkan render pass descriptor is unsupported");
      return false;
    }
    const bool usesSurface = desc.colorCount > 0 && desc.colors[0].surface;
    if (usesSurface &&
        (desc.colorCount != 1 || desc.colors[0].target.texture.valid() ||
         mSurfaceState != SurfaceState::Ready || !mSwapchain))
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::BeginRenderPass,
           "the Vulkan surface attachment is not ready");
      return false;
    }
    TextureObject *colors[RenderPassDesc::MaxColorAttachments] = {};
    TextureObject *color = nullptr;
    if (!usesSurface && desc.colorCount > 0)
    {
      for (std::uint32_t index = 0; index < desc.colorCount; ++index)
      {
        colors[index] = mTextures.find(desc.colors[index].target.texture);
        if (!colors[index] || desc.colors[index].surface || !colors[index]->view ||
            depthFormat(colors[index]->format) ||
            !(colors[index]->usage & TextureUsageRenderTarget) ||
            desc.colors[index].target.mipLevel != 0 ||
            desc.colors[index].target.layer != 0 ||
            (index > 0 && (colors[index]->width != colors[0]->width ||
                           colors[index]->height != colors[0]->height)))
        {
          push(!colors[index] ? GPUErrorCode::InvalidHandle
                              : GPUErrorCode::InvalidArgument,
               GPUOperation::BeginRenderPass, "invalid Vulkan color attachment");
          return false;
        }
      }
      color = colors[0];
    }
    TextureObject *depth = nullptr;
    if (desc.hasDepthStencil)
    {
      depth = mTextures.find(desc.depthStencil.target.texture);
      if (!depth || !depth->view || !depthFormat(depth->format) ||
          !(depth->usage & TextureUsageRenderTarget) ||
          desc.depthStencil.target.mipLevel != 0 ||
          desc.depthStencil.target.layer != 0)
      {
        push(!depth ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
             GPUOperation::BeginRenderPass, "invalid Vulkan depth attachment");
        return false;
      }
      // A color-less (depth/stencil-only) pass has nothing to match sizes
      // against - the depth attachment's own size is authoritative then.
      const bool sizeMismatch =
          usesSurface ? (depth->width != mWidth || depth->height != mHeight)
          : color     ? (depth->width != color->width || depth->height != color->height)
                      : false;
      if (sizeMismatch)
      {
        push(GPUErrorCode::InvalidArgument, GPUOperation::BeginRenderPass,
             "invalid Vulkan depth attachment");
        return false;
      }
    }
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (usesSurface ? (!acquireSwapchainImage() ||
                       !beginSwapchainCommands(commandBuffer))
                    : !beginCommands(commandBuffer))
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::BeginRenderPass,
           "failed to begin a Vulkan render command buffer");
      return false;
    }
    if (usesSurface)
      transitionSwapchainImage(commandBuffer, mSwapchainImage,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    else
      for (std::uint32_t index = 0; index < desc.colorCount; ++index)
        transitionImage(commandBuffer, *colors[index],
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (depth)
      transitionImage(commandBuffer, *depth,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo colorAttachments[RenderPassDesc::MaxColorAttachments] = {};
    for (std::uint32_t index = 0; index < desc.colorCount; ++index)
    {
      VkRenderingAttachmentInfo &attachment = colorAttachments[index];
      attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      attachment.imageView = usesSurface ? mSwapchainViews[mSwapchainImage]
                                         : colors[index]->view;
      attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      attachment.loadOp = desc.colors[index].loadOp == LoadOp::Clear
                              ? VK_ATTACHMENT_LOAD_OP_CLEAR
                              : desc.colors[index].loadOp == LoadOp::DontCare
                                    ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                    : VK_ATTACHMENT_LOAD_OP_LOAD;
      attachment.storeOp = desc.colors[index].storeOp == StoreOp::Discard
                               ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                               : VK_ATTACHMENT_STORE_OP_STORE;
      for (std::uint32_t channel = 0; channel < 4; ++channel)
        attachment.clearValue.color.float32[channel] =
            desc.colors[index].clearColor[channel];
    }
    VkRenderingAttachmentInfo depthAttachment = {};
    if (depth)
    {
      depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      depthAttachment.imageView = depth->view;
      depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      depthAttachment.loadOp = desc.depthStencil.depthLoadOp == LoadOp::Clear
                                   ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                   : desc.depthStencil.depthLoadOp == LoadOp::DontCare
                                         ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                         : VK_ATTACHMENT_LOAD_OP_LOAD;
      depthAttachment.storeOp = desc.depthStencil.depthStoreOp == StoreOp::Discard
                                    ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                    : VK_ATTACHMENT_STORE_OP_STORE;
      depthAttachment.clearValue.depthStencil.depth = desc.depthStencil.clearDepth;
      depthAttachment.clearValue.depthStencil.stencil = desc.depthStencil.clearStencil;
    }
    VkRenderingAttachmentInfo stencilAttachment = depthAttachment;
    if (depth && depth->format == Format::Depth24Stencil8)
    {
      stencilAttachment.loadOp = desc.depthStencil.stencilLoadOp == LoadOp::Clear
                                     ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                     : desc.depthStencil.stencilLoadOp == LoadOp::DontCare
                                           ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                           : VK_ATTACHMENT_LOAD_OP_LOAD;
      stencilAttachment.storeOp = desc.depthStencil.stencilStoreOp == StoreOp::Discard
                                      ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                      : VK_ATTACHMENT_STORE_OP_STORE;
    }
    VkRenderingInfo rendering = {};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    // depth is guaranteed non-null here whenever color is null: the entry
    // guard above only lets colorCount == 0 through alongside a depth
    // attachment.
    rendering.renderArea.extent = {
        usesSurface ? mWidth : (color ? color->width : depth->width),
        usesSurface ? mHeight : (color ? color->height : depth->height)};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = desc.colorCount;
    rendering.pColorAttachments = colorAttachments;
    rendering.pDepthAttachment = depth ? &depthAttachment : nullptr;
    rendering.pStencilAttachment = depth && depth->format == Format::Depth24Stencil8
                                       ? &stencilAttachment
                                       : nullptr;
    vkCmdBeginRendering(commandBuffer, &rendering);
    mActiveCommandBuffer = commandBuffer;
    for (std::uint32_t index = 0; index < desc.colorCount; ++index)
      mPassColors[index] = usesSurface ? TextureHandle()
                                       : desc.colors[index].target.texture;
    mPassDepth = depth ? desc.depthStencil.target.texture : TextureHandle();
    mPassColorCount = desc.colorCount;
    mPassWidth = usesSurface ? mWidth : (color ? color->width : depth->width);
    mPassHeight = usesSurface ? mHeight : (color ? color->height : depth->height);
    mActivePipeline = PipelineHandle();
    mIndexBuffer = BufferHandle();
    mDescriptorsBoundInPass = false;
    // Texture bindings are deliberately global/sticky (GL-like): they
    // survive across passes so setPipeline() can reapply them without the
    // caller re-binding on every single pass - existing tests rely on
    // exactly that. The one case that has to be forced out is a texture
    // that was validly sampled in some earlier pass but is *this* pass's
    // own attachment: reapplying it mid-write would try to sample a
    // texture currently in DEPTH_STENCIL/COLOR_ATTACHMENT_OPTIMAL layout,
    // not SHADER_READ_ONLY_OPTIMAL, and bindTexture() would reject it (as
    // it should - that's a real conflict, not a stale-state accident).
    for (TextureBinding &binding : mTextureBindings)
    {
      const bool isOwnColorAttachment = [&] {
        for (std::uint32_t index = 0; index < desc.colorCount; ++index)
          if (!usesSurface && binding.texture == desc.colors[index].target.texture)
            return true;
        return false;
      }();
      if (isOwnColorAttachment ||
          (depth && binding.texture == desc.depthStencil.target.texture))
        binding = TextureBinding{};
    }
    mInRenderPass = true;
    mPassUsesSurface = usesSurface;
    return true;
  }

  void VulkanDevice::endRenderPass()
  {
    if (!mInRenderPass)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::BeginRenderPass,
           "there is no active Vulkan render pass");
      return;
    }
    if (QueryObject *query = mQueries.find(mActiveQuery))
    {
      vkCmdEndQuery(mActiveCommandBuffer, query->pool, 0);
      query->active = false;
      query->written = true;
      mActiveQuery = QueryHandle();
      push(GPUErrorCode::InvalidArgument, GPUOperation::CreateQuery,
           "the active Vulkan occlusion query was ended with the render pass");
    }
    vkCmdEndRendering(mActiveCommandBuffer);
    if (mPassUsesSurface)
    {
      transitionSwapchainImage(mActiveCommandBuffer, mSwapchainImage,
                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    else for (std::uint32_t index = 0; index < mPassColorCount; ++index)
    {
      TextureObject *texture = mTextures.find(mPassColors[index]);
      if (texture)
        transitionImage(mActiveCommandBuffer, *texture,
                        texture->usage & TextureUsageSampled
                            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_GENERAL);
    }
    TextureObject *depth = mTextures.find(mPassDepth);
    if (depth)
      // Same rule as the color attachments just above: a shadow map (or
      // any other depth texture read back as a sampled texture, e.g. for
      // depth-based post effects) needs SHADER_READ_ONLY_OPTIMAL, not
      // GENERAL, or bindTexture() rejects it afterwards.
      transitionImage(mActiveCommandBuffer, *depth,
                      depth->usage & TextureUsageSampled
                          ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                          : VK_IMAGE_LAYOUT_GENERAL);
    const bool submitted = mPassUsesSurface
                               ? submitSwapchainCommands(mActiveCommandBuffer)
                               : submitCommands(mActiveCommandBuffer);
    if (!mPassUsesSurface)
      for (VkDescriptorPool pool : mDescriptorPools)
        vkResetDescriptorPool(mDevice, pool, 0);
    mActiveCommandBuffer = VK_NULL_HANDLE;
    mActivePipeline = PipelineHandle();
    mIndexBuffer = BufferHandle();
    mPassColorCount = 0;
    mPassDepth = TextureHandle();
    mPassWidth = 0;
    mPassHeight = 0;
    mInRenderPass = false;
    mPassUsesSurface = false;
    if (!submitted)
      push(GPUErrorCode::BackendFailure, GPUOperation::BeginRenderPass,
           "failed to submit a Vulkan render pass");
  }

  bool VulkanDevice::setPipeline(PipelineHandle handle)
  {
    PipelineObject *pipeline = mPipelines.find(handle);
    if (pipeline && pipeline->isCompute)
    {
      // Compute has no render pass to validate against, and no command
      // buffer to bind into yet: dispatch() opens one, binds the pipeline
      // and the recorded storage bindings, and submits it in one shot.
      if (mInRenderPass)
      {
        push(GPUErrorCode::InvalidArgument, GPUOperation::SetPipeline,
             "a Vulkan compute pipeline cannot be bound inside a render pass");
        return false;
      }
      mActivePipeline = handle;
      return true;
    }
    TextureObject *color = mPassColorCount ? mTextures.find(mPassColors[0]) : nullptr;
    TextureObject *depth = mTextures.find(mPassDepth);
    // mPassColorCount == 0 is legitimate for a depth/stencil-only pass
    // (see beginRenderPass): color staying null then is expected, not the
    // "offscreen pass lost its color attachment" error this originally
    // guarded against.
    if (!mInRenderPass || !pipeline ||
        (!mPassUsesSurface && mPassColorCount > 0 && !color) ||
        pipeline->colorTargetCount != mPassColorCount ||
        pipeline->depthFormat != (depth ? vulkanFormat(depth->format)
                                        : VK_FORMAT_UNDEFINED) ||
        (mPassUsesSurface && pipeline->colorFormats[0] != mSwapchainFormat))
    {
      push(!pipeline ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::SetPipeline, "invalid Vulkan pipeline for the render pass");
      return false;
    }
    for (std::uint32_t index = 0; !mPassUsesSurface && index < mPassColorCount;
         ++index)
    {
      TextureObject *attachment = mTextures.find(mPassColors[index]);
      if (!attachment || pipeline->colorFormats[index] !=
                             vulkanFormat(attachment->format))
      {
        push(GPUErrorCode::InvalidArgument, GPUOperation::SetPipeline,
             "the Vulkan pipeline color formats do not match the render pass");
        return false;
      }
    }
    vkCmdBindPipeline(mActiveCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline->pipeline);
    VkViewport viewport = {};
    viewport.width = static_cast<float>(mPassWidth);
    viewport.y = static_cast<float>(mPassHeight);
    viewport.height = -static_cast<float>(mPassHeight);
    viewport.maxDepth = 1.0f;
    VkRect2D scissor = {};
    scissor.extent = {mPassWidth, mPassHeight};
    vkCmdSetViewport(mActiveCommandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(mActiveCommandBuffer, 0, 1, &scissor);
    if (pipeline->stencilEnabled)
      vkCmdSetStencilReference(mActiveCommandBuffer,
                               VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    // Unlike the uniform/texture sets below, the bindless set's contents
    // don't change per pipeline switch - registerBindlessTexture() writes
    // it directly - so this only needs to (re)associate it with this
    // pipeline's layout, not rewrite any descriptors.
    if (mCapabilities.bindlessTextures)
      vkCmdBindDescriptorSets(mActiveCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->layout, 4, 1, &mBindlessTextureSet, 0, nullptr);
    mActivePipeline = handle;
    // Every graphics pipeline is built from the exact same
    // VkDescriptorSetLayout objects for sets 0-3 (and 4, when bindless) -
    // see createPipeline() - so per the Vulkan pipeline-layout-
    // compatibility rule, vkCmdBindPipeline() above never invalidates
    // descriptor sets already bound in this command buffer. The sticky
    // uniform/texture bindings only need replaying once per fresh command
    // buffer (right after beginRenderPass()), not on every pipeline switch
    // within the same render pass.
    if (!mDescriptorsBoundInPass)
    {
      for (std::uint32_t index = 0; index < MaxUniformBindings; ++index)
      {
        const UniformBinding &binding = mUniformBindings[index];
        if (binding.handle.valid() &&
            !bindUniformBuffer(index, binding.handle, binding.offset, binding.size))
        {
          mActivePipeline = PipelineHandle();
          return false;
        }
      }
      for (std::uint32_t index = 0; index < MaxTextureBindings; ++index)
      {
        const TextureBinding &binding = mTextureBindings[index];
        if (binding.texture.valid() && binding.sampler.valid() &&
            !bindTexture(index, binding.texture, binding.sampler))
        {
          mActivePipeline = PipelineHandle();
          return false;
        }
      }
      mDescriptorsBoundInPass = true;
    }
    return true;
  }

  bool VulkanDevice::setViewport(const Viewport &viewport)
  {
    if (!mInRenderPass || !mActivePipeline.valid() || viewport.width <= 0.0f ||
        viewport.height <= 0.0f || viewport.minDepth < 0.0f ||
        viewport.maxDepth > 1.0f || viewport.minDepth > viewport.maxDepth)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::SetPipeline,
           "invalid Vulkan viewport");
      return false;
    }
    VkViewport native = {};
    native.x = viewport.x;
    native.y = static_cast<float>(mPassHeight) - viewport.y;
    native.width = viewport.width;
    native.height = -viewport.height;
    native.minDepth = viewport.minDepth;
    native.maxDepth = viewport.maxDepth;
    vkCmdSetViewport(mActiveCommandBuffer, 0, 1, &native);
    return true;
  }

  bool VulkanDevice::setScissor(const Rect &rect)
  {
    if (!mInRenderPass || !mActivePipeline.valid() || rect.x < 0 || rect.y < 0 ||
        static_cast<std::uint32_t>(rect.x) >= mPassWidth ||
        static_cast<std::uint32_t>(rect.y) >= mPassHeight || rect.width == 0 ||
        rect.height == 0 ||
        rect.width > mPassWidth - static_cast<std::uint32_t>(rect.x) ||
        rect.height > mPassHeight - static_cast<std::uint32_t>(rect.y))
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::SetPipeline,
           "invalid Vulkan scissor");
      return false;
    }
    VkRect2D native = {};
    native.offset.x = rect.x;
    native.offset.y = static_cast<std::int32_t>(mPassHeight - rect.y - rect.height);
    native.extent = {rect.width, rect.height};
    vkCmdSetScissor(mActiveCommandBuffer, 0, 1, &native);
    return true;
  }

  bool VulkanDevice::setStencilReference(std::uint32_t reference)
  {
    PipelineObject *pipeline = mPipelines.find(mActivePipeline);
    if (!mInRenderPass || !pipeline || !pipeline->stencilEnabled)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::SetPipeline,
           "the active Vulkan pipeline does not use stencil");
      return false;
    }
    vkCmdSetStencilReference(mActiveCommandBuffer,
                             VK_STENCIL_FACE_FRONT_AND_BACK, reference);
    return true;
  }

  bool VulkanDevice::bindVertexBuffer(std::uint32_t slot, BufferHandle handle,
                                      std::uint64_t offset)
  {
    BufferObject *buffer = mBuffers.find(handle);
    PipelineObject *pipeline = mPipelines.find(mActivePipeline);
    if (!mInRenderPass || !pipeline || !buffer ||
        slot >= pipeline->vertexBufferCount || !(buffer->usage & BufferUsageVertex) ||
        offset >= buffer->size)
    {
      push(!buffer ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::BindResource, "invalid Vulkan vertex buffer binding");
      return false;
    }
    const VkDeviceSize nativeOffset = offset;
    vkCmdBindVertexBuffers(mActiveCommandBuffer, slot, 1, &buffer->buffer,
                           &nativeOffset);
    return true;
  }

  bool VulkanDevice::bindIndexBuffer(BufferHandle handle, IndexFormat format,
                                     std::uint64_t offset)
  {
    BufferObject *buffer = mBuffers.find(handle);
    if (!mInRenderPass || !mActivePipeline.valid() || !buffer ||
        !(buffer->usage & BufferUsageIndex) || offset >= buffer->size)
    {
      push(!buffer ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::BindResource, "invalid Vulkan index buffer binding");
      return false;
    }
    vkCmdBindIndexBuffer(mActiveCommandBuffer, buffer->buffer, offset,
                         format == IndexFormat::Uint16 ? VK_INDEX_TYPE_UINT16
                                                       : VK_INDEX_TYPE_UINT32);
    mIndexBuffer = handle;
    mIndexFormat = format;
    return true;
  }

  bool VulkanDevice::bindUniformBuffer(std::uint32_t slot, BufferHandle handle,
                                       std::uint64_t offset, std::uint64_t size)
  {
    BufferObject *buffer = mBuffers.find(handle);
    PipelineObject *pipeline = mPipelines.find(mActivePipeline);
    if (!mInRenderPass || !pipeline || !buffer || slot >= MaxUniformBindings ||
        !(buffer->usage & BufferUsageUniform) || size == 0 ||
        offset > buffer->size || size > buffer->size - offset ||
        size > mCapabilities.maxUniformBufferSize ||
        offset % mCapabilities.uniformBufferOffsetAlignment != 0)
    {
      push(!buffer ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::BindResource, "invalid Vulkan uniform buffer binding");
      return false;
    }
    const UniformBinding previous = mUniformBindings[slot];
    mUniformBindings[slot].handle = handle;
    mUniformBindings[slot].offset = offset;
    mUniformBindings[slot].size = size;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!allocateDescriptorSet(mUniformSetLayout, set))
    {
      mUniformBindings[slot] = previous;
      push(GPUErrorCode::OutOfMemory, GPUOperation::BindResource,
           "failed to allocate a Vulkan uniform descriptor set");
      return false;
    }
    VkDescriptorBufferInfo infos[MaxUniformBindings] = {};
    VkWriteDescriptorSet writes[MaxUniformBindings] = {};
    std::uint32_t writeCount = 0;
    for (std::uint32_t index = 0; index < MaxUniformBindings; ++index)
    {
      const UniformBinding &binding = mUniformBindings[index];
      BufferObject *bound = mBuffers.find(binding.handle);
      if (!bound)
        continue;
      infos[writeCount].buffer = bound->buffer;
      infos[writeCount].offset = binding.offset;
      infos[writeCount].range = binding.size;
      writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[writeCount].dstSet = set;
      writes[writeCount].dstBinding = index;
      writes[writeCount].descriptorCount = 1;
      writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      writes[writeCount].pBufferInfo = &infos[writeCount];
      ++writeCount;
    }
    vkUpdateDescriptorSets(mDevice, writeCount, writes, 0, nullptr);
    vkCmdBindDescriptorSets(mActiveCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->layout, 0, 1, &set, 0, nullptr);
    return true;
  }

  std::int32_t VulkanDevice::uniformBlockSlot(PipelineHandle handle, const char *name)
  {
    PipelineObject *pipeline = mPipelines.find(handle);
    if (!pipeline || !name)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::BindResource,
           "uniform block lookup needs a valid pipeline");
      return -1;
    }
    for (std::uint32_t index = 0; index < pipeline->reflection.resourceCount; ++index)
    {
      const ShaderResource &resource = pipeline->reflection.resources[index];
      if (resource.type == ShaderResourceType::UniformBuffer &&
          std::strcmp(resource.name, name) == 0)
        return static_cast<std::int32_t>(resource.slot);
    }
    return -1;
  }

  std::int32_t VulkanDevice::textureSlot(PipelineHandle handle, const char *name)
  {
    PipelineObject *pipeline = mPipelines.find(handle);
    if (!pipeline || !name)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::BindResource,
           "texture slot lookup needs a valid pipeline");
      return -1;
    }
    for (std::uint32_t index = 0; index < pipeline->reflection.resourceCount; ++index)
    {
      const ShaderResource &resource = pipeline->reflection.resources[index];
      if (resource.type == ShaderResourceType::Sampler &&
          std::strcmp(resource.name, name) == 0)
        return static_cast<std::int32_t>(resource.slot);
    }
    return -1;
  }

  bool VulkanDevice::bindTexture(std::uint32_t slot, TextureHandle texture,
                                 SamplerHandle sampler)
  {
    TextureObject *image = mTextures.find(texture);
    SamplerObject *samplerObject = mSamplers.find(sampler);
    PipelineObject *pipeline = mPipelines.find(mActivePipeline);
    bool passAttachment = false;
    for (std::uint32_t index = 0; index < mPassColorCount; ++index)
      passAttachment = passAttachment || mPassColors[index] == texture;
    if (!mInRenderPass || !pipeline || !image || !samplerObject ||
        slot >= MaxTextureBindings || !image->view ||
        !(image->usage & TextureUsageSampled) || passAttachment ||
        image->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
      push((!image || !samplerObject) ? GPUErrorCode::InvalidHandle
                                      : GPUErrorCode::InvalidArgument,
           GPUOperation::BindResource, "invalid Vulkan texture binding");
      return false;
    }
    const TextureBinding previous = mTextureBindings[slot];
    mTextureBindings[slot].texture = texture;
    mTextureBindings[slot].sampler = sampler;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!allocateDescriptorSet(mTextureSetLayout, set))
    {
      mTextureBindings[slot] = previous;
      push(GPUErrorCode::OutOfMemory, GPUOperation::BindResource,
           "failed to allocate a Vulkan texture descriptor set");
      return false;
    }
    VkDescriptorImageInfo infos[MaxTextureBindings] = {};
    VkWriteDescriptorSet writes[MaxTextureBindings] = {};
    std::uint32_t writeCount = 0;
    for (std::uint32_t index = 0; index < MaxTextureBindings; ++index)
    {
      const TextureBinding &binding = mTextureBindings[index];
      TextureObject *boundImage = mTextures.find(binding.texture);
      SamplerObject *boundSampler = mSamplers.find(binding.sampler);
      if (!boundImage || !boundSampler)
        continue;
      if (boundImage->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        continue;
      infos[writeCount].sampler = boundSampler->sampler;
      infos[writeCount].imageView = boundImage->view;
      infos[writeCount].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[writeCount].dstSet = set;
      writes[writeCount].dstBinding = index;
      writes[writeCount].descriptorCount = 1;
      writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[writeCount].pImageInfo = &infos[writeCount];
      ++writeCount;
    }
    vkUpdateDescriptorSets(mDevice, writeCount, writes, 0, nullptr);
    vkCmdBindDescriptorSets(mActiveCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->layout, 1, 1, &set, 0, nullptr);
    return true;
  }

  std::int32_t VulkanDevice::registerBindlessTexture(TextureHandle texture,
                                                      SamplerHandle sampler)
  {
    TextureObject *image = mTextures.find(texture);
    SamplerObject *samplerObject = mSamplers.find(sampler);
    // Unlike bindTexture, this only ever writes the persistent bindless
    // set (vkUpdateDescriptorSets is a host-side operation) - no active
    // render pass or command buffer is required, so textures can be
    // registered once up front during setup.
    if (!mCapabilities.bindlessTextures || !image || !samplerObject || !image->view ||
        !(image->usage & TextureUsageSampled) ||
        image->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
      push((!image || !samplerObject) ? GPUErrorCode::InvalidHandle
                                      : GPUErrorCode::InvalidArgument,
           GPUOperation::BindResource, "invalid Vulkan bindless texture registration");
      return -1;
    }
    std::uint32_t slot;
    if (!mBindlessFreeList.empty())
    {
      slot = mBindlessFreeList.back();
      mBindlessFreeList.pop_back();
    }
    else
    {
      if (mBindlessNextSlot >= MaxBindlessTextures)
      {
        push(GPUErrorCode::OutOfMemory, GPUOperation::BindResource,
             "the Vulkan bindless texture table is full");
        return -1;
      }
      slot = mBindlessNextSlot++;
    }
    VkDescriptorImageInfo info = {};
    info.sampler = samplerObject->sampler;
    info.imageView = image->view;
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = mBindlessTextureSet;
    write.dstBinding = 0;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(mDevice, 1, &write, 0, nullptr);
    return static_cast<std::int32_t>(slot);
  }

  void VulkanDevice::unregisterBindlessTexture(std::int32_t index)
  {
    if (index < 0 || static_cast<std::uint32_t>(index) >= mBindlessNextSlot)
      return;
    mBindlessFreeList.push_back(static_cast<std::uint32_t>(index));
  }

  bool VulkanDevice::bindStorageBuffer(std::uint32_t slot, BufferHandle handle,
                                       std::uint64_t offset, std::uint64_t size)
  {
    BufferObject *buffer = mBuffers.find(handle);
    PipelineObject *pipeline = mPipelines.find(mActivePipeline);
    // Compute has no render pass; graphics (e.g. vertex pulling: reading
    // vertex data from an SSBO instead of fixed vertex input state) needs
    // one already open, same as bindUniformBuffer/bindTexture.
    const bool graphicsActive = pipeline && !pipeline->isCompute && mInRenderPass;
    const bool computeActive = pipeline && pipeline->isCompute && !mInRenderPass;
    if (!(graphicsActive || computeActive) || !buffer ||
        slot >= MaxStorageBufferBindings || !(buffer->usage & BufferUsageStorage) ||
        size == 0 || offset > buffer->size || size > buffer->size - offset ||
        offset % mCapabilities.storageBufferOffsetAlignment != 0)
    {
      push(!buffer ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::BindResource, "invalid Vulkan storage buffer binding");
      return false;
    }
    const StorageBufferBinding previous = mStorageBufferBindings[slot];
    mStorageBufferBindings[slot].handle = handle;
    mStorageBufferBindings[slot].offset = offset;
    mStorageBufferBindings[slot].size = size;
    if (!graphicsActive)
    {
      // Compute: recorded only, no open command buffer exists yet -
      // dispatch() writes and binds the descriptor set from this state
      // right before vkCmdDispatch.
      return true;
    }
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!allocateDescriptorSet(mStorageBufferSetLayout, set))
    {
      mStorageBufferBindings[slot] = previous;
      push(GPUErrorCode::OutOfMemory, GPUOperation::BindResource,
           "failed to allocate a Vulkan storage buffer descriptor set");
      return false;
    }
    VkDescriptorBufferInfo infos[MaxStorageBufferBindings] = {};
    VkWriteDescriptorSet writes[MaxStorageBufferBindings] = {};
    std::uint32_t writeCount = 0;
    for (std::uint32_t index = 0; index < MaxStorageBufferBindings; ++index)
    {
      const StorageBufferBinding &binding = mStorageBufferBindings[index];
      BufferObject *bound = mBuffers.find(binding.handle);
      if (!bound)
        continue;
      infos[writeCount].buffer = bound->buffer;
      infos[writeCount].offset = binding.offset;
      infos[writeCount].range = binding.size;
      writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[writeCount].dstSet = set;
      writes[writeCount].dstBinding = index;
      writes[writeCount].descriptorCount = 1;
      writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[writeCount].pBufferInfo = &infos[writeCount];
      ++writeCount;
    }
    vkUpdateDescriptorSets(mDevice, writeCount, writes, 0, nullptr);
    vkCmdBindDescriptorSets(mActiveCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->layout, 2, 1, &set, 0, nullptr);
    return true;
  }

  bool VulkanDevice::bindStorageTexture(std::uint32_t slot, TextureHandle texture,
                                        std::uint32_t mipLevel)
  {
    // Compute only, unlike bindStorageBuffer: a graphics-side storage
    // image would need its GENERAL-layout transition recorded immediately
    // here (compute's dispatch() does it right before the one dispatch
    // that will use it), and no sample has needed that yet - deliberately
    // narrower scope, not an oversight.
    TextureObject *image = mTextures.find(texture);
    PipelineObject *pipeline = mPipelines.find(mActivePipeline);
    if (mInRenderPass || !pipeline || !pipeline->isCompute || !image ||
        slot >= MaxStorageTextureBindings || !(image->usage & TextureUsageStorage) ||
        mipLevel >= image->mipCount)
    {
      push(!image ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::BindResource, "invalid Vulkan storage texture binding");
      return false;
    }
    mStorageTextureBindings[slot].texture = texture;
    mStorageTextureBindings[slot].mipLevel = mipLevel;
    return true;
  }

  bool VulkanDevice::memoryBarrier(std::uint32_t barriers)
  {
    if (!mAlive || !mCapabilities.memoryBarriers || barriers == 0)
    {
      push(GPUErrorCode::UnsupportedFeature, GPUOperation::Dispatch,
           "Vulkan memory barriers are unavailable on this device");
      return false;
    }
    // Outside a render pass, every dispatch() and copy in this backend
    // already submits through submitCommands(), which blocks on a fence
    // until the GPU is done - so by the time control returns here, every
    // write from prior work is already visible everywhere and there is no
    // in-flight work left to order against.
    //
    // Inside a render pass, though, draws accumulate into
    // mActiveCommandBuffer and are not submitted until endRenderPass() -
    // a storage write followed by this call followed by a storage read
    // within the same pass needs a real GPU-side barrier to be ordered
    // correctly, so record one into the still-open command buffer.
    if (mInRenderPass && mActiveCommandBuffer != VK_NULL_HANDLE)
    {
      VkAccessFlags2 access = 0;
      VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
      if (barriers & BarrierVertex)
      {
        access |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        stages |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
      }
      if (barriers & BarrierIndex)
      {
        access |= VK_ACCESS_2_INDEX_READ_BIT;
        stages |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
      }
      if (barriers & BarrierUniform)
      {
        access |= VK_ACCESS_2_UNIFORM_READ_BIT;
        stages |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
      }
      if (barriers & BarrierStorage)
      {
        access |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        stages |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
      }
      if (barriers & BarrierTexture)
      {
        access |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        stages |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
      }
      if (barriers & BarrierIndirect)
      {
        access |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        stages |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
      }
      if (barriers == BarrierAll)
      {
        access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
      }
      VkMemoryBarrier2 memoryBarrier2 = {};
      memoryBarrier2.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
      memoryBarrier2.srcStageMask = stages;
      memoryBarrier2.srcAccessMask = access;
      memoryBarrier2.dstStageMask = stages;
      memoryBarrier2.dstAccessMask = access;
      VkDependencyInfo dependency = {};
      dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
      dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
      dependency.memoryBarrierCount = 1;
      dependency.pMemoryBarriers = &memoryBarrier2;
      vkCmdPipelineBarrier2(mActiveCommandBuffer, &dependency);
    }
    return true;
  }

  bool VulkanDevice::dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
                              std::uint32_t groupCountZ)
  {
    PipelineObject *pipeline = mPipelines.find(mActivePipeline);
    if (mInRenderPass || !pipeline || !pipeline->isCompute || groupCountX == 0 ||
        groupCountY == 0 || groupCountZ == 0)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::Dispatch,
           "invalid Vulkan dispatch call");
      return false;
    }
    // Compute has no endRenderPass() to clear this on: dispatch() is a
    // one-shot, fully synchronous action (submitCommands() below blocks
    // until it is done), so the pipeline is no longer "in use" once it
    // returns either way. Clearing it here - not just on success - is what
    // lets destroy(PipelineHandle) accept it right afterwards.
    mActivePipeline = PipelineHandle();
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (!beginCommands(commandBuffer))
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::Dispatch,
           "failed to begin a Vulkan compute command buffer");
      return false;
    }
    for (StorageTextureBinding &binding : mStorageTextureBindings)
    {
      TextureObject *image = mTextures.find(binding.texture);
      if (image)
        transitionImageMip(commandBuffer, *image, binding.mipLevel,
                           VK_IMAGE_LAYOUT_GENERAL);
    }
    VkDescriptorSet bufferSet = VK_NULL_HANDLE;
    VkDescriptorSet imageSet = VK_NULL_HANDLE;
    if (!allocateDescriptorSet(mStorageBufferSetLayout, bufferSet) ||
        !allocateDescriptorSet(mStorageImageSetLayout, imageSet))
    {
      vkFreeCommandBuffers(mDevice, mCommandPool, 1, &commandBuffer);
      push(GPUErrorCode::OutOfMemory, GPUOperation::Dispatch,
           "failed to allocate Vulkan compute descriptor sets");
      return false;
    }
    VkDescriptorBufferInfo bufferInfos[MaxStorageBufferBindings] = {};
    VkWriteDescriptorSet bufferWrites[MaxStorageBufferBindings] = {};
    std::uint32_t bufferWriteCount = 0;
    for (std::uint32_t index = 0; index < MaxStorageBufferBindings; ++index)
    {
      const StorageBufferBinding &binding = mStorageBufferBindings[index];
      BufferObject *bound = mBuffers.find(binding.handle);
      if (!bound)
        continue;
      bufferInfos[bufferWriteCount].buffer = bound->buffer;
      bufferInfos[bufferWriteCount].offset = binding.offset;
      bufferInfos[bufferWriteCount].range = binding.size;
      bufferWrites[bufferWriteCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      bufferWrites[bufferWriteCount].dstSet = bufferSet;
      bufferWrites[bufferWriteCount].dstBinding = index;
      bufferWrites[bufferWriteCount].descriptorCount = 1;
      bufferWrites[bufferWriteCount].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bufferWrites[bufferWriteCount].pBufferInfo = &bufferInfos[bufferWriteCount];
      ++bufferWriteCount;
    }
    VkDescriptorImageInfo imageInfos[MaxStorageTextureBindings] = {};
    VkWriteDescriptorSet imageWrites[MaxStorageTextureBindings] = {};
    std::uint32_t imageWriteCount = 0;
    for (std::uint32_t index = 0; index < MaxStorageTextureBindings; ++index)
    {
      const StorageTextureBinding &binding = mStorageTextureBindings[index];
      TextureObject *bound = mTextures.find(binding.texture);
      if (!bound)
        continue;
      // A mip-restricted view (see TextureObject::storageViews): using the
      // full-range `view` here would make Vulkan validation require every
      // mip the view spans to be in GENERAL, not just the one this
      // binding's dispatch() transition below actually puts there.
      imageInfos[imageWriteCount].imageView = bound->storageViews[binding.mipLevel];
      imageInfos[imageWriteCount].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
      imageWrites[imageWriteCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      imageWrites[imageWriteCount].dstSet = imageSet;
      imageWrites[imageWriteCount].dstBinding = index;
      imageWrites[imageWriteCount].descriptorCount = 1;
      imageWrites[imageWriteCount].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      imageWrites[imageWriteCount].pImageInfo = &imageInfos[imageWriteCount];
      ++imageWriteCount;
    }
    vkUpdateDescriptorSets(mDevice, bufferWriteCount, bufferWrites, 0, nullptr);
    vkUpdateDescriptorSets(mDevice, imageWriteCount, imageWrites, 0, nullptr);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
    const VkDescriptorSet sets[] = {bufferSet, imageSet};
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline->layout, 0, 2, sets, 0, nullptr);
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
    // Mirrors updateTexture()/generateMipmaps(): a storage texture that is
    // also sampled (TextureUsageSampled) needs to leave this dispatch in
    // SHADER_READ_ONLY_OPTIMAL, not stay in GENERAL - otherwise a
    // following bindTexture() of the same texture (e.g. a compute shader
    // painting a texture a graphics pipeline samples the same frame)
    // would always fail its layout check.
    for (StorageTextureBinding &binding : mStorageTextureBindings)
    {
      TextureObject *image = mTextures.find(binding.texture);
      if (image)
        transitionImageMip(commandBuffer, *image, binding.mipLevel,
                           finalTextureLayout(image->usage));
    }
    if (!submitCommands(commandBuffer))
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::Dispatch,
           "failed to submit a Vulkan compute dispatch");
      return false;
    }
    return true;
  }

  bool VulkanDevice::draw(std::uint32_t vertexCount, std::uint32_t instanceCount,
                          std::uint32_t firstVertex, std::uint32_t firstInstance)
  {
    if (!mInRenderPass || !mActivePipeline.valid() || vertexCount == 0 ||
        instanceCount == 0)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::Draw,
           "invalid Vulkan draw call");
      return false;
    }
    vkCmdDraw(mActiveCommandBuffer, vertexCount, instanceCount, firstVertex,
              firstInstance);
    return true;
  }

  bool VulkanDevice::drawIndexed(std::uint32_t indexCount,
                                 std::uint32_t instanceCount,
                                 std::uint32_t firstIndex,
                                 std::int32_t baseVertex,
                                 std::uint32_t firstInstance)
  {
    if (!mInRenderPass || !mActivePipeline.valid() || !mIndexBuffer.valid() ||
        indexCount == 0 || instanceCount == 0)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::Draw,
           "invalid Vulkan indexed draw call");
      return false;
    }
    vkCmdDrawIndexed(mActiveCommandBuffer, indexCount, instanceCount, firstIndex,
                     baseVertex, firstInstance);
    return true;
  }

  bool VulkanDevice::drawIndirect(BufferHandle handle, std::uint64_t offset)
  {
    BufferObject *buffer = mBuffers.find(handle);
    if (!mInRenderPass || !mActivePipeline.valid() || !buffer ||
        !(buffer->usage & BufferUsageIndirect) || (offset % 4) != 0 ||
        offset > buffer->size ||
        buffer->size - offset < sizeof(DrawIndirectArgs))
    {
      push(!buffer ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::DrawIndirect, "invalid Vulkan indirect draw call");
      return false;
    }
    vkCmdDrawIndirect(mActiveCommandBuffer, buffer->buffer, offset, 1,
                      sizeof(DrawIndirectArgs));
    return true;
  }

  bool VulkanDevice::drawIndexedIndirect(BufferHandle handle,
                                         std::uint64_t offset)
  {
    BufferObject *buffer = mBuffers.find(handle);
    if (!mInRenderPass || !mActivePipeline.valid() || !mIndexBuffer.valid() ||
        !buffer || !(buffer->usage & BufferUsageIndirect) || (offset % 4) != 0 ||
        offset > buffer->size ||
        buffer->size - offset < sizeof(DrawIndexedIndirectArgs))
    {
      push(!buffer ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::DrawIndirect, "invalid Vulkan indexed indirect draw call");
      return false;
    }
    vkCmdDrawIndexedIndirect(mActiveCommandBuffer, buffer->buffer, offset, 1,
                             sizeof(DrawIndexedIndirectArgs));
    return true;
  }

  bool VulkanDevice::drawIndirectCount(BufferHandle handle,
                                       std::uint64_t offset,
                                       BufferHandle countHandle,
                                       std::uint64_t countOffset,
                                       std::uint32_t maxDrawCount,
                                       std::uint32_t stride)
  {
    if (!mCapabilities.indirectCount)
    {
      push(GPUErrorCode::UnsupportedFeature, GPUOperation::DrawIndirect,
           "Vulkan indirect draw count is unavailable on this device");
      return false;
    }
    BufferObject *buffer = mBuffers.find(handle);
    BufferObject *countBuffer = mBuffers.find(countHandle);
    const std::uint64_t lastEntryOffset = maxDrawCount == 0
                                              ? 0
                                              : static_cast<std::uint64_t>(maxDrawCount - 1) * stride;
    const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    const bool spanOverflows =
        offset > maximum - sizeof(DrawIndirectArgs) ||
        lastEntryOffset > maximum - offset - sizeof(DrawIndirectArgs);
    if (!mInRenderPass || !mActivePipeline.valid() || !buffer || !countBuffer ||
        !(buffer->usage & BufferUsageIndirect) ||
        !(countBuffer->usage & BufferUsageIndirect) || maxDrawCount == 0 ||
        stride < sizeof(DrawIndirectArgs) || (stride % 4) != 0 ||
        (offset % 4) != 0 || (countOffset % 4) != 0 || spanOverflows ||
        offset + lastEntryOffset + sizeof(DrawIndirectArgs) > buffer->size ||
        countOffset > countBuffer->size ||
        countBuffer->size - countOffset < sizeof(std::uint32_t))
    {
      push((!buffer || !countBuffer) ? GPUErrorCode::InvalidHandle
                                     : GPUErrorCode::InvalidArgument,
           GPUOperation::DrawIndirect, "invalid Vulkan indirect draw count call");
      return false;
    }
    vkCmdDrawIndirectCount(mActiveCommandBuffer, buffer->buffer, offset,
                           countBuffer->buffer, countOffset, maxDrawCount,
                           stride);
    return true;
  }

  bool VulkanDevice::drawIndexedIndirectCount(BufferHandle handle,
                                              std::uint64_t offset,
                                              BufferHandle countHandle,
                                              std::uint64_t countOffset,
                                              std::uint32_t maxDrawCount,
                                              std::uint32_t stride)
  {
    if (!mCapabilities.indirectCount)
    {
      push(GPUErrorCode::UnsupportedFeature, GPUOperation::DrawIndirect,
           "Vulkan indexed indirect draw count is unavailable on this device");
      return false;
    }
    BufferObject *buffer = mBuffers.find(handle);
    BufferObject *countBuffer = mBuffers.find(countHandle);
    const std::uint64_t lastEntryOffset = maxDrawCount == 0
                                              ? 0
                                              : static_cast<std::uint64_t>(maxDrawCount - 1) * stride;
    const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    const bool spanOverflows =
        offset > maximum - sizeof(DrawIndexedIndirectArgs) ||
        lastEntryOffset > maximum - offset - sizeof(DrawIndexedIndirectArgs);
    if (!mInRenderPass || !mActivePipeline.valid() || !mIndexBuffer.valid() ||
        !buffer || !countBuffer || !(buffer->usage & BufferUsageIndirect) ||
        !(countBuffer->usage & BufferUsageIndirect) || maxDrawCount == 0 ||
        stride < sizeof(DrawIndexedIndirectArgs) || (stride % 4) != 0 ||
        (offset % 4) != 0 || (countOffset % 4) != 0 || spanOverflows ||
        offset + lastEntryOffset + sizeof(DrawIndexedIndirectArgs) > buffer->size ||
        countOffset > countBuffer->size ||
        countBuffer->size - countOffset < sizeof(std::uint32_t))
    {
      push((!buffer || !countBuffer) ? GPUErrorCode::InvalidHandle
                                     : GPUErrorCode::InvalidArgument,
           GPUOperation::DrawIndirect,
           "invalid Vulkan indexed indirect draw count call");
      return false;
    }
    vkCmdDrawIndexedIndirectCount(mActiveCommandBuffer, buffer->buffer, offset,
                                  countBuffer->buffer, countOffset,
                                  maxDrawCount, stride);
    return true;
  }

  bool VulkanDevice::beginQuery(QueryHandle handle)
  {
    QueryObject *query = mQueries.find(handle);
    if (!mInRenderPass || !query || query->type != QueryType::Occlusion ||
        query->active || mActiveQuery.valid())
    {
      push(!query ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::CreateQuery, "invalid Vulkan occlusion query begin");
      return false;
    }
    vkCmdBeginQuery(mActiveCommandBuffer, query->pool, 0, 0);
    query->active = true;
    query->written = false;
    mActiveQuery = handle;
    return true;
  }

  void VulkanDevice::endQuery(QueryHandle handle)
  {
    QueryObject *query = mQueries.find(handle);
    if (!query || !query->active || mActiveQuery != handle)
    {
      push(!query ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::CreateQuery, "invalid Vulkan occlusion query end");
      return;
    }
    vkCmdEndQuery(mActiveCommandBuffer, query->pool, 0);
    query->active = false;
    query->written = true;
    mActiveQuery = QueryHandle();
  }

  bool VulkanDevice::writeTimestamp(QueryHandle handle)
  {
    QueryObject *query = mQueries.find(handle);
    if (!mAlive || mInRenderPass || !query || query->type != QueryType::Timestamp ||
        query->active)
    {
      push(!query ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::CreateQuery, "invalid Vulkan timestamp query");
      return false;
    }
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (!beginCommands(commandBuffer))
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::CreateQuery,
           "failed to begin a Vulkan timestamp query");
      return false;
    }
    vkCmdResetQueryPool(commandBuffer, query->pool, 0, 1);
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        query->pool, 0);
    if (!submitCommands(commandBuffer))
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::CreateQuery,
           "failed to submit a Vulkan timestamp query");
      return false;
    }
    query->written = true;
    return true;
  }

  bool VulkanDevice::isQueryResultAvailable(QueryHandle handle)
  {
    QueryObject *query = mQueries.find(handle);
    if (!query || !query->written || query->active)
    {
      push(!query ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::ReadBuffer, "the Vulkan query has no pending result");
      return false;
    }
    std::uint64_t result = 0;
    const VkResult status = vkGetQueryPoolResults(
        mDevice, query->pool, 0, 1, sizeof(result), &result, sizeof(result),
        VK_QUERY_RESULT_64_BIT);
    return status == VK_SUCCESS;
  }

  bool VulkanDevice::getQueryResult(QueryHandle handle, std::uint64_t &result)
  {
    QueryObject *query = mQueries.find(handle);
    if (!query || !query->written || query->active)
    {
      push(!query ? GPUErrorCode::InvalidHandle : GPUErrorCode::InvalidArgument,
           GPUOperation::ReadBuffer, "the Vulkan query has no pending result");
      return false;
    }
    if (vkGetQueryPoolResults(mDevice, query->pool, 0, 1, sizeof(result), &result,
                              sizeof(result), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
      return false;
    query->written = false;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (!beginCommands(commandBuffer))
      return false;
    vkCmdResetQueryPool(commandBuffer, query->pool, 0, 1);
    if (!submitCommands(commandBuffer))
    {
      push(GPUErrorCode::BackendFailure, GPUOperation::ReadBuffer,
           "failed to reset a Vulkan query pool");
      return false;
    }
    return true;
  }

  void VulkanDevice::shutdown()
  {
    if (mDevice)
      vkDeviceWaitIdle(mDevice);
    destroySwapchain();
    destroyBuffers();
    destroySamplers();
    destroyTextures();
    destroyPipelines();
    destroyQueries();
    destroyFences();
    destroyDescriptors();
    if (mInFlight)
      vkDestroyFence(mDevice, mInFlight, nullptr);
    if (mTransferFence)
      vkDestroyFence(mDevice, mTransferFence, nullptr);
    if (mRenderFinished)
      vkDestroySemaphore(mDevice, mRenderFinished, nullptr);
    if (mImageAvailable)
      vkDestroySemaphore(mDevice, mImageAvailable, nullptr);
    if (mCommandPool)
      vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
    if (mDevice)
      vkDestroyDevice(mDevice, nullptr);
    if (mVkSurface)
      vkDestroySurfaceKHR(mInstance, mVkSurface, nullptr);
    destroyDebugMessenger();
    if (mInstance)
      vkDestroyInstance(mInstance, nullptr);
    mInstance = VK_NULL_HANDLE;
    mVkSurface = VK_NULL_HANDLE;
    mPhysicalDevice = VK_NULL_HANDLE;
    mDevice = VK_NULL_HANDLE;
    mQueue = VK_NULL_HANDLE;
    mCommandPool = VK_NULL_HANDLE;
    mImageAvailable = VK_NULL_HANDLE;
    mRenderFinished = VK_NULL_HANDLE;
    mInFlight = VK_NULL_HANDLE;
    mTransferFence = VK_NULL_HANDLE;
    mSwapchainImage = UINT32_MAX;
    mSwapchainFormat = VK_FORMAT_UNDEFINED;
    mFramePendingPresent = false;
    mPassUsesSurface = false;
    mAlive = false;
    mSurfaceState = SurfaceState::Lost;
  }

  SurfaceState VulkanDevice::surfaceState() const { return mSurfaceState; }

  bool VulkanDevice::resizeSurface(std::uint32_t width, std::uint32_t height)
  {
    if (!mAlive || !mSurface || width == 0 || height == 0 || mInRenderPass ||
        mFramePendingPresent)
    {
      push(!mAlive ? GPUErrorCode::DeviceLost : GPUErrorCode::InvalidArgument,
           GPUOperation::Present, "the Vulkan surface cannot be resized now");
      return false;
    }
    mWidth = width;
    mHeight = height;
    return recreateSwapchain();
  }

  void VulkanDevice::suspendSurface()
  {
    if (!mAlive || !mSurface)
      return;
    if (mInRenderPass || mFramePendingPresent)
    {
      push(GPUErrorCode::InvalidArgument, GPUOperation::Present,
           "the Vulkan surface cannot be suspended during a frame");
      return;
    }
    vkDeviceWaitIdle(mDevice);
    destroySwapchain();
    mSurfaceState = SurfaceState::Suspended;
  }

  bool VulkanDevice::resumeSurface()
  {
    if (!mAlive || !mSurface || mInRenderPass || mFramePendingPresent)
    {
      push(!mAlive ? GPUErrorCode::DeviceLost : GPUErrorCode::SurfaceLost,
           GPUOperation::Present, "the Vulkan surface cannot be resumed now");
      return false;
    }
    return recreateSwapchain();
  }

  bool VulkanDevice::present()
  {
    if (!mAlive || !mSwapchain || mInRenderPass)
    {
      push(!mAlive ? GPUErrorCode::DeviceLost : GPUErrorCode::SurfaceLost,
           GPUOperation::Present,
           "the Vulkan surface is not ready for presentation");
      return false;
    }

    if (!mFramePendingPresent)
    {
      VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
      if (!acquireSwapchainImage() || !beginSwapchainCommands(commandBuffer))
      {
        push(GPUErrorCode::BackendFailure, GPUOperation::Present,
             "failed to begin a Vulkan presentation frame");
        return false;
      }
      transitionSwapchainImage(commandBuffer, mSwapchainImage,
                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
      if (!submitSwapchainCommands(commandBuffer))
      {
        push(GPUErrorCode::BackendFailure, GPUOperation::Present,
             "failed to submit a Vulkan presentation frame");
        return false;
      }
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &mRenderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &mSwapchain;
    presentInfo.pImageIndices = &mSwapchainImage;
    const VkResult present = vkQueuePresentKHR(mQueue, &presentInfo);
    mFramePendingPresent = false;
    mSwapchainImage = UINT32_MAX;
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR)
      return recreateSwapchain();
    if (present != VK_SUCCESS)
    {
      if (present == VK_ERROR_SURFACE_LOST_KHR)
        mSurfaceState = SurfaceState::Lost;
      push(GPUErrorCode::SurfaceLost, GPUOperation::Present,
           "failed to present the Vulkan swapchain image");
      return false;
    }
    return true;
  }

  void VulkanDevice::unsupported(GPUOperation operation)
  {
    push(GPUErrorCode::UnsupportedFeature, operation,
         "Vulkan operation is not implemented in this phase");
  }

  bool VulkanDevice::unsupportedBool(GPUOperation operation)
  {
    unsupported(operation);
    return false;
  }

  void VulkanDevice::push(GPUErrorCode code, GPUOperation operation,
                          const char *message, std::uint64_t value0,
                          std::uint64_t value1)
  {
    GPUError error;
    error.code = code;
    error.operation = operation;
    error.message = message;
    error.value0 = value0;
    error.value1 = value1;
    mErrors.push(error);
  }

  bool VulkanDevice::createInstance()
  {
    Vector<const char *> extensions;
    if (mSurface)
    {
      const char *const *surfaceExtensions = nullptr;
      std::uint32_t count = 0;
      if (!mSurface->requiredInstanceExtensions ||
          !mSurface->requiredInstanceExtensions(mSurface->userData,
                                                surfaceExtensions, count) ||
          !surfaceExtensions || count == 0)
      {
        push(GPUErrorCode::InvalidArgument, GPUOperation::CreateDevice,
             "the Vulkan surface did not provide instance extensions");
        return false;
      }
      extensions.assign(surfaceExtensions, surfaceExtensions + count);
    }

    // Opportunistically enable the Khronos validation layer: if it (and the
    // debug utils extension it reports through) is not installed, both
    // enumerations below come back empty and the instance is created
    // exactly as before. This gives every Vulkan API misuse a real
    // diagnostic surfaced through GPUErrorQueue instead of silent,
    // driver-dependent undefined behaviour.
    Vector<const char *> layers;
    std::uint32_t layerCount = 0;
    if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr) == VK_SUCCESS &&
        layerCount > 0)
    {
      Vector<VkLayerProperties> layerProperties(layerCount);
      if (vkEnumerateInstanceLayerProperties(&layerCount, layerProperties.data()) ==
          VK_SUCCESS)
      {
        for (const VkLayerProperties &layer : layerProperties)
        {
          if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
          {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            break;
          }
        }
      }
    }
    bool debugUtilsAvailable = false;
    if (!layers.empty())
    {
      std::uint32_t extensionCount = 0;
      if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr) ==
          VK_SUCCESS)
      {
        Vector<VkExtensionProperties> instanceExtensions(extensionCount);
        if (vkEnumerateInstanceExtensionProperties(
                nullptr, &extensionCount, instanceExtensions.data()) == VK_SUCCESS)
        {
          for (const VkExtensionProperties &extension : instanceExtensions)
          {
            if (std::strcmp(extension.extensionName,
                            VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
            {
              debugUtilsAvailable = true;
              break;
            }
          }
        }
      }
      if (debugUtilsAvailable)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      else
        layers.clear();
    }

    VkApplicationInfo applicationInfo = {};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "gpu";
    applicationInfo.applicationVersion = 1;
    applicationInfo.pEngineName = "gpu";
    applicationInfo.engineVersion = 1;
    applicationInfo.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
    createInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
    if (vkCreateInstance(&createInfo, nullptr, &mInstance) != VK_SUCCESS)
    {
      push(GPUErrorCode::DeviceCreationFailed, GPUOperation::CreateDevice,
           "failed to create a Vulkan instance");
      return false;
    }
    if (!layers.empty())
      createDebugMessenger();
    return true;
  }

  char *VulkanDevice::nextValidationDiagnostic()
  {
    char *slot = mValidationDiagnostic[mValidationDiagnosticSlot];
    mValidationDiagnosticSlot =
        (mValidationDiagnosticSlot + 1) % ValidationDiagnosticSlots;
    return slot;
  }

  VkBool32 VKAPI_PTR VulkanDevice::validationCallback(
      VkDebugUtilsMessageSeverityFlagBitsEXT severity,
      VkDebugUtilsMessageTypeFlagsEXT type,
      const VkDebugUtilsMessengerCallbackDataEXT *data, void *userData)
  {
    (void)type;
    VulkanDevice *device = static_cast<VulkanDevice *>(userData);
    if (!device || !data || !data->pMessage)
      return VK_FALSE;
    char *text = device->nextValidationDiagnostic();
    const std::size_t limit = ValidationDiagnosticSize - 1;
    std::size_t copied = std::strlen(data->pMessage);
    if (copied > limit)
      copied = limit;
    std::memcpy(text, data->pMessage, copied);
    text[copied] = '\0';
    GPUError error;
    error.code = GPUErrorCode::BackendFailure;
    error.operation = GPUOperation::None;
    error.value0 = static_cast<std::uint64_t>(data->messageIdNumber);
    error.message = text;
    error.severity = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                         ? GPUErrorSeverity::Error
                     : (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
                         ? GPUErrorSeverity::Warning
                         : GPUErrorSeverity::Info;
    device->mErrors.push(error);
    return VK_FALSE;
  }

  bool VulkanDevice::createDebugMessenger()
  {
    PFN_vkCreateDebugUtilsMessengerEXT create =
        reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(mInstance, "vkCreateDebugUtilsMessengerEXT"));
    if (!create)
      return false;
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = &VulkanDevice::validationCallback;
    createInfo.pUserData = this;
    return create(mInstance, &createInfo, nullptr, &mDebugMessenger) == VK_SUCCESS;
  }

  void VulkanDevice::destroyDebugMessenger()
  {
    if (!mDebugMessenger)
      return;
    PFN_vkDestroyDebugUtilsMessengerEXT destroy =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(mInstance, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy)
      destroy(mInstance, mDebugMessenger, nullptr);
    mDebugMessenger = VK_NULL_HANDLE;
  }

  bool VulkanDevice::createSurface()
  {
    if (!mSurface)
      return true;
    if (!mSurface->create ||
        !mSurface->create(mSurface->userData, mInstance, mVkSurface) ||
        !mVkSurface)
    {
      push(GPUErrorCode::SurfaceLost, GPUOperation::CreateDevice,
           "failed to create a Vulkan surface");
      return false;
    }
    return true;
  }

  bool VulkanDevice::selectPhysicalDevice()
  {
    std::uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(mInstance, &count, nullptr) != VK_SUCCESS ||
        count == 0)
    {
      push(GPUErrorCode::DeviceCreationFailed, GPUOperation::CreateDevice,
           "no Vulkan physical device is available");
      return false;
    }
    Vector<VkPhysicalDevice> devices(count);
    if (vkEnumeratePhysicalDevices(mInstance, &count, devices.data()) != VK_SUCCESS)
      return false;

    for (VkPhysicalDevice device : devices)
    {
      VkPhysicalDeviceProperties properties = {};
      vkGetPhysicalDeviceProperties(device, &properties);
      if (VK_API_VERSION_MINOR(properties.apiVersion) < 3 ||
          (mSurface && !hasDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME)))
        continue;
      VkPhysicalDeviceSynchronization2Features synchronization2 = {};
      synchronization2.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
      VkPhysicalDeviceVulkan12Features vulkan12 = {};
      vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
      synchronization2.pNext = &vulkan12;
      VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering = {};
      dynamicRendering.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
      dynamicRendering.pNext = &synchronization2;
      VkPhysicalDeviceFeatures2 features = {};
      features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
      features.pNext = &dynamicRendering;
      vkGetPhysicalDeviceFeatures2(device, &features);
      if (!synchronization2.synchronization2 || !dynamicRendering.dynamicRendering)
        continue;
      std::uint32_t queueCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
      Vector<VkQueueFamilyProperties> queues(queueCount);
      vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());
      for (std::uint32_t index = 0; index < queueCount; ++index)
      {
        VkBool32 present = VK_TRUE;
        if (mVkSurface &&
            vkGetPhysicalDeviceSurfaceSupportKHR(device, index, mVkSurface,
                                                 &present) != VK_SUCCESS)
          continue;
        if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
        {
          mPhysicalDevice = device;
          mQueueFamily = index;
          vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &mMemoryProperties);
          mCapabilities.compute = true;
          mCapabilities.storageBuffers = true;
          mCapabilities.storageTextures = true;
          mCapabilities.memoryBarriers = true;
          mCapabilities.indirectDraw = true;
          mCapabilities.indirectCount = vulkan12.drawIndirectCount;
          mCapabilities.asyncReadback = false;
          mCapabilities.depthReadback = false;
          mCapabilities.timestampQueries = queues[index].timestampValidBits != 0;
          mCapabilities.occlusionQueries = true;
          mCapabilities.textureArrays = false;
          mCapabilities.samplerBorderColor = true;
          mCapabilities.wireframe = false;
          mCapabilities.independentBlend = false;
          // Whether a non-zero firstInstance is honored in an *indirect*
          // draw (VkDrawIndirectCommand::firstInstance) - required for
          // multi-draw-indirect to select a per-draw transform/material
          // via gl_InstanceIndex without a separate gl_DrawID extension.
          mCapabilities.baseInstance = features.features.drawIndirectFirstInstance;
          mCapabilities.tessellationShader = features.features.tessellationShader;
          mCapabilities.geometryShader = features.features.geometryShader;
          mSamplerAnisotropySupported = features.features.samplerAnisotropy;
          mMaxSamplerAnisotropy = properties.limits.maxSamplerAnisotropy;
          // Descriptor indexing (Vulkan 1.2 core, no extension needed):
          // the four bits registerBindlessTexture()'s descriptor array
          // actually relies on - nonuniform indexing into a sampler
          // array, partially-bound/update-after-bind so slots can be
          // written incrementally, and a runtime (unsized) array in the
          // shader itself.
          mCapabilities.bindlessTextures =
              vulkan12.shaderSampledImageArrayNonUniformIndexing &&
              vulkan12.descriptorBindingPartiallyBound &&
              vulkan12.descriptorBindingSampledImageUpdateAfterBind &&
              vulkan12.runtimeDescriptorArray;
          mCapabilities.maxBindlessTextures =
              mCapabilities.bindlessTextures ? MaxBindlessTextures : 0;
          mCapabilities.maxColorAttachments = std::min(
              properties.limits.maxColorAttachments,
              static_cast<std::uint32_t>(PipelineDesc::MaxColorTargets));
          mCapabilities.maxTextureDimension2D = properties.limits.maxImageDimension2D;
          mCapabilities.maxTextureDimension3D = 0;
          mCapabilities.maxTextureArrayLayers = 0;
          mCapabilities.maxTextureBindings = MaxTextureBindings;
          mCapabilities.maxSampleCount = 1;
          mCapabilities.maxUniformBufferBindings = MaxUniformBindings;
          mCapabilities.maxUniformBufferSize = properties.limits.maxUniformBufferRange;
          mCapabilities.maxStorageBufferBindings = MaxStorageBufferBindings;
          mCapabilities.uniformBufferOffsetAlignment = static_cast<std::uint32_t>(
              properties.limits.minUniformBufferOffsetAlignment);
          mCapabilities.storageBufferOffsetAlignment = static_cast<std::uint32_t>(
              properties.limits.minStorageBufferOffsetAlignment);
          return true;
        }
      }
    }
    push(GPUErrorCode::UnsupportedFeature, GPUOperation::CreateDevice,
         "no Vulkan graphics queue supports the requested surface");
    return false;
  }

  bool VulkanDevice::createLogicalDevice()
  {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = mQueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceFeatures features = {};
    features.tessellationShader = mCapabilities.tessellationShader ? VK_TRUE : VK_FALSE;
    features.geometryShader = mCapabilities.geometryShader ? VK_TRUE : VK_FALSE;
    features.drawIndirectFirstInstance = mCapabilities.baseInstance ? VK_TRUE : VK_FALSE;
    features.samplerAnisotropy = mSamplerAnisotropySupported ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceSynchronization2Features synchronization2 = {};
    synchronization2.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    synchronization2.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features vulkan12 = {};
    vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12.drawIndirectCount = mCapabilities.indirectCount ? VK_TRUE : VK_FALSE;
    const VkBool32 enableBindless = mCapabilities.bindlessTextures ? VK_TRUE : VK_FALSE;
    vulkan12.shaderSampledImageArrayNonUniformIndexing = enableBindless;
    vulkan12.descriptorBindingPartiallyBound = enableBindless;
    vulkan12.descriptorBindingSampledImageUpdateAfterBind = enableBindless;
    vulkan12.runtimeDescriptorArray = enableBindless;
    synchronization2.pNext = &vulkan12;
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering = {};
    dynamicRendering.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRendering.dynamicRendering = VK_TRUE;
    dynamicRendering.pNext = &synchronization2;
    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.pEnabledFeatures = &features;
    createInfo.pNext = &dynamicRendering;
    if (mSurface)
    {
      createInfo.enabledExtensionCount = 1;
      createInfo.ppEnabledExtensionNames = extensions;
    }
    if (vkCreateDevice(mPhysicalDevice, &createInfo, nullptr, &mDevice) != VK_SUCCESS)
    {
      push(GPUErrorCode::DeviceCreationFailed, GPUOperation::CreateDevice,
           "failed to create a Vulkan logical device");
      return false;
    }
    vkGetDeviceQueue(mDevice, mQueueFamily, 0, &mQueue);
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = mQueueFamily;
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateCommandPool(mDevice, &poolInfo, nullptr, &mCommandPool) != VK_SUCCESS ||
        vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &mImageAvailable) != VK_SUCCESS ||
        vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &mRenderFinished) != VK_SUCCESS ||
        vkCreateFence(mDevice, &fenceInfo, nullptr, &mInFlight) != VK_SUCCESS ||
        !createDescriptors())
    {
      push(GPUErrorCode::DeviceCreationFailed, GPUOperation::CreateDevice,
           "failed to create Vulkan command synchronization objects");
      return false;
    }
    return true;
  }

  bool VulkanDevice::createSwapchain()
  {
    if (!mVkSurface)
      return true;
    if (mSurface->drawableSize)
      mSurface->drawableSize(mSurface->userData, mWidth, mHeight);
    if (mWidth == 0 || mHeight == 0)
    {
      mSurfaceState = SurfaceState::Suspended;
      return true;
    }
    VkSurfaceCapabilitiesKHR capabilities = {};
    std::uint32_t formatCount = 0;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mPhysicalDevice, mVkSurface,
                                                   &capabilities) != VK_SUCCESS ||
        vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDevice, mVkSurface,
                                             &formatCount, nullptr) != VK_SUCCESS ||
        formatCount == 0)
      return false;
    Vector<VkSurfaceFormatKHR> formats(formatCount);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDevice, mVkSurface,
                                             &formatCount, formats.data()) != VK_SUCCESS)
      return false;
    VkSurfaceFormatKHR format = {};
    if (!chooseSurfaceFormat(formats, vulkanFormat(mDesc.surface.format), format))
      return false;
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
      return false;
    const VkCompositeAlphaFlagBitsKHR compositeAlpha =
        chooseCompositeAlpha(capabilities.supportedCompositeAlpha);
    if (!compositeAlpha)
      return false;
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX)
    {
      extent.width = std::max(capabilities.minImageExtent.width,
                              std::min(capabilities.maxImageExtent.width, mWidth));
      extent.height = std::max(capabilities.minImageExtent.height,
                               std::min(capabilities.maxImageExtent.height, mHeight));
    }
    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && imageCount > capabilities.maxImageCount)
      imageCount = capabilities.maxImageCount;
    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = mVkSurface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = format.format;
    createInfo.imageColorSpace = format.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = compositeAlpha;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(mDevice, &createInfo, nullptr, &mSwapchain) != VK_SUCCESS)
      return false;
    if (vkGetSwapchainImagesKHR(mDevice, mSwapchain, &imageCount, nullptr) != VK_SUCCESS)
    {
      destroySwapchain();
      return false;
    }
    mSwapchainImages.resize(imageCount);
    if (vkGetSwapchainImagesKHR(mDevice, mSwapchain, &imageCount,
                                mSwapchainImages.data()) != VK_SUCCESS)
    {
      destroySwapchain();
      return false;
    }
    mSwapchainViews.resize(imageCount);
    for (std::uint32_t index = 0; index < imageCount; ++index)
    {
      VkImageViewCreateInfo viewInfo = {};
      viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewInfo.image = mSwapchainImages[index];
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = format.format;
      viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.subresourceRange.levelCount = 1;
      viewInfo.subresourceRange.layerCount = 1;
      if (vkCreateImageView(mDevice, &viewInfo, nullptr,
                            &mSwapchainViews[index]) != VK_SUCCESS)
      {
        destroySwapchain();
        return false;
      }
    }
    mSwapchainLayouts.assign(imageCount, VK_IMAGE_LAYOUT_UNDEFINED);
    mCommandBuffers.resize(imageCount);
    VkCommandBufferAllocateInfo allocationInfo = {};
    allocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocationInfo.commandPool = mCommandPool;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = imageCount;
    if (vkAllocateCommandBuffers(mDevice, &allocationInfo,
                                 mCommandBuffers.data()) != VK_SUCCESS)
    {
      destroySwapchain();
      return false;
    }
    mWidth = extent.width;
    mHeight = extent.height;
    mSwapchainFormat = format.format;
    mSwapchainImage = UINT32_MAX;
    mSurfaceState = SurfaceState::Ready;
    return true;
  }

  void VulkanDevice::destroySwapchain()
  {
    mFramePendingPresent = false;
    mSwapchainImage = UINT32_MAX;
    if (mDevice && mCommandPool && !mCommandBuffers.empty())
      vkFreeCommandBuffers(mDevice, mCommandPool,
                           static_cast<std::uint32_t>(mCommandBuffers.size()),
                           mCommandBuffers.data());
    mCommandBuffers.clear();
    for (VkImageView view : mSwapchainViews)
      if (view)
        vkDestroyImageView(mDevice, view, nullptr);
    mSwapchainViews.clear();
    mSwapchainImages.clear();
    mSwapchainLayouts.clear();
    if (mSwapchain)
      vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
    mSwapchain = VK_NULL_HANDLE;
    mSwapchainFormat = VK_FORMAT_UNDEFINED;
  }

  bool VulkanDevice::recreateSwapchain()
  {
    if (!mAlive || !mSurface)
      return false;
    vkDeviceWaitIdle(mDevice);
    destroySwapchain();
    if (!createSwapchain())
    {
      mSurfaceState = SurfaceState::Lost;
      push(GPUErrorCode::SurfaceLost, GPUOperation::Present,
           "failed to recreate the Vulkan swapchain");
      return false;
    }
    return mSurfaceState == SurfaceState::Ready;
  }

  void VulkanDevice::waitForGPU()
  {
    if (mDevice && mInFlight)
      vkWaitForFences(mDevice, 1, &mInFlight, VK_TRUE, UINT64_MAX);
  }

  bool VulkanDevice::acquireSwapchainImage()
  {
    if (!mSwapchain || !mInFlight || mFramePendingPresent ||
        vkWaitForFences(mDevice, 1, &mInFlight, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
      return false;
    for (VkDescriptorPool pool : mDescriptorPools)
      vkResetDescriptorPool(mDevice, pool, 0);
    const VkResult result = vkAcquireNextImageKHR(
        mDevice, mSwapchain, UINT64_MAX, mImageAvailable, VK_NULL_HANDLE,
        &mSwapchainImage);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
      return true;
    mSwapchainImage = UINT32_MAX;
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
      recreateSwapchain();
    else if (result == VK_ERROR_SURFACE_LOST_KHR)
      mSurfaceState = SurfaceState::Lost;
    return false;
  }

  bool VulkanDevice::beginSwapchainCommands(VkCommandBuffer &commandBuffer)
  {
    if (mSwapchainImage >= mCommandBuffers.size())
      return false;
    commandBuffer = mCommandBuffers[mSwapchainImage];
    if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS)
      return false;
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS;
  }

  bool VulkanDevice::submitSwapchainCommands(VkCommandBuffer commandBuffer)
  {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS ||
        vkResetFences(mDevice, 1, &mInFlight) != VK_SUCCESS)
      return false;
    VkSemaphoreSubmitInfo waitInfo = {};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = mImageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkCommandBufferSubmitInfo commandInfo = {};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = commandBuffer;
    VkSemaphoreSubmitInfo signalInfo = {};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = mRenderFinished;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;
    if (vkQueueSubmit2(mQueue, 1, &submitInfo, mInFlight) != VK_SUCCESS)
    {
      vkDestroyFence(mDevice, mInFlight, nullptr);
      mInFlight = VK_NULL_HANDLE;
      VkFenceCreateInfo fenceInfo = {};
      fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
      if (vkCreateFence(mDevice, &fenceInfo, nullptr, &mInFlight) != VK_SUCCESS)
        mSurfaceState = SurfaceState::Lost;
      return false;
    }
    mFramePendingPresent = true;
    return true;
  }

  void VulkanDevice::transitionSwapchainImage(VkCommandBuffer commandBuffer,
                                               std::uint32_t imageIndex,
                                               VkImageLayout newLayout)
  {
    const VkImageLayout oldLayout = mSwapchainLayouts[imageIndex];
    VkImageMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcStageMask = oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                               ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                               : VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                                : VK_ACCESS_2_NONE;
    barrier.dstStageMask = newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                               ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                               : VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                ? VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                                : VK_ACCESS_2_NONE;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = mSwapchainImages[imageIndex];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    VkDependencyInfo dependency = {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
    mSwapchainLayouts[imageIndex] = newLayout;
  }

  bool VulkanDevice::createDescriptors()
  {
    Vector<VkDescriptorSetLayoutBinding> uniformBindings(MaxUniformBindings);
    for (std::uint32_t index = 0; index < MaxUniformBindings; ++index)
    {
      uniformBindings[index].binding = index;
      uniformBindings[index].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      uniformBindings[index].descriptorCount = 1;
      // Also the tessellation/geometry stages: getTessLevel()-style logic
      // in a tessellation control shader, or a geometry shader, may need
      // the same per-frame data a vertex/fragment shader would.
      uniformBindings[index].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                        VK_SHADER_STAGE_FRAGMENT_BIT |
                                        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
                                        VK_SHADER_STAGE_GEOMETRY_BIT;
    }
    VkDescriptorSetLayoutCreateInfo uniformInfo = {};
    uniformInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    uniformInfo.bindingCount = MaxUniformBindings;
    uniformInfo.pBindings = uniformBindings.data();
    if (vkCreateDescriptorSetLayout(mDevice, &uniformInfo, nullptr,
                                    &mUniformSetLayout) != VK_SUCCESS)
      return false;
    Vector<VkDescriptorSetLayoutBinding> textureBindings(MaxTextureBindings);
    for (std::uint32_t index = 0; index < MaxTextureBindings; ++index)
    {
      textureBindings[index].binding = index;
      textureBindings[index].descriptorType =
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      textureBindings[index].descriptorCount = 1;
      textureBindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo textureInfo = {};
    textureInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    textureInfo.bindingCount = MaxTextureBindings;
    textureInfo.pBindings = textureBindings.data();
    if (vkCreateDescriptorSetLayout(mDevice, &textureInfo, nullptr,
                                    &mTextureSetLayout) != VK_SUCCESS)
    {
      vkDestroyDescriptorSetLayout(mDevice, mUniformSetLayout, nullptr);
      mUniformSetLayout = VK_NULL_HANDLE;
      return false;
    }
    Vector<VkDescriptorSetLayoutBinding> storageBufferBindings(MaxStorageBufferBindings);
    for (std::uint32_t index = 0; index < MaxStorageBufferBindings; ++index)
    {
      storageBufferBindings[index].binding = index;
      storageBufferBindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      storageBufferBindings[index].descriptorCount = 1;
      // Also VERTEX/FRAGMENT: this layout is shared verbatim between
      // compute pipelines (set 0) and graphics ones (set 2, for vertex
      // pulling and similar SSBO reads from a raster shader).
      storageBufferBindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT |
                                              VK_SHADER_STAGE_VERTEX_BIT |
                                              VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo storageBufferInfo = {};
    storageBufferInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    storageBufferInfo.bindingCount = MaxStorageBufferBindings;
    storageBufferInfo.pBindings = storageBufferBindings.data();
    if (vkCreateDescriptorSetLayout(mDevice, &storageBufferInfo, nullptr,
                                    &mStorageBufferSetLayout) != VK_SUCCESS)
    {
      vkDestroyDescriptorSetLayout(mDevice, mTextureSetLayout, nullptr);
      vkDestroyDescriptorSetLayout(mDevice, mUniformSetLayout, nullptr);
      mTextureSetLayout = VK_NULL_HANDLE;
      mUniformSetLayout = VK_NULL_HANDLE;
      return false;
    }
    Vector<VkDescriptorSetLayoutBinding> storageImageBindings(MaxStorageTextureBindings);
    for (std::uint32_t index = 0; index < MaxStorageTextureBindings; ++index)
    {
      storageImageBindings[index].binding = index;
      storageImageBindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      storageImageBindings[index].descriptorCount = 1;
      storageImageBindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo storageImageInfo = {};
    storageImageInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    storageImageInfo.bindingCount = MaxStorageTextureBindings;
    storageImageInfo.pBindings = storageImageBindings.data();
    if (vkCreateDescriptorSetLayout(mDevice, &storageImageInfo, nullptr,
                                    &mStorageImageSetLayout) != VK_SUCCESS)
    {
      vkDestroyDescriptorSetLayout(mDevice, mStorageBufferSetLayout, nullptr);
      vkDestroyDescriptorSetLayout(mDevice, mTextureSetLayout, nullptr);
      vkDestroyDescriptorSetLayout(mDevice, mUniformSetLayout, nullptr);
      mStorageBufferSetLayout = VK_NULL_HANDLE;
      mTextureSetLayout = VK_NULL_HANDLE;
      mUniformSetLayout = VK_NULL_HANDLE;
      return false;
    }
    if (mCapabilities.bindlessTextures && !createBindlessTextureSet())
    {
      vkDestroyDescriptorSetLayout(mDevice, mStorageImageSetLayout, nullptr);
      vkDestroyDescriptorSetLayout(mDevice, mStorageBufferSetLayout, nullptr);
      vkDestroyDescriptorSetLayout(mDevice, mTextureSetLayout, nullptr);
      vkDestroyDescriptorSetLayout(mDevice, mUniformSetLayout, nullptr);
      mStorageImageSetLayout = VK_NULL_HANDLE;
      mStorageBufferSetLayout = VK_NULL_HANDLE;
      mTextureSetLayout = VK_NULL_HANDLE;
      mUniformSetLayout = VK_NULL_HANDLE;
      return false;
    }
    return createDescriptorPool();
  }

  bool VulkanDevice::createBindlessTextureSet()
  {
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = MaxBindlessTextures;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkDescriptorBindingFlags bindingFlags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {};
    bindingFlagsInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount = 1;
    bindingFlagsInfo.pBindingFlags = &bindingFlags;
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &bindingFlagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr,
                                    &mBindlessTextureSetLayout) != VK_SUCCESS)
      return false;
    const VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                           MaxBindlessTextures};
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &mBindlessDescriptorPool) !=
        VK_SUCCESS)
    {
      vkDestroyDescriptorSetLayout(mDevice, mBindlessTextureSetLayout, nullptr);
      mBindlessTextureSetLayout = VK_NULL_HANDLE;
      return false;
    }
    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = mBindlessDescriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &mBindlessTextureSetLayout;
    if (vkAllocateDescriptorSets(mDevice, &allocateInfo, &mBindlessTextureSet) !=
        VK_SUCCESS)
    {
      vkDestroyDescriptorPool(mDevice, mBindlessDescriptorPool, nullptr);
      vkDestroyDescriptorSetLayout(mDevice, mBindlessTextureSetLayout, nullptr);
      mBindlessDescriptorPool = VK_NULL_HANDLE;
      mBindlessTextureSetLayout = VK_NULL_HANDLE;
      return false;
    }
    return true;
  }

  bool VulkanDevice::createDescriptorPool()
  {
    VkDescriptorPoolSize sizes[4] = {};
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = MaxUniformBindings * 128;
    sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = MaxTextureBindings * 128;
    sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sizes[2].descriptorCount = MaxStorageBufferBindings * 128;
    sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    sizes[3].descriptorCount = MaxStorageTextureBindings * 128;
    VkDescriptorPoolCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    createInfo.maxSets = 128;
    createInfo.poolSizeCount = 4;
    createInfo.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(mDevice, &createInfo, nullptr, &pool) != VK_SUCCESS)
      return false;
    mDescriptorPools.push_back(pool);
    return true;
  }

  void VulkanDevice::destroyDescriptors()
  {
    for (VkDescriptorPool pool : mDescriptorPools)
      vkDestroyDescriptorPool(mDevice, pool, nullptr);
    mDescriptorPools.clear();
    if (mStorageImageSetLayout)
      vkDestroyDescriptorSetLayout(mDevice, mStorageImageSetLayout, nullptr);
    if (mStorageBufferSetLayout)
      vkDestroyDescriptorSetLayout(mDevice, mStorageBufferSetLayout, nullptr);
    if (mTextureSetLayout)
      vkDestroyDescriptorSetLayout(mDevice, mTextureSetLayout, nullptr);
    if (mUniformSetLayout)
      vkDestroyDescriptorSetLayout(mDevice, mUniformSetLayout, nullptr);
    mStorageImageSetLayout = VK_NULL_HANDLE;
    mStorageBufferSetLayout = VK_NULL_HANDLE;
    mTextureSetLayout = VK_NULL_HANDLE;
    mUniformSetLayout = VK_NULL_HANDLE;
    if (mBindlessDescriptorPool)
      vkDestroyDescriptorPool(mDevice, mBindlessDescriptorPool, nullptr);
    if (mBindlessTextureSetLayout)
      vkDestroyDescriptorSetLayout(mDevice, mBindlessTextureSetLayout, nullptr);
    mBindlessDescriptorPool = VK_NULL_HANDLE;
    mBindlessTextureSetLayout = VK_NULL_HANDLE;
    mBindlessTextureSet = VK_NULL_HANDLE;
    mBindlessFreeList.clear();
    mBindlessNextSlot = 0;
  }

  bool VulkanDevice::allocateDescriptorSet(VkDescriptorSetLayout layout,
                                           VkDescriptorSet &set)
  {
    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &layout;
    for (VkDescriptorPool pool : mDescriptorPools)
    {
      allocateInfo.descriptorPool = pool;
      if (vkAllocateDescriptorSets(mDevice, &allocateInfo, &set) == VK_SUCCESS)
        return true;
    }
    if (!createDescriptorPool())
      return false;
    allocateInfo.descriptorPool = mDescriptorPools.back();
    return vkAllocateDescriptorSets(mDevice, &allocateInfo, &set) == VK_SUCCESS;
  }

  bool VulkanDevice::beginCommands(VkCommandBuffer &commandBuffer)
  {
    VkCommandBufferAllocateInfo allocation = {};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = mCommandPool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(mDevice, &allocation, &commandBuffer) != VK_SUCCESS)
      return false;
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &begin) == VK_SUCCESS)
      return true;
    vkFreeCommandBuffers(mDevice, mCommandPool, 1, &commandBuffer);
    commandBuffer = VK_NULL_HANDLE;
    return false;
  }

  bool VulkanDevice::submitCommands(VkCommandBuffer commandBuffer)
  {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
      vkFreeCommandBuffers(mDevice, mCommandPool, 1, &commandBuffer);
      return false;
    }
    // This function always waits for the fence below before returning, so
    // it never has two submissions in flight against it at once - a single
    // fence reused (and reset) across calls is safe, and avoids a
    // vkCreateFence/vkDestroyFence round trip on every one-off upload,
    // copy, mip generation and query reset.
    if (mTransferFence == VK_NULL_HANDLE)
    {
      VkFenceCreateInfo fenceInfo = {};
      fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      if (vkCreateFence(mDevice, &fenceInfo, nullptr, &mTransferFence) != VK_SUCCESS)
      {
        vkFreeCommandBuffers(mDevice, mCommandPool, 1, &commandBuffer);
        return false;
      }
    }
    else
    {
      vkResetFences(mDevice, 1, &mTransferFence);
    }
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    const bool submitted =
        vkQueueSubmit(mQueue, 1, &submit, mTransferFence) == VK_SUCCESS &&
        vkWaitForFences(mDevice, 1, &mTransferFence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    vkFreeCommandBuffers(mDevice, mCommandPool, 1, &commandBuffer);
    return submitted;
  }

  bool VulkanDevice::createTransferBuffer(VkDeviceSize size, VkBuffer &buffer,
                                          VkDeviceMemory &memory, void *&mapped)
  {
    VkBufferCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.size = size;
    createInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(mDevice, &createInfo, nullptr, &buffer) != VK_SUCCESS)
      return false;
    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(mDevice, buffer, &requirements);
    const std::uint32_t memoryType = findMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memoryType == UINT32_MAX)
    {
      vkDestroyBuffer(mDevice, buffer, nullptr);
      buffer = VK_NULL_HANDLE;
      return false;
    }
    VkMemoryAllocateInfo allocation = {};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(mDevice, &allocation, nullptr, &memory) != VK_SUCCESS ||
        vkBindBufferMemory(mDevice, buffer, memory, 0) != VK_SUCCESS ||
        vkMapMemory(mDevice, memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
    {
      if (memory)
        vkFreeMemory(mDevice, memory, nullptr);
      vkDestroyBuffer(mDevice, buffer, nullptr);
      buffer = VK_NULL_HANDLE;
      memory = VK_NULL_HANDLE;
      mapped = nullptr;
      return false;
    }
    return true;
  }

  void VulkanDevice::transitionImage(VkCommandBuffer commandBuffer,
                                     TextureObject &texture,
                                     VkImageLayout newLayout)
  {
    transitionImageMip(commandBuffer, texture, 0, newLayout);
  }

  void VulkanDevice::transitionImageMip(VkCommandBuffer commandBuffer,
                                        TextureObject &texture,
                                        std::uint32_t mipLevel,
                                        VkImageLayout newLayout)
  {
    VkImageLayout &currentLayout = texture.mipLayouts[mipLevel];
    if (currentLayout == newLayout)
      return;
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = currentLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image;
    barrier.subresourceRange.aspectMask = texture.aspectMask;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = texture.layerCount;
    const VkPipelineStageFlags sourceStage =
        currentLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                    : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = currentLayout == VK_IMAGE_LAYOUT_UNDEFINED
                                ? 0
                                : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, sourceStage, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    currentLayout = newLayout;
    if (mipLevel == 0)
      texture.layout = newLayout;
  }

  std::uint32_t VulkanDevice::findMemoryType(
      std::uint32_t typeBits, VkMemoryPropertyFlags properties) const
  {
    for (std::uint32_t index = 0; index < mMemoryProperties.memoryTypeCount;
         ++index)
    {
      if ((typeBits & (1u << index)) &&
          (mMemoryProperties.memoryTypes[index].propertyFlags & properties) ==
              properties)
        return index;
    }
    return UINT32_MAX;
  }

  void VulkanDevice::destroyBuffers()
  {
    mBuffers.forEach([this](BufferObject &object) {
      if (object.mapped)
        vkUnmapMemory(mDevice, object.memory);
      if (object.buffer)
        vkDestroyBuffer(mDevice, object.buffer, nullptr);
      if (object.memory)
        vkFreeMemory(mDevice, object.memory, nullptr);
    });
    mBuffers.clear();
  }

  void VulkanDevice::destroySamplers()
  {
    mSamplers.forEach([this](SamplerObject &object) {
      if (object.sampler)
        vkDestroySampler(mDevice, object.sampler, nullptr);
    });
    mSamplers.clear();
  }

  void VulkanDevice::destroyTextures()
  {
    mTextures.forEach([this](TextureObject &object) {
      if (object.view)
        vkDestroyImageView(mDevice, object.view, nullptr);
      for (VkImageView storageView : object.storageViews)
        if (storageView)
          vkDestroyImageView(mDevice, storageView, nullptr);
      if (object.image)
        vkDestroyImage(mDevice, object.image, nullptr);
      if (object.memory)
        vkFreeMemory(mDevice, object.memory, nullptr);
    });
    mTextures.clear();
  }

  void VulkanDevice::destroyPipelines()
  {
    mPipelines.forEach([this](PipelineObject &object) {
      if (object.pipeline)
        vkDestroyPipeline(mDevice, object.pipeline, nullptr);
      if (object.layout)
        vkDestroyPipelineLayout(mDevice, object.layout, nullptr);
    });
    mPipelines.clear();
  }

  void VulkanDevice::destroyQueries()
  {
    mQueries.forEach([this](QueryObject &object) {
      if (object.pool)
        vkDestroyQueryPool(mDevice, object.pool, nullptr);
    });
    mQueries.clear();
    mActiveQuery = QueryHandle();
  }

  void VulkanDevice::destroyFences()
  {
    mFences.forEach([this](FenceObject &object) {
      if (object.fence)
        vkDestroyFence(mDevice, object.fence, nullptr);
    });
    mFences.clear();
  }

  FenceHandle VulkanDevice::insertFence()
  {
    // Unlike GL/Null, this is not gated on mCapabilities.asyncReadback:
    // that flag tracks whether readTexture()/readBuffer() can complete
    // without blocking (they can't yet on this backend - see
    // createTransferBuffer()/submitCommands()), which is a separate
    // question from whether the device can hand out a GPU-side fence.
    // VkFence is a core Vulkan primitive with no capability bit of its
    // own, so it is always available once the device is alive.
    if (!mAlive)
    {
      push(GPUErrorCode::DeviceLost, GPUOperation::CreateFence,
           "device is shut down");
      return FenceHandle();
    }
    FenceObject object;
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(mDevice, &fenceInfo, nullptr, &object.fence) != VK_SUCCESS)
    {
      push(GPUErrorCode::OutOfMemory, GPUOperation::CreateFence,
           "Vulkan fence creation failed");
      return FenceHandle();
    }
    // Vulkan has no "insert a marker into the queue's timeline" primitive
    // the way glFenceSync() does - the equivalent is a submission with no
    // command buffers, signaling this fence once the queue reaches this
    // point. A single queue executes submissions in submission order, so
    // this fence only signals once every previously submitted batch has
    // completed.
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if (vkQueueSubmit(mQueue, 1, &submit, object.fence) != VK_SUCCESS)
    {
      vkDestroyFence(mDevice, object.fence, nullptr);
      push(GPUErrorCode::BackendFailure, GPUOperation::CreateFence,
           "failed to submit a Vulkan fence");
      return FenceHandle();
    }
    const FenceHandle handle = mFences.insert(object);
    if (!handle.valid())
      vkDestroyFence(mDevice, object.fence, nullptr);
    return handle;
  }

  bool VulkanDevice::isFenceSignaled(FenceHandle handle)
  {
    FenceObject *fence = mFences.find(handle);
    if (!fence)
    {
      push(GPUErrorCode::InvalidHandle, GPUOperation::CreateFence,
           "invalid Vulkan fence handle");
      return false;
    }
    return vkGetFenceStatus(mDevice, fence->fence) == VK_SUCCESS;
  }

  void VulkanDevice::destroy(FenceHandle handle)
  {
    FenceObject *object = mFences.find(handle);
    if (!object)
    {
      push(GPUErrorCode::InvalidHandle, GPUOperation::Destroy,
           "invalid Vulkan fence handle");
      return;
    }
    vkDestroyFence(mDevice, object->fence, nullptr);
    mFences.erase(handle);
  }

  Device *createVulkanDevice(const DeviceDesc &desc, GPUError *error)
  {
    VulkanDevice *device = new VulkanDevice(desc);
    if (device->initialize(error))
      return device;
    delete device;
    return nullptr;
  }

} // namespace gpu
