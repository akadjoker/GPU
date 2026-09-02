#ifndef GPU_DESCRIPTORS_H
#define GPU_DESCRIPTORS_H

#include "GPUHandles.h"
#include "GPUTypes.h"

#include <cstdint>

namespace gpu
{

  /** @brief Non-owning immutable byte range supplied to a device call. */
  struct DataView
  {
    /** @brief Start address of the byte range. */
    const void *data = nullptr;
    /** @brief Number of bytes available at data. */
    std::uint64_t size = 0;
  };

  /** @brief Non-owning writable byte range supplied to a device call. */
  struct MutableDataView
  {
    /** @brief Start address of the writable byte range. */
    void *data = nullptr;
    /** @brief Number of writable bytes available at data. */
    std::uint64_t size = 0;
  };

  /** @brief Parameters used to create a buffer. */
  struct BufferDesc
  {
    /** @brief Requested buffer size in bytes. */
    std::uint64_t size = 0;
    /** @brief Bitwise combination of BufferUsage values. */
    std::uint32_t usage = 0;
    /** @brief Optional initial bytes copied during creation. */
    DataView initialData;
    /** @brief Optional non-owning debug label. */
    const char *debugName = nullptr;
  };

  /** @brief Parameters used to create a texture. */
  struct TextureDesc
  {
    /** @brief Texture dimensionality and array interpretation. */
    TextureDimension dimension = TextureDimension::Texture2D;
    /** @brief Texture storage format. */
    Format format = Format::RGBA8;
    /** @brief Width in texels. */
    std::uint32_t width = 0;
    /** @brief Height in texels. */
    std::uint32_t height = 0;
    /** @brief Depth in texels or array-layer count. */
    std::uint32_t depthOrLayers = 1;
    /** @brief Number of mip levels. */
    std::uint32_t mipCount = 1;
    /** @brief Requested multisample count. */
    std::uint32_t sampleCount = 1;
    /** @brief Bitwise combination of TextureUsage values. */
    std::uint32_t usage = TextureUsageSampled;
    /** @brief Optional initial texture bytes. */
    DataView initialData;
    /** @brief Optional non-owning debug label. */
    const char *debugName = nullptr;
  };

  /** @brief A mip-level region expressed in texels or layers. */
  struct TextureRegion
  {
    /** @brief Mip level containing the region. */
    std::uint32_t mipLevel = 0;
    /** @brief Region origin on the X axis. */
    std::uint32_t x = 0;
    /** @brief Region origin on the Y axis. */
    std::uint32_t y = 0;
    /** @brief Region origin on the Z axis or layer index. */
    std::uint32_t z = 0;
    /** @brief Region width. */
    std::uint32_t width = 0;
    /** @brief Region height. */
    std::uint32_t height = 0;
    /** @brief Region depth or layer count. */
    std::uint32_t depthOrLayers = 1;
  };

  /** @brief Destination origin for a texture copy operation. */
  struct TextureOrigin
  {
    /** @brief Destination mip level. */
    std::uint32_t mipLevel = 0;
    /** @brief Destination X coordinate. */
    std::uint32_t x = 0;
    /** @brief Destination Y coordinate. */
    std::uint32_t y = 0;
    /** @brief Destination Z coordinate or layer. */
    std::uint32_t z = 0;
  };

  /**
   * @brief Byte layout used for texture upload and readback data.
   * @warning Exact row-pitch and packing validation is backend-dependent until
   * the contract is finalized; see API_REVIEW.md.
   */
  struct TextureDataLayout
  {
    /** @brief Byte offset of the first texel in the data view. */
    std::uint64_t offset = 0;
    /** @brief Byte distance between consecutive rows. */
    std::uint32_t bytesPerRow = 0;
    /** @brief Number of rows between consecutive images or layers. */
    std::uint32_t rowsPerImage = 0;
  };

  /** @brief Minification, magnification, or mip filtering mode. */
  enum class Filter : std::uint8_t
  {
    Nearest,
    Linear
  };

  /** @brief Addressing mode used outside a sampler coordinate range. */
  enum class AddressMode : std::uint8_t
  {
    Repeat,
    MirrorRepeat,
    ClampToEdge,
    ClampToBorder
  };

