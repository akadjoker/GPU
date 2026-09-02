#include "../demos/SDLGLSurface.h"

#include "RenderPathProbe.h"

#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"
#include "gpu/GPUProfiler.h"

#include <glad/gl.h>

#include <SDL2/SDL.h>

#include <cassert>
#include <cstdint>
#include <cstring>

namespace
{

  gpu::TextureHandle createRenderTexture(gpu::Device &device,
                                         gpu::Format format,
                                         std::uint32_t sampleCount = 1)
  {
    gpu::TextureDesc desc;
    desc.format = format;
    desc.width = 16;
    desc.height = 16;
    desc.sampleCount = sampleCount;
    desc.usage = gpu::TextureUsageSampled | gpu::TextureUsageRenderTarget;
    return device.createTexture(desc);
  }

  void waitForGPU(gpu::Device &device) {
    const gpu::FenceHandle fence = device.insertFence();
    assert(fence.valid());
    while (!device.isFenceSignaled(fence)) {
    }
    device.destroy(fence);
  }

  void assertNoErrors(gpu::Device &device)
  {
    assert(device.pendingErrorCount() == 0);
  }

  gpu::TextureHandle createCompressedTexture(gpu::Device &device,
                                             gpu::Format format,
                                             std::uint32_t blockBytes)
  {
    const std::uint8_t block[16] = {};
    gpu::TextureDesc desc;
    desc.format = format;
    desc.width = 4;
    desc.height = 4;
    desc.usage = gpu::TextureUsageSampled | gpu::TextureUsageCopyDestination;
    desc.initialData = {block, blockBytes};
    const gpu::TextureHandle texture = device.createTexture(desc);
    if (texture.valid())
    {
      gpu::TextureRegion region;
      region.width = 4;
      region.height = 4;
      assert(device.updateTexture(texture, region, {block, blockBytes}));
    }
    return texture;
  }

}

