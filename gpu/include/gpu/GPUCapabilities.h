#ifndef GPU_CAPABILITIES_H
#define GPU_CAPABILITIES_H

#include <cstdint>

namespace gpu
{

  /** @brief Features and implementation limits reported by a device. */
  struct GPUCapabilities
  {
    /** @brief Compute dispatch is available. */
    bool compute = false;
    /** @brief Storage/uniform-style buffer resources are available. */
    bool storageBuffers = false;
    /** @brief Storage texture resources are available. */
    bool storageTextures = false;
    /** @brief Explicit memory barriers are available. */
    bool memoryBarriers = false;
    /** @brief Indirect draw commands are available. */
    bool indirectDraw = false;
    /** @brief Indirect draw-count commands are available. */
    bool indirectCount = false;
    /** @brief Asynchronous texture readback is available. */
    bool asyncReadback = false;
    /** @brief Depth-texture readback is available. */
    bool depthReadback = false;
    /** @brief Timestamp queries are available. */
    bool timestampQueries = false;
    /** @brief Occlusion queries are available. */
    bool occlusionQueries = false;
    /** @brief Texture arrays are available. */
    bool textureArrays = false;
    /** @brief BC texture compression family is available. */
    bool textureCompressionBC = false;
    /** @brief BC1 texture compression is available. */
    bool textureCompressionBC1 = false;
    /** @brief BC3 texture compression is available. */
    bool textureCompressionBC3 = false;
    /** @brief BC5 texture compression is available. */
    bool textureCompressionBC5 = false;
    /** @brief BC7 texture compression is available. */
    bool textureCompressionBC7 = false;
    /** @brief ETC2 texture compression is available. */
    bool textureCompressionETC2 = false;
    /** @brief Anisotropic sampler filtering is available. */
    bool anisotropicFiltering = false;
    /** @brief Largest SamplerDesc::maxAnisotropy the device accepts; 1 when
     * anisotropic filtering is unavailable. Larger requests are clamped. */
    float maxAnisotropy = 1.0f;
    /** @brief ASTC texture compression is available. */
    bool textureCompressionASTC = false;
    /** @brief Sampler border colors are available. */
    bool samplerBorderColor = false;
    /** @brief Wireframe rasterization is available. */
    bool wireframe = false;
    /** @brief Independent color-target blending is available. */
    bool independentBlend = false;
    /** @brief Non-zero base-instance draw parameters are available. */
    bool baseInstance = false;
    /** @brief Tessellation shaders are available. */
    bool tessellationShader = false;
    /** @brief Geometry shaders are available. */
    bool geometryShader = false;
    /** @brief Bindless texture registration is available. */
    bool bindlessTextures = false;
    /** @brief Maximum number of bindless textures. */
    std::uint32_t maxBindlessTextures = 0;
    /** @brief Maximum number of color attachments. */
    std::uint32_t maxColorAttachments = 1;
    /** @brief Maximum 2D texture dimension. */
    std::uint32_t maxTextureDimension2D = 0;
    /** @brief Maximum 3D texture dimension. */
    std::uint32_t maxTextureDimension3D = 0;
    /** @brief Maximum number of layers in a texture array. */
    std::uint32_t maxTextureArrayLayers = 0;
    /** @brief Maximum number of texture bindings. */
    std::uint32_t maxTextureBindings = 0;
    /** @brief Maximum supported sample count. */
    std::uint32_t maxSampleCount = 1;
    /** @brief Maximum number of uniform-buffer bindings. */
    std::uint32_t maxUniformBufferBindings = 0;
    /** @brief Maximum uniform-buffer size in bytes. */
    std::uint32_t maxUniformBufferSize = 0;
    /** @brief Maximum number of storage-buffer bindings. */
    std::uint32_t maxStorageBufferBindings = 0;
    /** @brief Required offset alignment for uniform-buffer ranges. */
    std::uint32_t uniformBufferOffsetAlignment = 1;
    /** @brief Required offset alignment for storage-buffer ranges. */
    std::uint32_t storageBufferOffsetAlignment = 1;
  };

  /**
   * @brief Check whether an available capability set satisfies requirements.
   * @param required Feature flags and maximum values requested by the caller.
   * @param available Features and limits reported by the device.
   * @return `true` when every requested feature and limit is met.
   */
  inline bool requirementsMet(const GPUCapabilities &required,
                              const GPUCapabilities &available)
  {
    return (!required.compute || available.compute) &&
           (!required.storageBuffers || available.storageBuffers) &&
           (!required.storageTextures || available.storageTextures) &&
           (!required.memoryBarriers || available.memoryBarriers) &&
           (!required.indirectDraw || available.indirectDraw) &&
           (!required.indirectCount || available.indirectCount) &&
           (!required.asyncReadback || available.asyncReadback) &&
           (!required.depthReadback || available.depthReadback) &&
           (!required.timestampQueries || available.timestampQueries) &&
           (!required.occlusionQueries || available.occlusionQueries) &&
           (!required.textureArrays || available.textureArrays) &&
           (!required.textureCompressionBC || available.textureCompressionBC) &&
           (!required.textureCompressionBC1 ||
            available.textureCompressionBC1) &&
           (!required.textureCompressionBC3 ||
            available.textureCompressionBC3) &&
           (!required.textureCompressionBC5 ||
            available.textureCompressionBC5) &&
           (!required.textureCompressionBC7 ||
            available.textureCompressionBC7) &&
           (!required.textureCompressionETC2 ||
            available.textureCompressionETC2) &&
           (!required.textureCompressionASTC ||
            available.textureCompressionASTC) &&
           (!required.samplerBorderColor || available.samplerBorderColor) &&
           (!required.wireframe || available.wireframe) &&
           (!required.independentBlend || available.independentBlend) &&
           (!required.baseInstance || available.baseInstance) &&
           (!required.tessellationShader || available.tessellationShader) &&
           (!required.geometryShader || available.geometryShader) &&
           (!required.bindlessTextures || available.bindlessTextures) &&
           required.maxBindlessTextures <= available.maxBindlessTextures &&
           required.maxColorAttachments <= available.maxColorAttachments &&
           required.maxTextureDimension2D <= available.maxTextureDimension2D &&
           required.maxTextureDimension3D <= available.maxTextureDimension3D &&
           required.maxTextureArrayLayers <= available.maxTextureArrayLayers &&
           required.maxTextureBindings <= available.maxTextureBindings &&
           required.maxSampleCount <= available.maxSampleCount &&
           required.maxUniformBufferBindings <=
               available.maxUniformBufferBindings &&
           required.maxUniformBufferSize <= available.maxUniformBufferSize &&
           required.maxStorageBufferBindings <=
               available.maxStorageBufferBindings;
  }

} // namespace gpu

#endif