  /** @brief Parameters used to create a sampler. */
  struct SamplerDesc
  {
    /** @brief Minification filter. */
    Filter minFilter = Filter::Linear;
    /** @brief Magnification filter. */
    Filter magFilter = Filter::Linear;
    /** @brief Mip-level filter. */
    Filter mipFilter = Filter::Linear;
    /** @brief Address mode for the U coordinate. */
    AddressMode addressU = AddressMode::Repeat;
    /** @brief Address mode for the V coordinate. */
    AddressMode addressV = AddressMode::Repeat;
    /** @brief Address mode for the W coordinate. */
    AddressMode addressW = AddressMode::Repeat;
    /** @brief Maximum anisotropy requested by the sampler. */
    float maxAnisotropy = 1.0f;
    /** @brief Minimum level-of-detail clamp. */
    float lodMin = 0.0f;
    /** @brief Maximum level-of-detail clamp. */
    float lodMax = 32.0f;
    /** @brief Depth comparison operation. */
    CompareOp compare = CompareOp::LessEqual;
    /** @brief Enable depth comparison. */
    bool compareEnabled = false;
    /** @brief Optional non-owning debug label. */
    const char *debugName = nullptr;
  };

  /** @brief Source and entry point used to create a shader stage. */
  struct ShaderDesc
  {
    /** @brief Non-owning shader source byte range. */
    DataView source;
    /** @brief Null-terminated entry-point name. */
    const char *entryPoint = "main";
    /** @brief Optional non-owning debug label. */
    const char *debugName = nullptr;
  };

  /** @brief One vertex input attribute declaration. */
  struct VertexAttribute
  {
    /** @brief Attribute element format. */
    VertexFormat format = VertexFormat::Float32;
    /** @brief Byte offset from the start of the vertex element. */
    std::uint32_t offset = 0;
    /** @brief Shader input location. */
    std::uint32_t shaderLocation = 0;
  };

  /** @brief Vertex-buffer stride, step mode, and attributes. */
  struct VertexBufferLayout
  {
    /** @brief Maximum number of attributes stored in attributes. */
    static constexpr std::uint32_t MaxAttributes = 16;
    /** @brief Byte stride between vertex or instance elements. */
    std::uint32_t stride = 0;
    /** @brief Whether the layout advances per vertex or per instance. */
    VertexStepMode stepMode = VertexStepMode::Vertex;
    /** @brief Attribute declarations, up to attributeCount entries. */
    VertexAttribute attributes[MaxAttributes];
    /** @brief Number of active entries in attributes. */
    std::uint32_t attributeCount = 0;
  };

  /** @brief One color or alpha blend equation. */
  struct BlendComponent
  {
    /** @brief Source blend factor. */
    BlendFactor sourceFactor = BlendFactor::One;
    /** @brief Destination blend factor. */
    BlendFactor destinationFactor = BlendFactor::Zero;
    /** @brief Blend arithmetic operation. */
    BlendOperation operation = BlendOperation::Add;
  };

  /** @brief Color-target format and blending state for a pipeline. */
  struct ColorTargetState
  {
    /** @brief Target storage format. */
    Format format = Format::RGBA8Srgb;
    /** @brief RGB blend equation. */
    BlendComponent colorBlend;
    /** @brief Alpha blend equation. */
    BlendComponent alphaBlend;
    /** @brief Bitwise combination of ColorWrite values. */
    std::uint8_t writeMask = ColorWriteAll;
    /** @brief Enable color and alpha blending. */
    bool blendEnabled = false;
    // Hint for pipelines that render into the window surface: on backends
    // that must lock a pipeline to an explicit pixel format (Vulkan dynamic
    // rendering), setting this asks the backend to substitute the surface's
    // native format for this target instead of requiring `format` to match
    // it exactly. Most desktop compositors expose only BGRA-ordered
    // swapchain formats, which `Format` has no variant for, so pipelines
    // that draw to the surface should set this rather than guess `format`.
    // Backends that don't lock formats to pipelines (GL, GLES) ignore it.
    /** @brief Use the presentation surface's native target format when needed. */
    bool surface = false;
  };

  /** @brief Stencil comparison and operations for one face. */
  struct StencilFaceState
  {
    /** @brief Stencil comparison operation. */
    CompareOp compare = CompareOp::Always;
    /** @brief Operation when the stencil test fails. */
    StencilOperation failOperation = StencilOperation::Keep;
    /** @brief Operation when stencil passes but depth fails. */
    StencilOperation depthFailOperation = StencilOperation::Keep;
    /** @brief Operation when both stencil and depth pass. */
    StencilOperation passOperation = StencilOperation::Keep;
  };

