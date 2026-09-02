#include "GLESDevice.h"

#if !defined(GPU_GL_DESKTOP)
#define GPU_GL_DESKTOP 0
#endif

#if !defined(GPU_GLES_HAS_ES31)
#define GPU_GLES_HAS_ES31 1
#endif
#if !defined(GPU_GLES_HAS_ES32)
#define GPU_GLES_HAS_ES32 1
#endif
#if !defined(GPU_GLES_HAS_KHR_DEBUG)
#define GPU_GLES_HAS_KHR_DEBUG 1
#endif
#if !defined(GPU_GLES_HAS_BUFFER_MAP)
#define GPU_GLES_HAS_BUFFER_MAP 1
#endif

#include <algorithm>
#include <cstring>
#include <limits>

namespace gpu {

namespace {

GLenum bufferTarget(std::uint32_t usage) {
  if (usage & BufferUsageIndex)
    return GL_ELEMENT_ARRAY_BUFFER;
  if (usage & BufferUsageUniform)
    return GL_UNIFORM_BUFFER;
  if (usage & BufferUsageStorage)
    return GL_SHADER_STORAGE_BUFFER;
  if (usage & BufferUsageIndirect)
    return GL_DRAW_INDIRECT_BUFFER;
  return GL_ARRAY_BUFFER;
}

bool textureFormat(Format format, GLint &internalFormat, GLenum &layout,
                   GLenum &type) {
  switch (format) {
  case Format::R8:
    internalFormat = GL_R8;
    layout = GL_RED;
    type = GL_UNSIGNED_BYTE;
    return true;
  case Format::RG8:
    internalFormat = GL_RG8;
    layout = GL_RG;
    type = GL_UNSIGNED_BYTE;
    return true;
  case Format::RGBA8:
    internalFormat = GL_RGBA8;
    layout = GL_RGBA;
    type = GL_UNSIGNED_BYTE;
    return true;
  case Format::RGBA8Srgb:
    internalFormat = GL_SRGB8_ALPHA8;
    layout = GL_RGBA;
    type = GL_UNSIGNED_BYTE;
    return true;
  case Format::R16Float:
    internalFormat = GL_R16F;
    layout = GL_RED;
    type = GL_HALF_FLOAT;
    return true;
  case Format::RG16Float:
    internalFormat = GL_RG16F;
    layout = GL_RG;
    type = GL_HALF_FLOAT;
    return true;
  case Format::RGBA16Float:
    internalFormat = GL_RGBA16F;
    layout = GL_RGBA;
    type = GL_HALF_FLOAT;
    return true;
  case Format::R32Float:
    internalFormat = GL_R32F;
    layout = GL_RED;
    type = GL_FLOAT;
    return true;
  case Format::RG32Float:
    internalFormat = GL_RG32F;
    layout = GL_RG;
    type = GL_FLOAT;
    return true;
  case Format::RGB32Float:
    internalFormat = GL_RGB32F;
    layout = GL_RGB;
    type = GL_FLOAT;
    return true;
  case Format::RGBA32Float:
    internalFormat = GL_RGBA32F;
    layout = GL_RGBA;
    type = GL_FLOAT;
    return true;
  case Format::R11G11B10Float:
    internalFormat = GL_R11F_G11F_B10F;
    layout = GL_RGB;
    type = GL_UNSIGNED_INT_10F_11F_11F_REV;
    return true;
  case Format::RGB10A2:
    internalFormat = GL_RGB10_A2;
    layout = GL_RGBA;
    type = GL_UNSIGNED_INT_2_10_10_10_REV;
    return true;
  case Format::R32Uint:
    internalFormat = GL_R32UI;
    layout = GL_RED_INTEGER;
    type = GL_UNSIGNED_INT;
    return true;
  case Format::R16Uint:
    internalFormat = GL_R16UI;
    layout = GL_RED_INTEGER;
    type = GL_UNSIGNED_SHORT;
    return true;
  case Format::RG16Uint:
    internalFormat = GL_RG16UI;
    layout = GL_RG_INTEGER;
    type = GL_UNSIGNED_SHORT;
    return true;
  case Format::RGBA16Uint:
    internalFormat = GL_RGBA16UI;
    layout = GL_RGBA_INTEGER;
    type = GL_UNSIGNED_SHORT;
    return true;
  case Format::RG32Uint:
    internalFormat = GL_RG32UI;
    layout = GL_RG_INTEGER;
    type = GL_UNSIGNED_INT;
    return true;
  case Format::RGBA32Uint:
    internalFormat = GL_RGBA32UI;
    layout = GL_RGBA_INTEGER;
    type = GL_UNSIGNED_INT;
    return true;
  case Format::Depth16:
    internalFormat = GL_DEPTH_COMPONENT16;
    layout = GL_DEPTH_COMPONENT;
    type = GL_UNSIGNED_SHORT;
    return true;
  case Format::Depth24:
    internalFormat = GL_DEPTH_COMPONENT24;
    layout = GL_DEPTH_COMPONENT;
    type = GL_UNSIGNED_INT;
    return true;
  case Format::Depth32Float:
    internalFormat = GL_DEPTH_COMPONENT32F;
    layout = GL_DEPTH_COMPONENT;
    type = GL_FLOAT;
    return true;
  case Format::Depth24Stencil8:
    internalFormat = GL_DEPTH24_STENCIL8;
    layout = GL_DEPTH_STENCIL;
    type = GL_UNSIGNED_INT_24_8;
    return true;
  default:
    return false;
  }
}

struct CompressedFormatInfo {
  GLenum internalFormat = 0;
  std::uint32_t blockBytes = 0;
};

constexpr GLenum kTextureMaxAnisotropyExt = 0x84FE;
constexpr GLenum kMaxTextureMaxAnisotropyExt = 0x84FF;
constexpr GLenum kCompressedRGBAS3TCDXT1Ext = 0x83F1;
constexpr GLenum kCompressedSRGBAlphaS3TCDXT1Ext = 0x8C4D;
constexpr GLenum kCompressedRGBAS3TCDXT5Ext = 0x83F3;
constexpr GLenum kCompressedSRGBAlphaS3TCDXT5Ext = 0x8C4F;
constexpr GLenum kCompressedRGRGTC2 = 0x8DBD;
constexpr GLenum kCompressedRGBABPTCUnorm = 0x8E8C;
constexpr GLenum kCompressedSRGBAlphaBPTCUnorm = 0x8E8D;

bool compressedTextureFormat(Format format, CompressedFormatInfo &info) {
  switch (format) {
  case Format::BC1RGBA:
    info = {kCompressedRGBAS3TCDXT1Ext, 8};
    return true;
  case Format::BC1RGBASrgb:
    info = {kCompressedSRGBAlphaS3TCDXT1Ext, 8};
    return true;
  case Format::BC3RGBA:
    info = {kCompressedRGBAS3TCDXT5Ext, 16};
    return true;
  case Format::BC3RGBASrgb:
    info = {kCompressedSRGBAlphaS3TCDXT5Ext, 16};
    return true;
  case Format::BC5RG:
    info = {kCompressedRGRGTC2, 16};
    return true;
  case Format::BC7RGBA:
    info = {kCompressedRGBABPTCUnorm, 16};
    return true;
  case Format::BC7RGBASrgb:
    info = {kCompressedSRGBAlphaBPTCUnorm, 16};
    return true;
  case Format::ETC2RGBA8:
    info = {GL_COMPRESSED_RGBA8_ETC2_EAC, 16};
    return true;
  case Format::ETC2RGBA8Srgb:
    info = {GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC, 16};
    return true;
  case Format::ASTC4x4RGBA:
    info = {GL_COMPRESSED_RGBA_ASTC_4x4, 16};
    return true;
  case Format::ASTC4x4RGBASrgb:
    info = {GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4, 16};
    return true;
  default:
    return false;
  }
}

bool compressedFormatSupported(Format format,
                               const GPUCapabilities &capabilities) {
  switch (format) {
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
    return false;
  }
}

bool compressedLevelSize(std::uint32_t width, std::uint32_t height,
                         std::uint32_t images, std::uint32_t blockBytes,
                         std::uint64_t &size) {
  const std::uint64_t blocksWide = (static_cast<std::uint64_t>(width) + 3) / 4;
  const std::uint64_t blocksHigh =
      (static_cast<std::uint64_t>(height) + 3) / 4;
  const std::uint64_t max = (std::numeric_limits<std::uint64_t>::max)();
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

bool requiredUploadSize(std::uint64_t offset, std::uint64_t bytesPerRow,
                        std::uint64_t rowsPerImage,
                        std::uint64_t copiedRows,
                        std::uint64_t tightRowBytes, std::uint64_t images,
                        std::uint64_t &required) {
  const std::uint64_t max = (std::numeric_limits<std::uint64_t>::max)();
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

GLenum textureTarget(TextureDimension dimension, std::uint32_t sampleCount) {
  if (sampleCount > 1)
    return GL_TEXTURE_2D_MULTISAMPLE;
  switch (dimension) {
  case TextureDimension::Texture2DArray:
    return GL_TEXTURE_2D_ARRAY;
  case TextureDimension::Texture3D:
    return GL_TEXTURE_3D;
  case TextureDimension::TextureCube:
    return GL_TEXTURE_CUBE_MAP;
  case TextureDimension::Texture2D:
  default:
    return GL_TEXTURE_2D;
  }
}

bool depthFormat(Format format) {
  return format == Format::Depth16 || format == Format::Depth24 ||
         format == Format::Depth32Float ||
         format == Format::Depth24Stencil8;
}

bool stencilFormat(Format format) {
  return format == Format::Depth24Stencil8;
}

bool unsignedIntegerFormat(Format format) {
  return format == Format::R16Uint || format == Format::RG16Uint ||
         format == Format::RGBA16Uint || format == Format::R32Uint ||
         format == Format::RG32Uint || format == Format::RGBA32Uint;
}

std::uint32_t bytesPerTexel(Format format) {
  switch (format) {
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

bool checkedTextureDataSize(const TextureDesc &desc, std::uint64_t &size) {
  const std::uint32_t texelSize = bytesPerTexel(desc.format);
  if (texelSize == 0)
    return false;
  std::uint64_t depth = 1;
  if (desc.dimension == TextureDimension::Texture2DArray ||
      desc.dimension == TextureDimension::Texture3D)
    depth = desc.depthOrLayers;
  else if (desc.dimension == TextureDimension::TextureCube)
    depth = 6;
  const std::uint64_t max = (std::numeric_limits<std::uint64_t>::max)();
  if (desc.width > max / desc.height)
    return false;
  size = static_cast<std::uint64_t>(desc.width) * desc.height;
  if (size > max / depth)
    return false;
  size *= depth;
  if (size > max / texelSize)
    return false;
  size *= texelSize;
  return true;
}

std::uint32_t maximumMipCount(const TextureDesc &desc) {
  std::uint32_t largest = std::max(desc.width, desc.height);
  if (desc.dimension == TextureDimension::Texture3D)
    largest = std::max(largest, desc.depthOrLayers);
  std::uint32_t count = 0;
  while (largest != 0) {
    ++count;
    largest >>= 1;
  }
  return count;
}

std::uint32_t mipDimension(std::uint32_t value, std::uint32_t mipLevel) {
  return std::max(value >> mipLevel, 1u);
}

bool validAttachmentLayer(TextureDimension dimension,
                          std::uint32_t depthOrLayers,
                          std::uint32_t mipLevel,
                          std::uint32_t layer) {
  switch (dimension) {
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

void attachTexture(GLenum framebufferTarget, GLenum attachment,
                   GLuint texture, GLenum target, std::uint32_t mipLevel,
                   std::uint32_t layer) {
  if (target == GL_TEXTURE_CUBE_MAP) {
    glFramebufferTexture2D(framebufferTarget, attachment,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, texture,
                           static_cast<GLint>(mipLevel));
  } else if (target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_3D) {
    glFramebufferTextureLayer(framebufferTarget, attachment, texture,
                              static_cast<GLint>(mipLevel),
                              static_cast<GLint>(layer));
  } else {
    glFramebufferTexture2D(framebufferTarget, attachment, target, texture,
                           static_cast<GLint>(mipLevel));
  }
}

void attachTexture(GLenum attachment, GLuint texture, GLenum target,
                   std::uint32_t mipLevel, std::uint32_t layer) {
  attachTexture(GL_FRAMEBUFFER, attachment, texture, target, mipLevel, layer);
}

TextureDimension dimensionOfTarget(GLenum target) {
  if (target == GL_TEXTURE_CUBE_MAP)
    return TextureDimension::TextureCube;
  if (target == GL_TEXTURE_2D_ARRAY)
    return TextureDimension::Texture2DArray;
  if (target == GL_TEXTURE_3D)
    return TextureDimension::Texture3D;
  return TextureDimension::Texture2D;
}

void ensureFramebuffer(GLuint &framebuffer) {
  if (framebuffer == 0)
    glGenFramebuffers(1, &framebuffer);
}

void detachAllAttachments(GLenum framebufferTarget) {
  glFramebufferTexture2D(framebufferTarget, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         0, 0);
  glFramebufferTexture2D(framebufferTarget, GL_DEPTH_STENCIL_ATTACHMENT,
                         GL_TEXTURE_2D, 0, 0);
  glFramebufferTexture2D(framebufferTarget, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         0, 0);
  glFramebufferTexture2D(framebufferTarget, GL_STENCIL_ATTACHMENT,
                         GL_TEXTURE_2D, 0, 0);
}

GLenum textureAttachmentPoint(Format format, GLenum &mask) {
  if (!depthFormat(format)) {
    mask = GL_COLOR_BUFFER_BIT;
    return GL_COLOR_ATTACHMENT0;
  }
  if (stencilFormat(format)) {
    mask = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    return GL_DEPTH_STENCIL_ATTACHMENT;
  }
  mask = GL_DEPTH_BUFFER_BIT;
  return GL_DEPTH_ATTACHMENT;
}

GLenum samplerFilter(Filter filter) {
  return filter == Filter::Nearest ? GL_NEAREST : GL_LINEAR;
}

GLenum samplerMinFilter(Filter minFilter, Filter mipFilter) {
  if (minFilter == Filter::Nearest)
    return mipFilter == Filter::Nearest ? GL_NEAREST_MIPMAP_NEAREST
                                        : GL_NEAREST_MIPMAP_LINEAR;
  return mipFilter == Filter::Nearest ? GL_LINEAR_MIPMAP_NEAREST
                                      : GL_LINEAR_MIPMAP_LINEAR;
}

GLenum addressMode(AddressMode mode) {
  switch (mode) {
  case AddressMode::MirrorRepeat:
    return GL_MIRRORED_REPEAT;
  case AddressMode::ClampToEdge:
    return GL_CLAMP_TO_EDGE;
  case AddressMode::ClampToBorder:
    return GL_CLAMP_TO_BORDER;
  case AddressMode::Repeat:
  default:
    return GL_REPEAT;
  }
}

GLenum topologyValue(Topology topology) {
  switch (topology) {
  case Topology::TriangleStrip:
    return GL_TRIANGLE_STRIP;
  case Topology::Lines:
    return GL_LINES;
  case Topology::LineStrip:
    return GL_LINE_STRIP;
  case Topology::Points:
    return GL_POINTS;
  case Topology::Triangles:
  default:
    return GL_TRIANGLES;
  }
}

GLenum compareValue(CompareOp compare) {
  static const GLenum values[] = {GL_NEVER,   GL_LESS,    GL_EQUAL,
                                  GL_LEQUAL,  GL_GREATER, GL_NOTEQUAL,
                                  GL_GEQUAL, GL_ALWAYS};
  return values[static_cast<std::uint32_t>(compare)];
}

GLenum blendFactorValue(BlendFactor factor) {
  static const GLenum values[] = {
      GL_ZERO,           GL_ONE,
      GL_SRC_COLOR,      GL_ONE_MINUS_SRC_COLOR,
      GL_SRC_ALPHA,      GL_ONE_MINUS_SRC_ALPHA,
      GL_DST_COLOR,      GL_ONE_MINUS_DST_COLOR,
      GL_DST_ALPHA,      GL_ONE_MINUS_DST_ALPHA};
  return values[static_cast<std::uint32_t>(factor)];
}

GLenum blendOperationValue(BlendOperation operation) {
  static const GLenum values[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT,
                                  GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX};
  return values[static_cast<std::uint32_t>(operation)];
}

bool blendComponentEqual(const BlendComponent &left,
                         const BlendComponent &right) {
  return left.sourceFactor == right.sourceFactor &&
         left.destinationFactor == right.destinationFactor &&
         left.operation == right.operation;
}

bool blendStateEqual(const ColorTargetState &left,
                     const ColorTargetState &right) {
  return left.blendEnabled == right.blendEnabled &&
         (!left.blendEnabled ||
          (blendComponentEqual(left.colorBlend, right.colorBlend) &&
           blendComponentEqual(left.alphaBlend, right.alphaBlend)));
}

GLenum stencilOperationValue(StencilOperation operation) {
  static const GLenum values[] = {GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR,
                                  GL_DECR, GL_INVERT, GL_INCR_WRAP,
                                  GL_DECR_WRAP};
  return values[static_cast<std::uint32_t>(operation)];
}

struct VertexFormatInfo {
  GLint components;
  GLenum type;
  GLboolean normalized;
  bool integer;
};

VertexFormatInfo vertexFormatInfo(VertexFormat format) {
  switch (format) {
  case VertexFormat::Float32:
    return {1, GL_FLOAT, GL_FALSE, false};
  case VertexFormat::Float32x2:
    return {2, GL_FLOAT, GL_FALSE, false};
  case VertexFormat::Float32x3:
    return {3, GL_FLOAT, GL_FALSE, false};
  case VertexFormat::Float32x4:
    return {4, GL_FLOAT, GL_FALSE, false};
  case VertexFormat::Uint32:
    return {1, GL_UNSIGNED_INT, GL_FALSE, true};
  case VertexFormat::Uint32x2:
    return {2, GL_UNSIGNED_INT, GL_FALSE, true};
  case VertexFormat::Uint32x3:
    return {3, GL_UNSIGNED_INT, GL_FALSE, true};
  case VertexFormat::Uint32x4:
    return {4, GL_UNSIGNED_INT, GL_FALSE, true};
  case VertexFormat::Unorm8x4:
    return {4, GL_UNSIGNED_BYTE, GL_TRUE, false};
  case VertexFormat::Snorm8x4:
    return {4, GL_BYTE, GL_TRUE, false};
  }
  return {1, GL_FLOAT, GL_FALSE, false};
}

bool hasExtension(const char *name) {
  GLint count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &count);
  for (GLint index = 0; index < count; ++index) {
    const GLubyte *extension = glGetStringi(GL_EXTENSIONS, index);
    if (extension &&
        std::strcmp(reinterpret_cast<const char *>(extension), name) == 0)
      return true;
  }
  return false;
}

bool samplerUniformType(GLenum type) {
  switch (type) {
  case GL_SAMPLER_2D:
  case GL_SAMPLER_3D:
  case GL_SAMPLER_CUBE:
  case GL_SAMPLER_2D_SHADOW:
  case GL_SAMPLER_2D_ARRAY:
  case GL_SAMPLER_2D_ARRAY_SHADOW:
  case GL_SAMPLER_CUBE_SHADOW:
  case GL_SAMPLER_2D_MULTISAMPLE:
  case GL_INT_SAMPLER_2D:
  case GL_INT_SAMPLER_3D:
  case GL_INT_SAMPLER_CUBE:
  case GL_INT_SAMPLER_2D_ARRAY:
  case GL_INT_SAMPLER_2D_MULTISAMPLE:
  case GL_UNSIGNED_INT_SAMPLER_2D:
  case GL_UNSIGNED_INT_SAMPLER_3D:
  case GL_UNSIGNED_INT_SAMPLER_CUBE:
  case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
  case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE:
    return true;
  default:
    return false;
  }
}

bool imageUniformType(GLenum type) {
  switch (type) {
  case GL_IMAGE_2D:
  case GL_IMAGE_3D:
  case GL_IMAGE_CUBE:
  case GL_IMAGE_2D_ARRAY:
  case GL_INT_IMAGE_2D:
  case GL_INT_IMAGE_3D:
  case GL_INT_IMAGE_CUBE:
  case GL_INT_IMAGE_2D_ARRAY:
  case GL_UNSIGNED_INT_IMAGE_2D:
  case GL_UNSIGNED_INT_IMAGE_3D:
  case GL_UNSIGNED_INT_IMAGE_CUBE:
  case GL_UNSIGNED_INT_IMAGE_2D_ARRAY:
    return true;
  default:
    return false;
  }
}

} // namespace

#define GPU_GL_DEVICE_CLASS GLESDevice
#include "../gl/GLDeviceCommon.inl"
#undef GPU_GL_DEVICE_CLASS

Device *createOpenGLESDevice(const DeviceDesc &desc, GPUError *error) {
  if (desc.backend != Backend::OpenGLES) {
    reportDeviceCreationFailure(error, GPUErrorCode::InvalidArgument,
                                "descriptor does not select the OpenGL ES backend");
    return nullptr;
  }
  if (!desc.surface.nativeHandle) {
    reportDeviceCreationFailure(error, GPUErrorCode::InvalidArgument,
                                "the OpenGL ES backend requires a surface handle");
    return nullptr;
  }
  GLESDevice *device = new GLESDevice(desc);
  if (!device->initialize()) {
    delete device;
    reportDeviceCreationFailure(
        error, GPUErrorCode::DeviceCreationFailed,
        "the OpenGL ES context could not be made current or does not meet the "
        "minimum version");
    return nullptr;
  }
  if (!requirementsMet(desc.requiredCapabilities, device->capabilities())) {
    delete device;
    reportDeviceCreationFailure(
        error, GPUErrorCode::UnsupportedFeature,
        "required capabilities are unavailable on this device");
    return nullptr;
  }
  return device;
}

} // namespace gpu