int main()
{
  assert(SDL_Init(SDL_INIT_VIDEO) == 0);
  SDLGLSurface surface;
  assert(createSDLGLSurface(surface, "GPU OpenGL Tests", 64, 64));
  SDL_HideWindow(surface.window);

  gpu::DeviceDesc deviceDesc;
  deviceDesc.backend = gpu::Backend::OpenGL;
  deviceDesc.surface.nativeHandle = &surface.gpuSurface;
  deviceDesc.surface.width = 64;
  deviceDesc.surface.height = 64;
  gpu::Device *device = gpu::createDevice(deviceDesc);
  assert(device != nullptr);

  const std::uint16_t grid[16] = {};
  gpu::TextureDesc gridDesc;
  gridDesc.format = gpu::Format::R16Uint;
  gridDesc.width = 4;
  gridDesc.height = 4;
  gridDesc.usage =
      gpu::TextureUsageSampled | gpu::TextureUsageCopyDestination;
  gridDesc.initialData = {grid, sizeof(grid)};
  const gpu::TextureHandle gridTexture = device->createTexture(gridDesc);
  assert(gridTexture.valid());
  const std::uint8_t paddedGrid[16] = {};
  gpu::TextureRegion gridRegion;
  gridRegion.x = 1;
  gridRegion.y = 1;
  gridRegion.width = 2;
  gridRegion.height = 2;
  gpu::TextureDataLayout gridLayout;
  gridLayout.offset = 2;
  gridLayout.bytesPerRow = 8;
  gridLayout.rowsPerImage = 2;
  assert(device->updateTexture(gridTexture, gridRegion,
                               {paddedGrid, sizeof(paddedGrid)}, gridLayout));

  gpu::TextureHandle compressedTextures[6];
  std::uint32_t compressedCount = 0;
  const gpu::GPUCapabilities &capabilities = device->capabilities();
  if (capabilities.textureCompressionBC1)
    compressedTextures[compressedCount++] =
        createCompressedTexture(*device, gpu::Format::BC1RGBA, 8);
  if (capabilities.textureCompressionBC3)
    compressedTextures[compressedCount++] =
        createCompressedTexture(*device, gpu::Format::BC3RGBA, 16);
  if (capabilities.textureCompressionBC5)
    compressedTextures[compressedCount++] =
        createCompressedTexture(*device, gpu::Format::BC5RG, 16);
  if (capabilities.textureCompressionBC7)
    compressedTextures[compressedCount++] =
        createCompressedTexture(*device, gpu::Format::BC7RGBA, 16);
  if (capabilities.textureCompressionETC2)
    compressedTextures[compressedCount++] =
        createCompressedTexture(*device, gpu::Format::ETC2RGBA8, 16);
  if (capabilities.textureCompressionASTC)
    compressedTextures[compressedCount++] =
        createCompressedTexture(*device, gpu::Format::ASTC4x4RGBA, 16);
  for (std::uint32_t index = 0; index < compressedCount; ++index)
    assert(compressedTextures[index].valid());
  assertNoErrors(*device);

  const gpu::TextureHandle color0 =
      createRenderTexture(*device, gpu::Format::RGBA8);
  const gpu::TextureHandle color1 =
      createRenderTexture(*device, gpu::Format::R16Uint);
  const gpu::TextureHandle depth =
      createRenderTexture(*device, gpu::Format::Depth24Stencil8);
  assert(color0 && color1 && depth);

  static const char vertexSource[] =
      "#version 330 core\n"
      "void main() {\n"
      "  vec2 p[3] = vec2[3](vec2(0.0, 0.5), vec2(-0.5, -0.5), vec2(0.5, -0.5));\n"
      "  gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);\n"
      "}\n";
  static const char fragmentSource[] =
      "#version 330 core\n"
      "layout(location=0) out vec4 color;\n"
      "layout(location=1) out uint identifier;\n"
      "void main() { color = vec4(1.0); identifier = 9u; }\n";
  gpu::PipelineDesc pipelineDesc;
  pipelineDesc.vertex.source = {vertexSource, std::strlen(vertexSource)};
  pipelineDesc.fragment.source = {fragmentSource, std::strlen(fragmentSource)};
  pipelineDesc.colorTargetCount = 2;
  pipelineDesc.colorTargets[0].format = gpu::Format::RGBA8;
  pipelineDesc.colorTargets[1].format = gpu::Format::R16Uint;
  pipelineDesc.colorTargets[1].writeMask = gpu::ColorWriteRed;
  pipelineDesc.raster.cullMode = gpu::CullMode::None;
  const gpu::PipelineHandle pipeline = device->createPipeline(pipelineDesc);
  assert(pipeline.valid());

  gpu::RenderPassDesc pass;
  pass.colorCount = 2;
  pass.colors[0].target.texture = color0;
  pass.colors[0].loadOp = gpu::LoadOp::Clear;
  pass.colors[1].target.texture = color1;
  pass.colors[1].loadOp = gpu::LoadOp::Clear;
  pass.colors[1].storeOp = gpu::StoreOp::Discard;
  pass.colors[1].clearColor[0] = 7.0f;
  pass.hasDepthStencil = true;
  pass.depthStencil.target.texture = depth;
  pass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
  pass.depthStencil.stencilLoadOp = gpu::LoadOp::Clear;
  pass.depthStencil.depthStoreOp = gpu::StoreOp::Discard;
  pass.depthStencil.stencilStoreOp = gpu::StoreOp::Discard;
  pass.depthStencil.clearStencil = 3;
  assert(device->beginRenderPass(pass));
  assert(device->setPipeline(pipeline));
  assert(device->draw(3));
  device->endRenderPass();
  assertNoErrors(*device);

  gpu::TextureDesc arrayDesc;
  arrayDesc.dimension = gpu::TextureDimension::Texture2DArray;
  arrayDesc.width = 8;
  arrayDesc.height = 8;
  arrayDesc.depthOrLayers = 4;
  arrayDesc.mipCount = 4;
  arrayDesc.usage = gpu::TextureUsageSampled |
                    gpu::TextureUsageRenderTarget |
                    gpu::TextureUsageCopyDestination;
  const gpu::TextureHandle arrayTexture = device->createTexture(arrayDesc);
  assert(arrayTexture.valid());
  const std::uint8_t arrayPixels[16] = {};
  gpu::TextureRegion arrayRegion;
  arrayRegion.mipLevel = 2;
  arrayRegion.z = 3;
  arrayRegion.width = 2;
  arrayRegion.height = 2;
  assert(device->updateTexture(arrayTexture, arrayRegion,
                               {arrayPixels, sizeof(arrayPixels)}));
  const std::uint8_t layeredPixels[56] = {};
  gpu::TextureRegion layeredRegion;
  layeredRegion.width = 2;
  layeredRegion.height = 2;
  layeredRegion.depthOrLayers = 2;
  gpu::TextureDataLayout layeredLayout;
  layeredLayout.bytesPerRow = 12;
  layeredLayout.rowsPerImage = 3;
  assert(device->updateTexture(arrayTexture, layeredRegion,
                               {layeredPixels, sizeof(layeredPixels)},
                               layeredLayout));
  gpu::RenderPassDesc arrayPass;
  arrayPass.colorCount = 1;
  arrayPass.colors[0].target.texture = arrayTexture;
  arrayPass.colors[0].target.mipLevel = 2;
  arrayPass.colors[0].target.layer = 3;
  arrayPass.colors[0].loadOp = gpu::LoadOp::Clear;
  assert(device->beginRenderPass(arrayPass));
  device->endRenderPass();
  assertNoErrors(*device);

  gpu::TextureDesc copySourceDesc;
  copySourceDesc.format = gpu::Format::R8;
  copySourceDesc.width = 4;
  copySourceDesc.height = 4;
  copySourceDesc.usage = gpu::TextureUsageCopySource;
  const std::uint8_t copySourcePixels[16] = {0, 1, 2, 3, 4, 5, 6, 7,
                                             8, 9, 10, 11, 12, 13, 14, 15};
  copySourceDesc.initialData = {copySourcePixels, sizeof(copySourcePixels)};
  const gpu::TextureHandle copySource = device->createTexture(copySourceDesc);
  assert(copySource.valid());

  gpu::TextureDesc copyDestinationDesc = copySourceDesc;
  copyDestinationDesc.usage =
      gpu::TextureUsageCopyDestination | gpu::TextureUsageCopySource;
  copyDestinationDesc.initialData = {};
  const gpu::TextureHandle copyDestination =
      device->createTexture(copyDestinationDesc);
  assert(copyDestination.valid());
  assertNoErrors(*device);

  gpu::TextureRegion copyRegion;
  copyRegion.x = 1;
  copyRegion.y = 1;
  copyRegion.width = 2;
  copyRegion.height = 2;
  gpu::TextureOrigin copyOrigin;
  assert(
      device->copyTexture(copyDestination, copyOrigin, copySource, copyRegion));
  assertNoErrors(*device);

  std::uint8_t readBack[4] = {};
  gpu::TextureRegion readRegion;
  readRegion.width = 2;
  readRegion.height = 2;
  assert(device->readTexture(copyDestination, readRegion,
                             {readBack, sizeof(readBack)}));
  assertNoErrors(*device);
  const std::uint8_t expectedReadBack[4] = {5, 6, 9, 10};
  assert(std::memcmp(readBack, expectedReadBack, sizeof(expectedReadBack)) == 0);

  gpu::TextureRegion outOfBoundsRegion;
  outOfBoundsRegion.x = 3;
  outOfBoundsRegion.y = 3;
  outOfBoundsRegion.width = 2;
  outOfBoundsRegion.height = 2;
  assert(!device->copyTexture(copyDestination, copyOrigin, copySource,
                              outOfBoundsRegion));
  device->clearErrors();

  gpu::TextureDesc mismatchedFormatDesc = copySourceDesc;
  mismatchedFormatDesc.format = gpu::Format::RGBA8;
  mismatchedFormatDesc.usage = gpu::TextureUsageCopyDestination;
  mismatchedFormatDesc.initialData = {};
  const gpu::TextureHandle mismatchedFormat =
      device->createTexture(mismatchedFormatDesc);
  assert(mismatchedFormat.valid());
  assert(!device->copyTexture(mismatchedFormat, copyOrigin, copySource,
                              copyRegion));
  device->clearErrors();
  device->destroy(mismatchedFormat);

  device->destroy(copyDestination);
  device->destroy(copySource);
  assertNoErrors(*device);

  gpu::PipelineHandle computeImagePipeline;
  gpu::TextureHandle storageTexture;
  gpu::PipelineHandle computeBufferPipeline;
  gpu::BufferHandle storageBuffer;
  if (capabilities.compute && capabilities.storageTextures) {
    static const char imageComputeSource[] =
        "#version 430 core\n"
        "layout(local_size_x=2, local_size_y=2) in;\n"
        "layout(rgba8, binding=0) writeonly uniform image2D dst;\n"
        "void main() {\n"
        "  ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "  imageStore(dst, p, vec4(1.0, 0.0, 0.0, 1.0));\n"
        "}\n";
    gpu::PipelineDesc computeImageDesc;
    computeImageDesc.compute.source = {imageComputeSource,
                                       std::strlen(imageComputeSource)};
    computeImagePipeline = device->createPipeline(computeImageDesc);
    assert(computeImagePipeline.valid());

    gpu::TextureDesc storageTextureDesc;
    storageTextureDesc.format = gpu::Format::RGBA8;
    storageTextureDesc.width = 4;
    storageTextureDesc.height = 4;
    storageTextureDesc.usage =
        gpu::TextureUsageStorage | gpu::TextureUsageCopySource;
    storageTexture = device->createTexture(storageTextureDesc);
    assert(storageTexture.valid());
    assertNoErrors(*device);

    assert(device->setPipeline(computeImagePipeline));
    assert(device->bindStorageTexture(0, storageTexture, 0));
    assert(device->dispatch(2, 2, 1));
    assert(device->memoryBarrier(gpu::BarrierTexture));
    assertNoErrors(*device);

    std::uint8_t imagePixels[64] = {};
    gpu::TextureRegion imageRegion;
    imageRegion.width = 4;
    imageRegion.height = 4;
    assert(device->readTexture(storageTexture, imageRegion,
                               {imagePixels, sizeof(imagePixels)}));
    assertNoErrors(*device);
    std::uint8_t expectedImagePixel[4] = {255, 0, 0, 255};
    for (std::uint32_t texel = 0; texel < 16; ++texel)
      assert(std::memcmp(imagePixels + texel * 4, expectedImagePixel, 4) == 0);

    assert(!device->dispatch(0, 1, 1));
    device->clearErrors();
  }

  if (capabilities.compute && capabilities.storageBuffers) {
    static const char bufferComputeSource[] =
        "#version 430 core\n"
        "layout(local_size_x=4) in;\n"
        "layout(std430, binding=0) buffer Values { uint values[]; };\n"
        "void main() {\n"
        "  values[gl_GlobalInvocationID.x] = gl_GlobalInvocationID.x;\n"
        "}\n";
    gpu::PipelineDesc computeBufferDesc;
    computeBufferDesc.compute.source = {bufferComputeSource,
                                        std::strlen(bufferComputeSource)};
    computeBufferPipeline = device->createPipeline(computeBufferDesc);
    assert(computeBufferPipeline.valid());

    gpu::BufferDesc storageBufferDesc;
    storageBufferDesc.size = 64;
    storageBufferDesc.usage = gpu::BufferUsageStorage;
    storageBuffer = device->createBuffer(storageBufferDesc);
    assert(storageBuffer.valid());

    assert(device->setPipeline(computeBufferPipeline));
    assert(device->bindStorageBuffer(0, storageBuffer, 0,
                                     storageBufferDesc.size));
    assert(device->dispatch(1, 1, 1));
    assert(device->memoryBarrier(gpu::BarrierStorage));
    assertNoErrors(*device);
  }

  if (computeImagePipeline.valid())
    device->destroy(computeImagePipeline);
  if (storageTexture.valid())
    device->destroy(storageTexture);
  if (computeBufferPipeline.valid())
    device->destroy(computeBufferPipeline);
  if (storageBuffer.valid())
    device->destroy(storageBuffer);
  assertNoErrors(*device);

  gpu::BufferDesc stagingBufferDesc;
  stagingBufferDesc.size = 32;
  stagingBufferDesc.usage = gpu::BufferUsageStaging;
  const gpu::BufferHandle stagingBuffer = device->createBuffer(stagingBufferDesc);
  assert(stagingBuffer.valid());
  gpu::BufferDesc readbackBufferDesc;
  readbackBufferDesc.size = 32;
  readbackBufferDesc.usage = gpu::BufferUsageReadback;
  const gpu::BufferHandle readbackBuffer =
      device->createBuffer(readbackBufferDesc);
  assert(readbackBuffer.valid());

  std::uint8_t mapWritePattern[32];
  for (std::uint32_t index = 0; index < sizeof(mapWritePattern); ++index)
    mapWritePattern[index] = static_cast<std::uint8_t>(index * 7 + 1);
  void *writePointer =
      device->mapBuffer(stagingBuffer, 0, sizeof(mapWritePattern),
                        gpu::MapMode::Write);
  assert(writePointer != nullptr);
  std::memcpy(writePointer, mapWritePattern, sizeof(mapWritePattern));
  assert(device->unmapBuffer(stagingBuffer));
  assertNoErrors(*device);

  assert(device->copyBuffer(readbackBuffer, 0, stagingBuffer, 0,
                            sizeof(mapWritePattern)));
  assertNoErrors(*device);

  if (capabilities.asyncReadback) {
    const gpu::FenceHandle copyFence = device->insertFence();
    assert(copyFence.valid());
    while (!device->isFenceSignaled(copyFence)) {
    }
    assert(device->isFenceSignaled(copyFence));
    device->destroy(copyFence);
    assertNoErrors(*device);
  }

  void *readPointer = device->mapBuffer(readbackBuffer, 0,
                                        sizeof(mapWritePattern),
                                        gpu::MapMode::Read);
  assert(readPointer != nullptr);
  assert(std::memcmp(readPointer, mapWritePattern, sizeof(mapWritePattern)) ==
        0);
  assert(device->unmapBuffer(readbackBuffer));
  assertNoErrors(*device);

  device->destroy(readbackBuffer);
  device->destroy(stagingBuffer);
  assertNoErrors(*device);

  static const char indirectVertexSource[] =
      "#version 330 core\n"
      "void main() {\n"
      "  vec2 p[3] = vec2[3](vec2(-1.0,-1.0), vec2(3.0,-1.0), vec2(-1.0,3.0));\n"
      "  gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);\n"
      "}\n";
  static const char indirectFragmentSource[] =
      "#version 330 core\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color = vec4(0.0, 1.0, 0.0, 1.0); }\n";
  gpu::PipelineDesc indirectPipelineDesc;
  indirectPipelineDesc.vertex.source = {indirectVertexSource,
                                        std::strlen(indirectVertexSource)};
  indirectPipelineDesc.fragment.source = {indirectFragmentSource,
                                          std::strlen(indirectFragmentSource)};
  indirectPipelineDesc.raster.cullMode = gpu::CullMode::None;
  const gpu::PipelineHandle indirectPipeline =
      device->createPipeline(indirectPipelineDesc);
  assert(indirectPipeline.valid());

  gpu::TextureDesc indirectTargetDesc;
  indirectTargetDesc.format = gpu::Format::RGBA8;
  indirectTargetDesc.width = 16;
  indirectTargetDesc.height = 16;
  indirectTargetDesc.usage = gpu::TextureUsageRenderTarget |
                             gpu::TextureUsageCopySource;
  const gpu::TextureHandle indirectTarget =
      device->createTexture(indirectTargetDesc);
  assert(indirectTarget.valid());

  gpu::DrawIndirectArgs indirectArgs;
  indirectArgs.vertexCount = 3;
  gpu::BufferDesc indirectBufferDesc;
  indirectBufferDesc.size = sizeof(indirectArgs);
  indirectBufferDesc.usage = gpu::BufferUsageIndirect;
  indirectBufferDesc.initialData = {&indirectArgs, sizeof(indirectArgs)};
  const gpu::BufferHandle indirectBuffer =
      device->createBuffer(indirectBufferDesc);
  assert(indirectBuffer.valid());
  assertNoErrors(*device);

  gpu::RenderPassDesc indirectPass;
  indirectPass.colorCount = 1;
  indirectPass.colors[0].target.texture = indirectTarget;
  indirectPass.colors[0].loadOp = gpu::LoadOp::Clear;
  {
    gpu::RenderPassScope scope(*device, indirectPass);
    assert(scope);
    assert(device->setPipeline(indirectPipeline));
    assert(device->drawIndirect(indirectBuffer, 0));
  }
  assertNoErrors(*device);

  std::uint8_t indirectPixels[4] = {};
  gpu::TextureRegion indirectRegion;
  indirectRegion.width = 1;
  indirectRegion.height = 1;
  assert(device->readTexture(indirectTarget, indirectRegion,
                             {indirectPixels, sizeof(indirectPixels)}));
  const std::uint8_t expectedIndirectPixel[4] = {0, 255, 0, 255};
  assert(std::memcmp(indirectPixels, expectedIndirectPixel, 4) == 0);

  const std::uint16_t indirectIndices[] = {0, 1, 2};
  gpu::BufferDesc indirectIndexDesc;
  indirectIndexDesc.size = sizeof(indirectIndices);
  indirectIndexDesc.usage = gpu::BufferUsageIndex;
  indirectIndexDesc.initialData = {indirectIndices, sizeof(indirectIndices)};
  const gpu::BufferHandle indirectIndexBuffer =
      device->createBuffer(indirectIndexDesc);
  assert(indirectIndexBuffer.valid());

  gpu::DrawIndexedIndirectArgs indexedIndirectArgs;
  indexedIndirectArgs.indexCount = 3;
  gpu::BufferDesc indexedIndirectBufferDesc;
  indexedIndirectBufferDesc.size = sizeof(indexedIndirectArgs);
  indexedIndirectBufferDesc.usage = gpu::BufferUsageIndirect;
  indexedIndirectBufferDesc.initialData = {&indexedIndirectArgs,
                                           sizeof(indexedIndirectArgs)};
  const gpu::BufferHandle indexedIndirectBuffer =
      device->createBuffer(indexedIndirectBufferDesc);
  assert(indexedIndirectBuffer.valid());
  assertNoErrors(*device);

  gpu::RenderPassDesc indexedIndirectPass;
  indexedIndirectPass.colorCount = 1;
  indexedIndirectPass.colors[0].target.texture = indirectTarget;
  indexedIndirectPass.colors[0].loadOp = gpu::LoadOp::Clear;
  assert(device->beginRenderPass(indexedIndirectPass));
  assert(device->setPipeline(indirectPipeline));
  assert(device->bindIndexBuffer(indirectIndexBuffer, gpu::IndexFormat::Uint16,
                                 0));
  assert(device->drawIndexedIndirect(indexedIndirectBuffer, 0));
  device->endRenderPass();
  assertNoErrors(*device);

  std::uint8_t indexedIndirectPixel[4] = {};
  assert(device->readTexture(indirectTarget, indirectRegion,
                             {indexedIndirectPixel,
                              sizeof(indexedIndirectPixel)}));
  assert(std::memcmp(indexedIndirectPixel, expectedIndirectPixel, 4) == 0);

  if (capabilities.occlusionQueries && capabilities.timestampQueries) {
    const gpu::QueryHandle occlusionQuery =
        device->createQuery(gpu::QueryType::Occlusion);
    assert(occlusionQuery.valid());
    const gpu::QueryHandle timestampQuery =
        device->createQuery(gpu::QueryType::Timestamp);
    assert(timestampQuery.valid());
    assertNoErrors(*device);

    gpu::RenderPassDesc queryPass;
    queryPass.colorCount = 1;
    queryPass.colors[0].target.texture = indirectTarget;
    queryPass.colors[0].loadOp = gpu::LoadOp::Clear;
    assert(device->beginRenderPass(queryPass));
    assert(device->beginQuery(occlusionQuery));
    assert(device->setPipeline(indirectPipeline));
    assert(device->draw(3));
    device->endQuery(occlusionQuery);
    device->endRenderPass();
    assert(device->writeTimestamp(timestampQuery));
    assertNoErrors(*device);

    waitForGPU(*device);
    std::uint64_t occlusionResult = 0;
    while (!device->getQueryResult(occlusionQuery, occlusionResult)) {
    }
    assert(occlusionResult > 0);

    std::uint64_t timestampResult = 0;
    while (!device->getQueryResult(timestampQuery, timestampResult)) {
    }
    assert(timestampResult > 0);
    assertNoErrors(*device);

    device->destroy(occlusionQuery);
    device->destroy(timestampQuery);
    assertNoErrors(*device);
  }

  if (capabilities.timestampQueries) {
    gpu::GPUProfiler profiler(*device);
    assert(profiler.enabled());

    gpu::RenderPassDesc profilerPass;
    profilerPass.colorCount = 1;
    profilerPass.colors[0].target.texture = indirectTarget;
    profilerPass.colors[0].loadOp = gpu::LoadOp::Clear;

    profiler.beginFrame();
    assert(device->beginRenderPass(profilerPass));
    assert(profiler.beginScope("triangle"));
    assert(device->setPipeline(indirectPipeline));
    assert(device->draw(3));
    profiler.endScope();
    device->endRenderPass();
    profiler.endFrame();
    assertNoErrors(*device);

    for (std::uint32_t frame = 0; frame + 1 < gpu::GPUProfiler::FrameLatency;
        ++frame) {
      profiler.beginFrame();
      profiler.endFrame();
    }

    waitForGPU(*device);
    gpu::ProfilerScopeResult profilerResults[4];
    std::uint32_t profilerCount = 0;
    while (profilerCount == 0)
      profilerCount = profiler.collectFrame(profilerResults, 4);
    assert(profilerCount == 1);
    assert(std::strcmp(profilerResults[0].name, "triangle") == 0);
    assert(profilerResults[0].milliseconds >= 0.0);
    assertNoErrors(*device);
  }

  if (capabilities.indirectCount) {
    gpu::BufferDesc countableIndirectDesc;
    countableIndirectDesc.size = sizeof(gpu::DrawIndirectArgs);
    countableIndirectDesc.usage = gpu::BufferUsageIndirect;
    countableIndirectDesc.initialData = {&indirectArgs, sizeof(indirectArgs)};
    const gpu::BufferHandle countableIndirectBuffer =
        device->createBuffer(countableIndirectDesc);
    assert(countableIndirectBuffer.valid());

    std::uint32_t zeroCount = 0;
    gpu::BufferDesc countBufferDesc;
    countBufferDesc.size = sizeof(zeroCount);
    countBufferDesc.usage = gpu::BufferUsageIndirect;
    countBufferDesc.initialData = {&zeroCount, sizeof(zeroCount)};
    const gpu::BufferHandle countBuffer = device->createBuffer(countBufferDesc);
    assert(countBuffer.valid());
    assertNoErrors(*device);

    gpu::RenderPassDesc countPass;
    countPass.colorCount = 1;
    countPass.colors[0].target.texture = indirectTarget;
    countPass.colors[0].loadOp = gpu::LoadOp::Clear;
    assert(device->beginRenderPass(countPass));
    assert(device->setPipeline(indirectPipeline));
    assert(device->drawIndirectCount(countableIndirectBuffer, 0, countBuffer,
                                     0, 1, sizeof(gpu::DrawIndirectArgs)));
    device->endRenderPass();
    assertNoErrors(*device);

    std::uint8_t zeroDrawPixel[4] = {};
    assert(device->readTexture(indirectTarget, indirectRegion,
                               {zeroDrawPixel, sizeof(zeroDrawPixel)}));
    const std::uint8_t expectedClearedPixel[4] = {0, 0, 0, 0};
    assert(std::memcmp(zeroDrawPixel, expectedClearedPixel, 4) == 0);

    const std::uint32_t oneCount = 1;
    assert(device->updateBuffer(countBuffer, 0, {&oneCount, sizeof(oneCount)}));

    assert(device->beginRenderPass(countPass));
    assert(device->setPipeline(indirectPipeline));
    assert(device->drawIndirectCount(countableIndirectBuffer, 0, countBuffer,
                                     0, 1, sizeof(gpu::DrawIndirectArgs)));
    device->endRenderPass();
    assertNoErrors(*device);

    std::uint8_t oneDrawPixel[4] = {};
    assert(device->readTexture(indirectTarget, indirectRegion,
                               {oneDrawPixel, sizeof(oneDrawPixel)}));
    assert(std::memcmp(oneDrawPixel, expectedIndirectPixel, 4) == 0);

    device->destroy(countBuffer);
    device->destroy(countableIndirectBuffer);
    assertNoErrors(*device);
  }

  device->destroy(indirectIndexBuffer);
  device->destroy(indexedIndirectBuffer);
  device->destroy(indirectBuffer);
  device->destroy(indirectTarget);
  device->destroy(indirectPipeline);
  assertNoErrors(*device);

  {
    GLint driverAlignment = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &driverAlignment);
    assert(device->capabilities().uniformBufferOffsetAlignment ==
           static_cast<std::uint32_t>(driverAlignment));

    gpu::SamplerDesc samplerDesc;
    samplerDesc.minFilter = gpu::Filter::Linear;
    samplerDesc.magFilter = gpu::Filter::Nearest;
    samplerDesc.mipFilter = gpu::Filter::Linear;
    samplerDesc.addressU = gpu::AddressMode::MirrorRepeat;
    samplerDesc.addressV = gpu::AddressMode::ClampToEdge;
    samplerDesc.addressW = gpu::AddressMode::ClampToBorder;
    samplerDesc.lodMin = 2.0f;
    samplerDesc.lodMax = 5.0f;
    samplerDesc.debugName = "gpu test sampler";
    const gpu::SamplerHandle configured = device->createSampler(samplerDesc);
    assert(configured.valid());
    assertNoErrors(*device);

    GLint sampled = 0;
    GLuint probe = 0;
    glGenSamplers(1, &probe);
    const GLuint programmed = probe - 1;
    glDeleteSamplers(1, &probe);
    glGetSamplerParameteriv(programmed, GL_TEXTURE_MIN_FILTER, &sampled);
    assert(sampled == GL_LINEAR_MIPMAP_LINEAR);
    glGetSamplerParameteriv(programmed, GL_TEXTURE_MAG_FILTER, &sampled);
    assert(sampled == GL_NEAREST);
    glGetSamplerParameteriv(programmed, GL_TEXTURE_WRAP_S, &sampled);
    assert(sampled == GL_MIRRORED_REPEAT);
    glGetSamplerParameteriv(programmed, GL_TEXTURE_WRAP_T, &sampled);
    assert(sampled == GL_CLAMP_TO_EDGE);
    glGetSamplerParameteriv(programmed, GL_TEXTURE_WRAP_R, &sampled);
    assert(sampled == GL_CLAMP_TO_BORDER);
    glGetSamplerParameteriv(programmed, GL_TEXTURE_MIN_LOD, &sampled);
    assert(sampled == 2);
    glGetSamplerParameteriv(programmed, GL_TEXTURE_MAX_LOD, &sampled);
    assert(sampled == 5);
    if (GLAD_GL_VERSION_4_3 || GLAD_GL_KHR_debug) {
      char label[64] = {};
      GLsizei labelLength = 0;
      glGetObjectLabel(GL_SAMPLER, programmed, sizeof(label), &labelLength,
                       label);
      assert(std::strcmp(label, "gpu test sampler") == 0);
    }
    device->destroy(configured);

    gpu::DeviceDesc noSurface;
    noSurface.backend = gpu::Backend::OpenGL;
    gpu::GPUError creationError;
    assert(gpu::createDevice(noSurface, &creationError) == nullptr);
    assert(creationError.code == gpu::GPUErrorCode::InvalidArgument);
    assert(creationError.operation == gpu::GPUOperation::CreateDevice);
    assert(creationError.message != nullptr);

    gpu::SamplerDesc invalidLod;
    invalidLod.lodMin = 4.0f;
    invalidLod.lodMax = 1.0f;
    assert(!device->createSampler(invalidLod).valid());
    assert(device->pendingErrorCount() == 1);
    device->clearErrors();

    const char brokenA[] =
        "#version 330 core\nvoid main(){ MARKER_AAA; }\n";
    const char brokenB[] =
        "#version 330 core\nvoid main(){ MARKER_BBB; }\n";
    gpu::PipelineDesc brokenDesc;
    brokenDesc.vertex.source = {brokenA, sizeof(brokenA) - 1};
    brokenDesc.fragment.source = {brokenA, sizeof(brokenA) - 1};
    brokenDesc.colorTargets[0].format = gpu::Format::RGBA8;
    assert(!device->createPipeline(brokenDesc).valid());
    brokenDesc.vertex.source = {brokenB, sizeof(brokenB) - 1};
    brokenDesc.fragment.source = {brokenB, sizeof(brokenB) - 1};
    assert(!device->createPipeline(brokenDesc).valid());
    gpu::GPUError firstFailure;
    gpu::GPUError secondFailure;
    assert(device->getError(firstFailure));
    assert(device->getError(secondFailure));
    assert(firstFailure.code == gpu::GPUErrorCode::ShaderCompilationFailed);
    assert(secondFailure.code == gpu::GPUErrorCode::ShaderCompilationFailed);
    assert(firstFailure.message != nullptr);
    assert(secondFailure.message != nullptr);
    assert(std::strstr(firstFailure.message, "MARKER_AAA") != nullptr);
    assert(std::strstr(secondFailure.message, "MARKER_BBB") != nullptr);
    device->clearErrors();

    gpu::PipelineDesc entryPointDesc;
    const char trivial[] =
        "#version 330 core\nvoid main(){ gl_Position = vec4(0); }\n";
    entryPointDesc.vertex.source = {trivial, sizeof(trivial) - 1};
    entryPointDesc.vertex.entryPoint = "vertexMain";
    entryPointDesc.fragment.source = {trivial, sizeof(trivial) - 1};
    entryPointDesc.colorTargets[0].format = gpu::Format::RGBA8;
    assert(!device->createPipeline(entryPointDesc).valid());
    gpu::GPUError entryPointError;
    assert(device->getError(entryPointError));
    assert(entryPointError.code == gpu::GPUErrorCode::UnsupportedFeature);
    device->clearErrors();
  }

  {
    gpu::TextureDesc cacheDesc;
    cacheDesc.format = gpu::Format::RGBA8;
    cacheDesc.width = 4;
    cacheDesc.height = 4;
    cacheDesc.usage =
        gpu::TextureUsageRenderTarget | gpu::TextureUsageCopySource;
    cacheDesc.debugName = "framebuffer cache probe";

    std::uint8_t pixel[4] = {};
    const auto clearAndRead = [&](gpu::TextureHandle texture, float red) {
      gpu::RenderPassDesc pass;
      pass.colorCount = 1;
      pass.colors[0].target.texture = texture;
      pass.colors[0].loadOp = gpu::LoadOp::Clear;
      pass.colors[0].clearColor[0] = red;
      pass.colors[0].clearColor[3] = 1.0f;
      assert(device->beginRenderPass(pass));
      device->endRenderPass();
      gpu::TextureRegion region;
      region.width = 1;
      region.height = 1;
      assert(device->readTexture(texture, region, {pixel, sizeof(pixel)}));
    };

    const gpu::TextureHandle cacheA = device->createTexture(cacheDesc);
    const gpu::TextureHandle cacheB = device->createTexture(cacheDesc);
    assert(cacheA.valid() && cacheB.valid());
    for (int round = 0; round < 3; ++round) {
      clearAndRead(cacheA, 1.0f);
      assert(pixel[0] == 255 && pixel[3] == 255);
      clearAndRead(cacheB, 0.0f);
      assert(pixel[0] == 0 && pixel[3] == 255);
    }
    assertNoErrors(*device);

    device->destroy(cacheA);
    const gpu::TextureHandle recycled = device->createTexture(cacheDesc);
    assert(recycled.valid());
    clearAndRead(recycled, 0.0f);
    assert(pixel[0] == 0 && pixel[3] == 255);
    clearAndRead(recycled, 1.0f);
    assert(pixel[0] == 255 && pixel[3] == 255);
    assertNoErrors(*device);
    device->destroy(recycled);

    gpu::TextureHandle overflow[24];
    for (std::uint32_t index = 0; index < 24; ++index) {
      overflow[index] = device->createTexture(cacheDesc);
      assert(overflow[index].valid());
      clearAndRead(overflow[index], 1.0f);
      assert(pixel[0] == 255 && pixel[3] == 255);
    }
    clearAndRead(cacheB, 0.0f);
    assert(pixel[0] == 0 && pixel[3] == 255);
    for (std::uint32_t index = 0; index < 24; ++index)
      device->destroy(overflow[index]);
    device->destroy(cacheB);
    assertNoErrors(*device);
  }

  {
    // This backend never compiles tessellation/geometry stages - a
    // descriptor requesting them (or Topology::Patches on its own) must be
    // rejected outright rather than silently linking a vertex+fragment-only
    // pipeline, matching the Vulkan backend's validation for the same
    // fields.
    gpu::PipelineDesc unsupportedDesc = pipelineDesc;
    unsupportedDesc.tessControl.source = {vertexSource, std::strlen(vertexSource)};
    assert(!device->createPipeline(unsupportedDesc).valid());
    assert(takeError(*device).code == gpu::GPUErrorCode::UnsupportedFeature);

    gpu::PipelineDesc patchesDesc = pipelineDesc;
    patchesDesc.topology = gpu::Topology::Patches;
    assert(!device->createPipeline(patchesDesc).valid());
    assert(takeError(*device).code == gpu::GPUErrorCode::UnsupportedFeature);
  }

  device->destroy(arrayTexture);
  device->destroy(pipeline);
  device->destroy(depth);
  device->destroy(color1);
  device->destroy(color0);
  device->destroy(gridTexture);
  for (std::uint32_t index = 0; index < compressedCount; ++index)
    device->destroy(compressedTextures[index]);
  assertNoErrors(*device);
  runRenderPathProbe(*device, false, "OpenGL desktop");

  gpu::destroyDevice(device);
  destroySDLGLSurface(surface);
  SDL_Quit();
  return 0;
}