  /** @brief Depth and stencil state for a pipeline. */
  struct DepthStencilState
  {
    /** @brief Depth/stencil attachment format. */
    Format format = Format::Depth24Stencil8;
    /** @brief Depth comparison operation. */
    CompareOp depthCompare = CompareOp::LessEqual;
    /** @brief Front-face stencil state. */
    StencilFaceState stencilFront;
    /** @brief Back-face stencil state. */
    StencilFaceState stencilBack;
    /** @brief Bits read by stencil comparisons. */
    std::uint32_t stencilReadMask = 0xff;
    /** @brief Bits written by stencil operations. */
    std::uint32_t stencilWriteMask = 0xff;
    /** @brief Enable depth writes. */
    bool depthWriteEnabled = true;
    /** @brief Enable depth testing. */
    bool depthTestEnabled = false;
    /** @brief Enable stencil testing. */
    bool stencilEnabled = false;
  };

  /** @brief Rasterization state for a pipeline. */
  struct RasterState
  {
    /** @brief Face culling mode. */
    CullMode cullMode = CullMode::Back;
    /** @brief Winding considered front-facing. */
    FrontFace frontFace = FrontFace::CounterClockwise;
    /** @brief Constant depth-bias term. */
    float depthBiasConstant = 0.0f;
    /** @brief Slope-scaled depth-bias term. */
    float depthBiasSlope = 0.0f;
    /** @brief Enable scissor testing for the pipeline. */
    bool scissorEnabled = false;
  };

  /** @brief Graphics, compute, and fixed-function pipeline description. */
  struct PipelineDesc
  {
    /** @brief Maximum number of vertex-buffer layouts. */
    static constexpr std::uint32_t MaxVertexBuffers = 8;
    /** @brief Maximum number of color-target states. */
    static constexpr std::uint32_t MaxColorTargets = 8;
    /** @brief Vertex shader stage description. */
    ShaderDesc vertex;
    /** @brief Fragment shader stage description. */
    ShaderDesc fragment;
    /** @brief Compute shader stage description. */
    ShaderDesc compute;
    // Tessellation control/evaluation: either both set (a patch pipeline,
    // topology must be Topology::Patches) or both left default. Geometry
    // is independent of tessellation and of vertex/fragment otherwise.
    // Requires GPUCapabilities::tessellationShader/geometryShader.
    /** @brief Optional tessellation-control shader stage. */
    ShaderDesc tessControl;
    /** @brief Optional tessellation-evaluation shader stage. */
    ShaderDesc tessEvaluation;
    /** @brief Optional geometry shader stage. */
    ShaderDesc geometry;
    /** @brief Vertex-buffer layout entries. */
    VertexBufferLayout vertexBuffers[MaxVertexBuffers];
    /** @brief Color-target state entries. */
    ColorTargetState colorTargets[MaxColorTargets];
    /** @brief Depth/stencil state. */
    DepthStencilState depthStencil;
    /** @brief Rasterization state. */
    RasterState raster;
    /** @brief Number of active vertex-buffer layouts. */
    std::uint32_t vertexBufferCount = 0;
    /** @brief Number of active color targets. */
    std::uint32_t colorTargetCount = 1;
    /** @brief Vertices per tessellation patch. */
    std::uint32_t patchControlPoints = 0;
    /** @brief Primitive topology. */
    Topology topology = Topology::Triangles;
    /** @brief Optional non-owning debug label. */
    const char *debugName = nullptr;
  };

  /** @brief Layout of one non-indexed indirect draw command. */
  struct DrawIndirectArgs
  {
    /** @brief Number of vertices. */
    std::uint32_t vertexCount = 0;
    /** @brief Number of instances. */
    std::uint32_t instanceCount = 1;
    /** @brief First vertex index. */
    std::uint32_t firstVertex = 0;
    /** @brief First instance index. */
    std::uint32_t firstInstance = 0;
  };

  /** @brief Layout of one indexed indirect draw command. */
  struct DrawIndexedIndirectArgs
  {
    /** @brief Number of indices. */
    std::uint32_t indexCount = 0;
    /** @brief Number of instances. */
    std::uint32_t instanceCount = 1;
    /** @brief First index offset. */
    std::uint32_t firstIndex = 0;
    /** @brief Signed vertex offset added to indices. */
    std::int32_t baseVertex = 0;
    /** @brief First instance index. */
    std::uint32_t firstInstance = 0;
  };

  /** @brief Viewport transform and depth range. */
  struct Viewport
  {
    /** @brief Viewport origin on the X axis. */
    float x = 0.0f;
    /** @brief Viewport origin on the Y axis. */
    float y = 0.0f;
    /** @brief Viewport width. */
    float width = 0.0f;
    /** @brief Viewport height. */
    float height = 0.0f;
    /** @brief Minimum depth mapped by the viewport. */
    float minDepth = 0.0f;
    /** @brief Maximum depth mapped by the viewport. */
    float maxDepth = 1.0f;
  };

