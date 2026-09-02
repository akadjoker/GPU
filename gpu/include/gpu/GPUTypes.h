#ifndef GPU_TYPES_H
#define GPU_TYPES_H

#include <cstdint>

namespace gpu
{

  /** @brief Portability profile requested during device creation. */
  enum class RendererProfile : std::uint8_t
  {
    Portable,
    Modern
  };

  /** @brief Texture and render-target formats understood by the API. */
  enum class Format : std::uint8_t
  {
    Unknown,
    R8,
    RG8,
    RGBA8,
    RGBA8Srgb,
    R16Float,
    RG16Float,
    RGBA16Float,
    R32Float,
    RG32Float,
    RGB32Float,
    RGBA32Float,
    R11G11B10Float,
    RGB10A2,
    R16Uint,
    RG16Uint,
    RGBA16Uint,
    R32Uint,
    RG32Uint,
    RGBA32Uint,
    BC1RGBA,
    BC1RGBASrgb,
    BC3RGBA,
    BC3RGBASrgb,
    BC5RG,
    BC7RGBA,
    BC7RGBASrgb,
    ETC2RGBA8,
    ETC2RGBA8Srgb,
    ASTC4x4RGBA,
    ASTC4x4RGBASrgb,
    Depth16,
    Depth24,
    Depth32Float,
    Depth24Stencil8
  };

  /** @brief Texture dimensionality and array shape. */
  enum class TextureDimension : std::uint8_t
  {
    Texture2D,
    Texture2DArray,
    Texture3D,
    TextureCube
  };

  /** @brief Primitive topology used by a graphics pipeline. */
  enum class Topology : std::uint8_t
  {
    Triangles,
    TriangleStrip,
    Lines,
    LineStrip,
    Points,
    // Only valid on a pipeline with both a tessellation control and a
    // tessellation evaluation shader (PipelineDesc::patchControlPoints
    // gives the vertex count per patch); rejected everywhere else.
    Patches
  };

  /** @brief Operation applied to an attachment before rendering. */
  enum class LoadOp : std::uint8_t
  {
    Load,
    Clear,
    DontCare
  };

  /** @brief Operation applied to an attachment after rendering. */
  enum class StoreOp : std::uint8_t
  {
    Store,
    Discard
  };

  /** @brief Comparison function used by depth, stencil, or sampler state. */
  enum class CompareOp : std::uint8_t
  {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
  };

  /** @brief Element type of an index buffer. */
  enum class IndexFormat : std::uint8_t
  {
    Uint16,
    Uint32
  };

  /** @brief CPU access mode requested by mapBuffer. */
  enum class MapMode : std::uint8_t
  {
    Read,
    Write
  };

  /** @brief Query category created by createQuery. */
  enum class QueryType : std::uint8_t
  {
    Occlusion,
    Timestamp
  };

  /** @brief Vertex attribute element format. */
  enum class VertexFormat : std::uint8_t
  {
    Float32,
    Float32x2,
    Float32x3,
    Float32x4,
    Uint32,
    Uint32x2,
    Uint32x3,
    Uint32x4,
    Unorm8x4,
    Snorm8x4
  };

  /** @brief Frequency at which a vertex buffer layout advances. */
  enum class VertexStepMode : std::uint8_t
  {
    Vertex,
    Instance
  };

  /** @brief Multiplicative factor used by a blend equation. */
  enum class BlendFactor : std::uint8_t
  {
    Zero,
    One,
    SourceColor,
    OneMinusSourceColor,
    SourceAlpha,
    OneMinusSourceAlpha,
    DestinationColor,
    OneMinusDestinationColor,
    DestinationAlpha,
    OneMinusDestinationAlpha
  };

  /** @brief Arithmetic operation used by a blend equation. */
  enum class BlendOperation : std::uint8_t
  {
    Add,
    Subtract,
    ReverseSubtract,
    Minimum,
    Maximum
  };

  /** @brief Face-culling mode used by rasterization. */
  enum class CullMode : std::uint8_t
  {
    None,
    Front,
    Back
  };

  /** @brief Winding that identifies front-facing primitives. */
  enum class FrontFace : std::uint8_t
  {
    CounterClockwise,
    Clockwise
  };

  /** @brief Operation applied to a stencil value. */
  enum class StencilOperation : std::uint8_t
  {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap
  };

  /** @brief Bit flags selecting writable color channels. */
  enum ColorWrite : std::uint8_t
  {
    ColorWriteRed = 1u << 0,
    ColorWriteGreen = 1u << 1,
    ColorWriteBlue = 1u << 2,
    ColorWriteAlpha = 1u << 3,
    ColorWriteAll = ColorWriteRed | ColorWriteGreen | ColorWriteBlue | ColorWriteAlpha
  };

  /** @brief Bit flags describing permitted buffer uses. */
  enum BufferUsage : std::uint32_t
  {
    BufferUsageVertex = 1u << 0,
    BufferUsageIndex = 1u << 1,
    BufferUsageUniform = 1u << 2,
    BufferUsageStorage = 1u << 3,
    BufferUsageIndirect = 1u << 4,
    BufferUsageStaging = 1u << 5,
    BufferUsageReadback = 1u << 6
  };

  /** @brief Bit flags describing permitted texture uses. */
  enum TextureUsage : std::uint32_t
  {
    TextureUsageSampled = 1u << 0,
    TextureUsageStorage = 1u << 1,
    TextureUsageRenderTarget = 1u << 2,
    TextureUsageCopySource = 1u << 3,
    TextureUsageCopyDestination = 1u << 4
  };

  /** @brief Bit flags selecting resource classes for memoryBarrier. */
  enum Barrier : std::uint32_t
  {
    BarrierVertex = 1u << 0,
    BarrierIndex = 1u << 1,
    BarrierUniform = 1u << 2,
    BarrierStorage = 1u << 3,
    BarrierTexture = 1u << 4,
    BarrierIndirect = 1u << 5,
    BarrierAll = 0xffffffffu
  };

} // namespace gpu

#endif