  /** @brief Integer scissor rectangle. */
  struct Rect
  {
    /** @brief Rectangle origin on the X axis. */
    std::int32_t x = 0;
    /** @brief Rectangle origin on the Y axis. */
    std::int32_t y = 0;
    /** @brief Rectangle width. */
    std::uint32_t width = 0;
    /** @brief Rectangle height. */
    std::uint32_t height = 0;
  };

  /** @brief Texture subresource used as a render-pass attachment. */
  struct TargetAttachment
  {
    /** @brief Target texture handle. */
    TextureHandle texture;
    /** @brief Mip level used as the target. */
    std::uint32_t mipLevel = 0;
    /** @brief Array layer used as the target. */
    std::uint32_t layer = 0;
  };

  /** @brief Color attachment load, store, and clear state. */
  struct RenderPassColorAttachment
  {
    /** @brief Texture target for this attachment. */
    TargetAttachment target;
    /** @brief Select the presentation surface instead of target.texture. */
    bool surface = false;
    /** @brief Operation performed before rendering. */
    LoadOp loadOp = LoadOp::Load;
    /** @brief Operation performed after rendering. */
    StoreOp storeOp = StoreOp::Store;
    /** @brief RGBA clear value used when loadOp is Clear. */
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  };

  /** @brief Depth/stencil attachment load, store, and clear state. */
  struct RenderPassDepthStencilAttachment
  {
    /** @brief Texture target for the depth/stencil attachment. */
    TargetAttachment target;
    /** @brief Depth load operation. */
    LoadOp depthLoadOp = LoadOp::Load;
    /** @brief Depth store operation. */
    StoreOp depthStoreOp = StoreOp::Store;
    /** @brief Stencil load operation. */
    LoadOp stencilLoadOp = LoadOp::Load;
    /** @brief Stencil store operation. */
    StoreOp stencilStoreOp = StoreOp::Store;
    /** @brief Depth clear value used when depthLoadOp is Clear. */
    float clearDepth = 1.0f;
    /** @brief Stencil clear value used when stencilLoadOp is Clear. */
    std::uint32_t clearStencil = 0;
  };

  // Reflection de um pipeline ja linkado. Os backends GL/GLES ja enumeram os
  // uniforms depois do link para atribuir unidades de sampler e bindings de
  // uniform block; isto expoe esse resultado ao chamador, que e o que um
  // sistema de materiais precisa para ligar recursos por nome.
  /** @brief Resource category reported by pipeline reflection. */
  enum class ShaderResourceType : std::uint8_t
  {
    Sampler,
    UniformBuffer,
    StorageBuffer,
    StorageTexture
  };

  /** @brief One active resource reported by pipeline reflection. */
  struct ShaderResource
  {
    /** @brief Maximum number of characters stored in name. */
    static constexpr std::uint32_t MaxNameLength = 64;
    /** @brief Null-terminated resource name when it fits the fixed buffer. */
    char name[MaxNameLength] = {};
    /** @brief Reflected resource category. */
    ShaderResourceType type = ShaderResourceType::Sampler;
    /** @brief Texture unit for Sampler, or binding point for other types. */
    std::uint32_t slot = 0;
    /** @brief Element count for arrays, or 1 for a non-array resource. */
    std::uint32_t elementCount = 1;
    /** @brief Uniform-block size in bytes, or 0 for other resource types. */
    std::uint32_t blockSize = 0;
  };

  /** @brief Fixed-capacity reflection result for a linked pipeline. */
  struct PipelineReflection
  {
    /** @brief Maximum number of resources returned. */
    static constexpr std::uint32_t MaxResources = 32;
    /** @brief Reflected resource entries. */
    ShaderResource resources[MaxResources];
    /** @brief Number of active entries in resources. */
    std::uint32_t resourceCount = 0;
    // true quando havia mais recursos ativos do que MaxResources.
    bool truncated = false;
  };

  /** @brief Color and depth/stencil attachments for beginRenderPass. */
  struct RenderPassDesc
  {
    /** @brief Maximum number of color attachments. */
    static constexpr std::uint32_t MaxColorAttachments = 8;
    /** @brief Color attachment entries. */
    RenderPassColorAttachment colors[MaxColorAttachments];
    /** @brief Depth/stencil attachment state. */
    RenderPassDepthStencilAttachment depthStencil;
    /** @brief Number of active color attachments. */
    std::uint32_t colorCount = 0;
    /** @brief Whether depthStencil is active. */
    bool hasDepthStencil = false;
  };

} // namespace gpu

#endif
