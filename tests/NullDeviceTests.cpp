#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"
#include "RenderPathProbe.h"

#include <cassert>

namespace {

void testLifecycle() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);

  gpu::BufferDesc invalidBuffer;
  assert(!device->createBuffer(invalidBuffer).valid());
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  gpu::BufferDesc bufferDesc;
  bufferDesc.size = 64;
  bufferDesc.usage = gpu::BufferUsageVertex;
  const gpu::BufferHandle buffer = device->createBuffer(bufferDesc);
  assert(buffer.valid());
  device->destroy(buffer);
  device->destroy(buffer);
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidHandle);

  gpu::TextureDesc textureDesc;
  textureDesc.width = 16;
  textureDesc.height = 16;
  textureDesc.usage = gpu::TextureUsageSampled | gpu::TextureUsageRenderTarget;
  const gpu::TextureHandle texture = device->createTexture(textureDesc);
  assert(texture.valid());

  const char shaderSource[] = "void main() {}";
  gpu::PipelineDesc pipelineDesc;
  pipelineDesc.vertex.source.data = shaderSource;
  pipelineDesc.vertex.source.size = sizeof(shaderSource) - 1;
  pipelineDesc.fragment.source.data = shaderSource;
  pipelineDesc.fragment.source.size = sizeof(shaderSource) - 1;
  const gpu::PipelineHandle pipeline = device->createPipeline(pipelineDesc);
  assert(pipeline.valid());

  gpu::RenderPassDesc pass;
  pass.colorCount = 1;
  pass.colors[0].target.texture = texture;
  assert(device->beginRenderPass(pass));
  assert(device->setPipeline(pipeline));
  assert(device->draw(3));
  device->endRenderPass();
  assert(device->present());

  device->shutdown();
  device->shutdown();
  assert(!device->createBuffer(bufferDesc).valid());
  assert(takeError(*device).code == gpu::GPUErrorCode::DeviceLost);
  gpu::destroyDevice(device);
}

void testCapabilities() {
  gpu::DeviceDesc descriptor;
  descriptor.requiredCapabilities.compute = true;
  assert(gpu::createDevice(descriptor) == nullptr);

  descriptor.profile = gpu::RendererProfile::Modern;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(device->capabilities().compute);
  gpu::destroyDevice(device);

  gpu::DeviceDesc compressedDescriptor;
  compressedDescriptor.requiredCapabilities.textureCompressionBC7 = true;
  assert(gpu::createDevice(compressedDescriptor) == nullptr);
}

void testInvalidStateAndReuse() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);

  device->endRenderPass();
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  gpu::TextureDesc textureDesc;
  textureDesc.width = 4;
  textureDesc.height = 4;
  textureDesc.usage = gpu::TextureUsageSampled | gpu::TextureUsageRenderTarget;
  const gpu::TextureHandle texture = device->createTexture(textureDesc);
  gpu::RenderPassDesc pass;
  pass.colorCount = 1;
  pass.colors[0].target.texture = texture;
  assert(device->beginRenderPass(pass));
  assert(!device->beginRenderPass(pass));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);
  assert(!device->present());
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);
  device->endRenderPass();

  gpu::BufferDesc bufferDesc;
  bufferDesc.size = 1;
  bufferDesc.usage = gpu::BufferUsageVertex;
  gpu::BufferHandle handles[1024];
  for (gpu::BufferHandle &handle : handles) {
    handle = device->createBuffer(bufferDesc);
    assert(handle.valid());
  }
  const gpu::BufferHandle stale = handles[0];
  device->destroy(stale);
  const gpu::BufferHandle replacement = device->createBuffer(bufferDesc);
  assert(replacement.valid());
  assert(replacement != stale);
  device->destroy(stale);
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidHandle);
  gpu::destroyDevice(device);
}

void testResourcePoolGrowth() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  gpu::TextureDesc textureDesc;
  textureDesc.width = 1;
  textureDesc.height = 1;
  gpu::TextureHandle textures[1024];
  for (gpu::TextureHandle &texture : textures) {
    texture = device->createTexture(textureDesc);
    assert(texture.valid());
  }
  assert(device->pendingErrorCount() == 0);
  for (gpu::TextureHandle texture : textures)
    device->destroy(texture);
  gpu::destroyDevice(device);
}

void testPortableCommands() {
  gpu::DeviceDesc descriptor;
  descriptor.surface.width = 640;
  descriptor.surface.height = 480;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);

  const float vertices[] = {0.0f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f};
  const std::uint16_t indices[] = {0, 1, 2};
  gpu::BufferDesc vertexDesc;
  vertexDesc.size = sizeof(vertices);
  vertexDesc.usage = gpu::BufferUsageVertex;
  vertexDesc.initialData = {vertices, sizeof(vertices)};
  gpu::BufferDesc indexDesc;
  indexDesc.size = sizeof(indices);
  indexDesc.usage = gpu::BufferUsageIndex;
  indexDesc.initialData = {indices, sizeof(indices)};
  gpu::BufferDesc uniformDesc;
  uniformDesc.size = 256;
  uniformDesc.usage = gpu::BufferUsageUniform;
  gpu::BufferDesc copyDesc;
  copyDesc.size = sizeof(vertices);
  copyDesc.usage = gpu::BufferUsageVertex;
  const gpu::BufferHandle vertex = device->createBuffer(vertexDesc);
  const gpu::BufferHandle index = device->createBuffer(indexDesc);
  const gpu::BufferHandle uniform = device->createBuffer(uniformDesc);
  const gpu::BufferHandle copy = device->createBuffer(copyDesc);
  assert(vertex && index && uniform && copy);
  assert(device->updateBuffer(copy, 0, {vertices, sizeof(vertices)}));
  assert(device->copyBuffer(vertex, 0, copy, 0, sizeof(vertices)));
  assert(!device->copyBuffer(vertex, sizeof(vertices), copy, 0, 1));
  assert(takeError(*device).code == gpu::GPUErrorCode::OutOfBounds);

  gpu::TextureDesc textureDesc;
  textureDesc.width = 4;
  textureDesc.height = 4;
  const gpu::TextureHandle texture = device->createTexture(textureDesc);
  const gpu::SamplerHandle sampler = device->createSampler({});
  assert(device->capabilities().anisotropicFiltering);
  assert(device->capabilities().maxAnisotropy >= 1.0f);
  gpu::SamplerDesc anisotropicDesc;
  anisotropicDesc.maxAnisotropy = device->capabilities().maxAnisotropy * 4.0f;
  const gpu::SamplerHandle anisotropic = device->createSampler(anisotropicDesc);
  assert(anisotropic);
  assert(device->pendingErrorCount() == 0);
  device->destroy(anisotropic);

  const char shader[] = "void main() {}";
  gpu::PipelineDesc pipelineDesc;
  pipelineDesc.vertex.source = {shader, sizeof(shader) - 1};
  pipelineDesc.fragment.source = {shader, sizeof(shader) - 1};
  pipelineDesc.vertexBufferCount = 1;
  pipelineDesc.vertexBuffers[0].stride = sizeof(float) * 2;
  pipelineDesc.vertexBuffers[0].attributeCount = 1;
  pipelineDesc.vertexBuffers[0].attributes[0].format =
      gpu::VertexFormat::Float32x2;
  pipelineDesc.depthStencil.stencilEnabled = true;
  const gpu::PipelineHandle pipeline = device->createPipeline(pipelineDesc);
  assert(texture && sampler && pipeline);

  gpu::RenderPassDesc pass;
  pass.colorCount = 1;
  pass.colors[0].surface = true;
  assert(device->beginRenderPass(pass));
  assert(device->setPipeline(pipeline));
  assert(device->setViewport({0, 0, 640, 480, 0, 1}));
  assert(device->setScissor({0, 0, 640, 480}));
  assert(device->setStencilReference(1));
  assert(device->bindVertexBuffer(0, vertex, 0));
  assert(device->bindIndexBuffer(index, gpu::IndexFormat::Uint16, 0));
  assert(device->bindUniformBuffer(0, uniform, 0, 256));
  assert(device->bindTexture(0, texture, sampler));
  assert(device->drawIndexed(3));
  device->endRenderPass();
  assert(device->present());
  device->suspendSurface();
  assert(device->surfaceState() == gpu::SurfaceState::Suspended);
  assert(!device->present());
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);
  assert(device->resumeSurface());
  assert(device->resizeSurface(800, 600));
  assert(device->present());

  device->destroy(pipeline);
  device->destroy(sampler);
  device->destroy(texture);
  device->destroy(copy);
  device->destroy(uniform);
  device->destroy(index);
  device->destroy(vertex);
  gpu::destroyDevice(device);
}

void testIntegerFormatsAndTargets() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);

  const std::uint16_t rData[16] = {};
  gpu::TextureDesc r16;
  r16.format = gpu::Format::R16Uint;
  r16.width = 4;
  r16.height = 4;
  r16.initialData = {rData, sizeof(rData)};
  const gpu::TextureHandle sampled = device->createTexture(r16);
  assert(sampled.valid());

  gpu::TextureDesc updateDesc = r16;
  updateDesc.initialData = {};
  updateDesc.usage =
      gpu::TextureUsageSampled | gpu::TextureUsageCopyDestination;
  const gpu::TextureHandle updateTexture = device->createTexture(updateDesc);
  assert(updateTexture.valid());
  const std::uint8_t updateBytes[16] = {};
  gpu::TextureRegion updateRegion;
  updateRegion.x = 1;
  updateRegion.y = 1;
  updateRegion.width = 2;
  updateRegion.height = 2;
  gpu::TextureDataLayout updateLayout;
  updateLayout.offset = 2;
  updateLayout.bytesPerRow = 8;
  updateLayout.rowsPerImage = 2;
  assert(device->updateTexture(updateTexture, updateRegion,
                               {updateBytes, sizeof(updateBytes)}, updateLayout));
  assert(!device->updateTexture(updateTexture, updateRegion,
                                {updateBytes, 8}, updateLayout));
  assert(takeError(*device).code == gpu::GPUErrorCode::OutOfBounds);
  assert(!device->updateTexture(sampled, updateRegion,
                                {updateBytes, sizeof(updateBytes)}, updateLayout));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  const std::uint8_t bc1Block[8] = {};
  gpu::TextureDesc compressed;
  compressed.format = gpu::Format::BC1RGBA;
  compressed.width = 4;
  compressed.height = 4;
  compressed.initialData = {bc1Block, sizeof(bc1Block)};
  assert(!device->createTexture(compressed).valid());
  assert(takeError(*device).code == gpu::GPUErrorCode::UnsupportedFormat);

  gpu::TextureDesc color = r16;
  color.initialData = {};
  color.usage = gpu::TextureUsageSampled | gpu::TextureUsageRenderTarget;
  const gpu::TextureHandle color0 = device->createTexture(color);
  color.format = gpu::Format::RG16Uint;
  const gpu::TextureHandle color1 = device->createTexture(color);

  gpu::TextureDesc depth;
  depth.format = gpu::Format::Depth24Stencil8;
  depth.width = 4;
  depth.height = 4;
  depth.usage = gpu::TextureUsageRenderTarget;
  const gpu::TextureHandle depthTexture = device->createTexture(depth);
  assert(color0 && color1 && depthTexture);

  gpu::RenderPassDesc pass;
  pass.colorCount = 2;
  pass.colors[0].target.texture = color0;
  pass.colors[1].target.texture = color1;
  pass.hasDepthStencil = true;
  pass.depthStencil.target.texture = depthTexture;
  assert(device->beginRenderPass(pass));
  device->endRenderPass();

  gpu::RenderPassDesc invalidPass;
  invalidPass.colorCount = 1;
  invalidPass.colors[0].target.texture = sampled;
  assert(!device->beginRenderPass(invalidPass));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  device->destroy(depthTexture);
  device->destroy(color1);
  device->destroy(color0);
  device->destroy(sampled);
  device->destroy(updateTexture);
  gpu::destroyDevice(device);
}

void testCopyAndReadTexture() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);

  gpu::TextureDesc sourceDesc;
  sourceDesc.format = gpu::Format::R8;
  sourceDesc.width = 4;
  sourceDesc.height = 4;
  sourceDesc.usage = gpu::TextureUsageCopySource;
  const gpu::TextureHandle source = device->createTexture(sourceDesc);
  assert(source.valid());

  gpu::TextureDesc destinationDesc = sourceDesc;
  destinationDesc.usage =
      gpu::TextureUsageCopyDestination | gpu::TextureUsageCopySource;
  const gpu::TextureHandle destination = device->createTexture(destinationDesc);
  assert(destination.valid());

  gpu::TextureDesc notReadableDesc = sourceDesc;
  notReadableDesc.usage = gpu::TextureUsageSampled;
  const gpu::TextureHandle notReadable = device->createTexture(notReadableDesc);
  assert(notReadable.valid());

  gpu::TextureRegion copyRegion;
  copyRegion.x = 1;
  copyRegion.y = 1;
  copyRegion.width = 2;
  copyRegion.height = 2;
  gpu::TextureOrigin copyOrigin;
  assert(device->copyTexture(destination, copyOrigin, source, copyRegion));

  assert(!device->copyTexture(destination, copyOrigin, notReadable, copyRegion));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  gpu::TextureRegion outOfBoundsRegion;
  outOfBoundsRegion.x = 3;
  outOfBoundsRegion.y = 3;
  outOfBoundsRegion.width = 2;
  outOfBoundsRegion.height = 2;
  assert(!device->copyTexture(destination, copyOrigin, source, outOfBoundsRegion));
  assert(takeError(*device).code == gpu::GPUErrorCode::OutOfBounds);

  gpu::TextureDesc mismatchedFormatDesc = sourceDesc;
  mismatchedFormatDesc.format = gpu::Format::RGBA8;
  mismatchedFormatDesc.usage = gpu::TextureUsageCopyDestination;
  const gpu::TextureHandle mismatchedFormat =
      device->createTexture(mismatchedFormatDesc);
  assert(mismatchedFormat.valid());
  assert(!device->copyTexture(mismatchedFormat, copyOrigin, source, copyRegion));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  std::uint8_t readBack[4] = {};
  gpu::TextureRegion readRegion;
  readRegion.width = 2;
  readRegion.height = 2;
  assert(device->readTexture(destination, readRegion,
                             {readBack, sizeof(readBack)}));

  assert(!device->readTexture(notReadable, readRegion,
                              {readBack, sizeof(readBack)}));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  assert(!device->readTexture(destination, readRegion, {readBack, 2}));
  assert(takeError(*device).code == gpu::GPUErrorCode::OutOfBounds);

  device->destroy(mismatchedFormat);
  device->destroy(notReadable);
  device->destroy(destination);
  device->destroy(source);
  gpu::destroyDevice(device);
}

void testComputePipeline() {
  gpu::DeviceDesc descriptor;
  descriptor.profile = gpu::RendererProfile::Modern;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(device->capabilities().compute);

  static const char computeSource[] = "compute";
  gpu::PipelineDesc computeDesc;
  computeDesc.compute.source = {computeSource, sizeof(computeSource)};
  const gpu::PipelineHandle computePipeline = device->createPipeline(computeDesc);
  assert(computePipeline.valid());

  static const char vertexSource[] = "vertex";
  static const char fragmentSource[] = "fragment";
  gpu::PipelineDesc graphicsDesc;
  graphicsDesc.vertex.source = {vertexSource, sizeof(vertexSource)};
  graphicsDesc.fragment.source = {fragmentSource, sizeof(fragmentSource)};
  const gpu::PipelineHandle graphicsPipeline =
      device->createPipeline(graphicsDesc);
  assert(graphicsPipeline.valid());

  gpu::BufferDesc storageBufferDesc;
  storageBufferDesc.size = 256;
  storageBufferDesc.usage = gpu::BufferUsageStorage;
  const gpu::BufferHandle storageBuffer = device->createBuffer(storageBufferDesc);
  assert(storageBuffer.valid());

  gpu::TextureDesc storageTextureDesc;
  storageTextureDesc.format = gpu::Format::RGBA8;
  storageTextureDesc.width = 4;
  storageTextureDesc.height = 4;
  storageTextureDesc.usage = gpu::TextureUsageStorage;
  const gpu::TextureHandle storageTexture =
      device->createTexture(storageTextureDesc);
  assert(storageTexture.valid());

  assert(!device->dispatch(1, 1, 1));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  assert(device->setPipeline(computePipeline));
  assert(device->bindStorageBuffer(0, storageBuffer, 0, storageBufferDesc.size));
  assert(device->bindStorageTexture(0, storageTexture, 0));
  assert(device->dispatch(2, 2, 1));
  assert(device->memoryBarrier(gpu::BarrierStorage | gpu::BarrierTexture));

  assert(!device->dispatch(0, 1, 1));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  gpu::TextureDesc colorDesc;
  colorDesc.width = 4;
  colorDesc.height = 4;
  colorDesc.usage = gpu::TextureUsageRenderTarget;
  const gpu::TextureHandle color = device->createTexture(colorDesc);
  assert(color.valid());
  gpu::RenderPassDesc pass;
  pass.colorCount = 1;
  pass.colors[0].target.texture = color;
  assert(device->beginRenderPass(pass));
  assert(!device->setPipeline(computePipeline));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);
  assert(!device->dispatch(1, 1, 1));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);
  assert(device->setPipeline(graphicsPipeline));
  device->endRenderPass();

  assert(!device->dispatch(1, 1, 1));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  gpu::BufferDesc plainBufferDesc;
  plainBufferDesc.size = 64;
  plainBufferDesc.usage = gpu::BufferUsageUniform;
  const gpu::BufferHandle plainBuffer = device->createBuffer(plainBufferDesc);
  assert(plainBuffer.valid());
  assert(!device->bindStorageBuffer(0, plainBuffer, 0, 64));
  assert(takeError(*device).code == gpu::GPUErrorCode::OutOfBounds);

  device->destroy(color);
  device->destroy(plainBuffer);
  device->destroy(storageTexture);
  device->destroy(storageBuffer);
  device->destroy(graphicsPipeline);
  device->destroy(computePipeline);
  gpu::destroyDevice(device);
}

void testComputeUnavailableInPortableProfile() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(!device->capabilities().compute);

  static const char computeSource[] = "compute";
  gpu::PipelineDesc computeDesc;
  computeDesc.compute.source = {computeSource, sizeof(computeSource)};
  assert(!device->createPipeline(computeDesc).valid());
  assert(takeError(*device).code == gpu::GPUErrorCode::UnsupportedFeature);

  assert(!device->memoryBarrier(gpu::BarrierAll));
  assert(takeError(*device).code == gpu::GPUErrorCode::UnsupportedFeature);

  gpu::destroyDevice(device);
}

void testMapBuffer() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);

  gpu::BufferDesc readbackDesc;
  readbackDesc.size = 64;
  readbackDesc.usage = gpu::BufferUsageReadback;
  const gpu::BufferHandle readback = device->createBuffer(readbackDesc);
  assert(readback.valid());

  gpu::BufferDesc stagingDesc;
  stagingDesc.size = 64;
  stagingDesc.usage = gpu::BufferUsageStaging;
  const gpu::BufferHandle staging = device->createBuffer(stagingDesc);
  assert(staging.valid());

  gpu::BufferDesc uniformDesc;
  uniformDesc.size = 64;
  uniformDesc.usage = gpu::BufferUsageUniform;
  const gpu::BufferHandle uniform = device->createBuffer(uniformDesc);
  assert(uniform.valid());

  void *readPointer = device->mapBuffer(readback, 0, 32, gpu::MapMode::Read);
  assert(readPointer != nullptr);
  assert(device->unmapBuffer(readback));
  assert(!device->unmapBuffer(readback));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  void *writePointer = device->mapBuffer(staging, 0, 64, gpu::MapMode::Write);
  assert(writePointer != nullptr);
  assert(device->mapBuffer(staging, 0, 32, gpu::MapMode::Write) == nullptr);
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);
  assert(device->unmapBuffer(staging));

  assert(device->mapBuffer(uniform, 0, 32, gpu::MapMode::Read) == nullptr);
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  assert(device->mapBuffer(readback, 32, 64, gpu::MapMode::Read) == nullptr);
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  device->destroy(uniform);
  device->destroy(staging);
  device->destroy(readback);
  gpu::destroyDevice(device);
}

void testDrawIndirect() {
  gpu::DeviceDesc descriptor;
  descriptor.profile = gpu::RendererProfile::Modern;
  descriptor.surface.width = 640;
  descriptor.surface.height = 480;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(device->capabilities().indirectDraw);

  gpu::BufferDesc indirectDesc;
  indirectDesc.size = sizeof(gpu::DrawIndirectArgs);
  indirectDesc.usage = gpu::BufferUsageIndirect;
  const gpu::BufferHandle indirectBuffer = device->createBuffer(indirectDesc);
  assert(indirectBuffer.valid());

  gpu::BufferDesc indexedIndirectDesc;
  indexedIndirectDesc.size = sizeof(gpu::DrawIndexedIndirectArgs);
  indexedIndirectDesc.usage = gpu::BufferUsageIndirect;
  const gpu::BufferHandle indexedIndirectBuffer =
      device->createBuffer(indexedIndirectDesc);
  assert(indexedIndirectBuffer.valid());

  gpu::BufferDesc plainDesc;
  plainDesc.size = sizeof(gpu::DrawIndirectArgs);
  plainDesc.usage = gpu::BufferUsageVertex;
  const gpu::BufferHandle plainBuffer = device->createBuffer(plainDesc);
  assert(plainBuffer.valid());

  const std::uint16_t indices[] = {0, 1, 2};
  gpu::BufferDesc indexDesc;
  indexDesc.size = sizeof(indices);
  indexDesc.usage = gpu::BufferUsageIndex;
  indexDesc.initialData = {indices, sizeof(indices)};
  const gpu::BufferHandle index = device->createBuffer(indexDesc);
  assert(index.valid());

  const char shader[] = "void main() {}";
  gpu::PipelineDesc pipelineDesc;
  pipelineDesc.vertex.source = {shader, sizeof(shader) - 1};
  pipelineDesc.fragment.source = {shader, sizeof(shader) - 1};
  const gpu::PipelineHandle pipeline = device->createPipeline(pipelineDesc);
  assert(pipeline.valid());

  assert(!device->drawIndirect(indirectBuffer, 0));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  gpu::RenderPassDesc pass;
  pass.colorCount = 1;
  pass.colors[0].surface = true;
  assert(device->beginRenderPass(pass));
  assert(device->setPipeline(pipeline));
  assert(device->drawIndirect(indirectBuffer, 0));
  assert(!device->drawIndirect(plainBuffer, 0));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);
  assert(!device->drawIndirect(indirectBuffer, 1));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  assert(!device->drawIndexedIndirect(indexedIndirectBuffer, 0));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);
  assert(device->bindIndexBuffer(index, gpu::IndexFormat::Uint16, 0));
  assert(device->drawIndexedIndirect(indexedIndirectBuffer, 0));
  device->endRenderPass();

  device->destroy(pipeline);
  device->destroy(index);
  device->destroy(plainBuffer);
  device->destroy(indexedIndirectBuffer);
  device->destroy(indirectBuffer);
  gpu::destroyDevice(device);
}

void testQueries() {
  gpu::DeviceDesc descriptor;
  descriptor.profile = gpu::RendererProfile::Modern;
  descriptor.surface.width = 640;
  descriptor.surface.height = 480;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(device->capabilities().occlusionQueries);
  assert(device->capabilities().timestampQueries);

  const gpu::QueryHandle occlusion =
      device->createQuery(gpu::QueryType::Occlusion);
  assert(occlusion.valid());
  const gpu::QueryHandle timestamp =
      device->createQuery(gpu::QueryType::Timestamp);
  assert(timestamp.valid());

  std::uint64_t result = 0;
  assert(!device->getQueryResult(occlusion, result));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  assert(!device->beginQuery(occlusion));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  gpu::RenderPassDesc pass;
  pass.colorCount = 1;
  pass.colors[0].surface = true;
  assert(device->beginRenderPass(pass));
  assert(device->beginQuery(occlusion));

  const gpu::QueryHandle secondOcclusion =
      device->createQuery(gpu::QueryType::Occlusion);
  assert(secondOcclusion.valid());
  assert(!device->beginQuery(secondOcclusion));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  device->endQuery(secondOcclusion);
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  device->endQuery(occlusion);
  device->endRenderPass();

  assert(!device->writeTimestamp(occlusion));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);
  assert(device->writeTimestamp(timestamp));

  assert(device->getQueryResult(occlusion, result));
  assert(device->getQueryResult(timestamp, result));
  assert(!device->getQueryResult(occlusion, result));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  device->destroy(secondOcclusion);
  device->destroy(timestamp);
  device->destroy(occlusion);
  gpu::destroyDevice(device);
}

void testQueriesUnavailableInPortableProfile() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(!device->capabilities().occlusionQueries);
  assert(!device->capabilities().timestampQueries);

  assert(!device->createQuery(gpu::QueryType::Occlusion).valid());
  assert(takeError(*device).code == gpu::GPUErrorCode::UnsupportedFeature);
  assert(!device->createQuery(gpu::QueryType::Timestamp).valid());
  assert(takeError(*device).code == gpu::GPUErrorCode::UnsupportedFeature);

  gpu::destroyDevice(device);
}

void testDrawIndirectCount() {
  gpu::DeviceDesc descriptor;
  descriptor.profile = gpu::RendererProfile::Modern;
  descriptor.surface.width = 640;
  descriptor.surface.height = 480;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(device->capabilities().indirectCount);

  gpu::BufferDesc indirectDesc;
  indirectDesc.size = sizeof(gpu::DrawIndirectArgs) * 3;
  indirectDesc.usage = gpu::BufferUsageIndirect;
  const gpu::BufferHandle indirectBuffer = device->createBuffer(indirectDesc);
  assert(indirectBuffer.valid());

  gpu::BufferDesc indexedIndirectDesc;
  indexedIndirectDesc.size = sizeof(gpu::DrawIndexedIndirectArgs) * 3;
  indexedIndirectDesc.usage = gpu::BufferUsageIndirect;
  const gpu::BufferHandle indexedIndirectBuffer =
      device->createBuffer(indexedIndirectDesc);
  assert(indexedIndirectBuffer.valid());

  gpu::BufferDesc countDesc;
  countDesc.size = sizeof(std::uint32_t);
  countDesc.usage = gpu::BufferUsageIndirect;
  const gpu::BufferHandle countBuffer = device->createBuffer(countDesc);
  assert(countBuffer.valid());

  gpu::BufferDesc plainDesc;
  plainDesc.size = sizeof(std::uint32_t);
  plainDesc.usage = gpu::BufferUsageVertex;
  const gpu::BufferHandle plainCountBuffer = device->createBuffer(plainDesc);
  assert(plainCountBuffer.valid());

  const std::uint16_t indices[] = {0, 1, 2};
  gpu::BufferDesc indexDesc;
  indexDesc.size = sizeof(indices);
  indexDesc.usage = gpu::BufferUsageIndex;
  indexDesc.initialData = {indices, sizeof(indices)};
  const gpu::BufferHandle index = device->createBuffer(indexDesc);
  assert(index.valid());

  const char shader[] = "void main() {}";
  gpu::PipelineDesc pipelineDesc;
  pipelineDesc.vertex.source = {shader, sizeof(shader) - 1};
  pipelineDesc.fragment.source = {shader, sizeof(shader) - 1};
  const gpu::PipelineHandle pipeline = device->createPipeline(pipelineDesc);
  assert(pipeline.valid());

  gpu::RenderPassDesc pass;
  pass.colorCount = 1;
  pass.colors[0].surface = true;
  assert(device->beginRenderPass(pass));
  assert(device->setPipeline(pipeline));

  assert(device->drawIndirectCount(indirectBuffer, 0, countBuffer, 0, 3,
                                   sizeof(gpu::DrawIndirectArgs)));

  assert(!device->drawIndirectCount(indirectBuffer, 0, plainCountBuffer, 0, 3,
                                    sizeof(gpu::DrawIndirectArgs)));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  assert(!device->drawIndirectCount(indirectBuffer, 0, countBuffer, 0, 0,
                                    sizeof(gpu::DrawIndirectArgs)));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  assert(!device->drawIndirectCount(indirectBuffer, 0, countBuffer, 0, 3,
                                    sizeof(gpu::DrawIndirectArgs) - 1));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  assert(!device->drawIndirectCount(indirectBuffer, 0, countBuffer, 0, 4,
                                    sizeof(gpu::DrawIndirectArgs)));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);

  assert(device->bindIndexBuffer(index, gpu::IndexFormat::Uint16, 0));
  assert(device->drawIndexedIndirectCount(indexedIndirectBuffer, 0,
                                          countBuffer, 0, 3,
                                          sizeof(gpu::DrawIndexedIndirectArgs)));
  device->endRenderPass();

  device->destroy(pipeline);
  device->destroy(index);
  device->destroy(plainCountBuffer);
  device->destroy(countBuffer);
  device->destroy(indexedIndirectBuffer);
  device->destroy(indirectBuffer);
  gpu::destroyDevice(device);
}

void testDrawIndirectCountUnavailableInPortableProfile() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(!device->capabilities().indirectCount);

  assert(!device->drawIndirectCount(gpu::BufferHandle(), 0,
                                    gpu::BufferHandle(), 0, 1,
                                    sizeof(gpu::DrawIndirectArgs)));
  assert(takeError(*device).code == gpu::GPUErrorCode::UnsupportedFeature);
  assert(!device->drawIndexedIndirectCount(
      gpu::BufferHandle(), 0, gpu::BufferHandle(), 0, 1,
      sizeof(gpu::DrawIndexedIndirectArgs)));
  assert(takeError(*device).code == gpu::GPUErrorCode::UnsupportedFeature);

  gpu::destroyDevice(device);
}

void testFences() {
  gpu::DeviceDesc descriptor;
  descriptor.profile = gpu::RendererProfile::Modern;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(device->capabilities().asyncReadback);

  const gpu::FenceHandle fence = device->insertFence();
  assert(fence.valid());
  assert(device->isFenceSignaled(fence));
  assert(device->isFenceSignaled(fence));

  device->destroy(fence);
  assert(!device->isFenceSignaled(fence));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidHandle);

  gpu::destroyDevice(device);
}

void testFencesUnavailableInPortableProfile() {
  gpu::DeviceDesc descriptor;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  assert(!device->capabilities().asyncReadback);

  assert(!device->insertFence().valid());
  assert(takeError(*device).code == gpu::GPUErrorCode::UnsupportedFeature);

  gpu::destroyDevice(device);
}

void testRenderPassScope() {
  gpu::DeviceDesc descriptor;
  descriptor.surface.width = 640;
  descriptor.surface.height = 480;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);

  gpu::RenderPassDesc pass;
  pass.colorCount = 1;
  pass.colors[0].surface = true;
  {
    gpu::RenderPassScope scope(*device, pass);
    assert(scope);
  }
  assert(device->present());

  gpu::RenderPassDesc invalidPass;
  {
    gpu::RenderPassScope scope(*device, invalidPass);
    assert(!scope);
  }
  device->clearErrors();

  gpu::destroyDevice(device);
}

gpu::Device *createModernDevice() {
  gpu::DeviceDesc descriptor;
  descriptor.profile = gpu::RendererProfile::Modern;
  descriptor.surface.width = 64;
  descriptor.surface.height = 64;
  gpu::Device *device = gpu::createDevice(descriptor);
  assert(device != nullptr);
  return device;
}

void testDegenerateTextureExtents() {
  gpu::Device *device = createModernDevice();
  const unsigned char bytes[64] = {};
  const gpu::TextureDimension dimensions[3] = {
      gpu::TextureDimension::Texture2D, gpu::TextureDimension::Texture2DArray,
      gpu::TextureDimension::Texture3D};
  const gpu::Format formats[2] = {gpu::Format::RGBA8, gpu::Format::ETC2RGBA8};
  for (gpu::TextureDimension dimension : dimensions) {
    for (gpu::Format format : formats) {
      for (int zeroed = 0; zeroed < 3; ++zeroed) {
        gpu::TextureDesc desc;
        desc.dimension = dimension;
        desc.format = format;
        desc.width = zeroed == 0 ? 0u : 4u;
        desc.height = zeroed == 1 ? 0u : 4u;
        desc.depthOrLayers = zeroed == 2 ? 0u : 1u;
        desc.usage = gpu::TextureUsageSampled;
        desc.initialData = {bytes, sizeof(bytes)};
        const std::uint64_t before = device->totalErrorCount();
        assert(!device->createTexture(desc).valid());
        assert(device->totalErrorCount() > before);
      }
    }
  }
  gpu::destroyDevice(device);
}

void testFeedbackLoopAndMipmaps() {
  gpu::Device *device = createModernDevice();

  gpu::TextureDesc targetDesc;
  targetDesc.format = gpu::Format::RGBA8;
  targetDesc.width = 32;
  targetDesc.height = 32;
  targetDesc.mipCount = 6;
  targetDesc.usage =
      gpu::TextureUsageRenderTarget | gpu::TextureUsageSampled;
  const gpu::TextureHandle target = device->createTexture(targetDesc);
  assert(target.valid());

  gpu::TextureDesc otherDesc = targetDesc;
  otherDesc.mipCount = 1;
  const gpu::TextureHandle other = device->createTexture(otherDesc);
  assert(other.valid());

  const gpu::SamplerHandle sampler = device->createSampler(gpu::SamplerDesc{});
  assert(sampler.valid());

  gpu::RenderPassDesc pass;
  pass.colorCount = 1;
  pass.colors[0].target.texture = target;
  pass.colors[0].loadOp = gpu::LoadOp::Clear;

  // Bindar o attachment da passada como textura amostrada e um feedback loop.
  device->clearErrors();
  assert(device->beginRenderPass(pass));
  assert(!device->bindTexture(0, target, sampler));
  gpu::GPUError error;
  assert(device->getError(error));
  assert(error.code == gpu::GPUErrorCode::InvalidArgument);
  assert(error.operation == gpu::GPUOperation::BindResource);
  // Uma textura que nao e attachment continua a passar.
  assert(device->bindTexture(0, other, sampler));
  device->endRenderPass();
  device->clearErrors();

  // Depois de fechar a passada, a mesma textura ja pode ser bindada.
  gpu::RenderPassDesc plain = pass;
  plain.colors[0].target.texture = other;
  assert(device->beginRenderPass(plain));
  assert(device->bindTexture(0, target, sampler));
  device->endRenderPass();
  device->clearErrors();

  // generateMipmaps: aceite fora da passada, com mais de um nivel.
  assert(device->generateMipmaps(target));
  assert(device->pendingErrorCount() == 0);

  // Recusado com um so nivel.
  assert(!device->generateMipmaps(other));
  assert(device->getError(error));
  assert(error.operation == gpu::GPUOperation::GenerateMipmaps);
  device->clearErrors();

  // Recusado dentro de uma passada.
  assert(device->beginRenderPass(plain));
  assert(!device->generateMipmaps(target));
  assert(device->getError(error));
  assert(error.operation == gpu::GPUOperation::GenerateMipmaps);
  device->endRenderPass();
  device->clearErrors();

  // Recusado num handle invalido.
  assert(!device->generateMipmaps(gpu::TextureHandle()));
  assert(device->pendingErrorCount() != 0);
  device->clearErrors();

  device->destroy(sampler);
  device->destroy(other);
  device->destroy(target);
  gpu::destroyDevice(device);
}

void testReflectionOnNullBackend() {
  gpu::Device *device = createModernDevice();
  static const char vertexSource[] = "vertex";
  static const char fragmentSource[] = "fragment";
  gpu::PipelineDesc desc;
  desc.vertex.source = {vertexSource, sizeof(vertexSource)};
  desc.fragment.source = {fragmentSource, sizeof(fragmentSource)};
  const gpu::PipelineHandle pipeline = device->createPipeline(desc);
  assert(pipeline.valid());

  // O backend Null nao compila shaders: a reflection e valida mas vazia.
  gpu::PipelineReflection reflection;
  reflection.resourceCount = 7;
  reflection.truncated = true;
  assert(device->reflectPipeline(pipeline, reflection));
  assert(reflection.resourceCount == 0);
  assert(!reflection.truncated);
  assert(device->pendingErrorCount() == 0);

  // Handle invalido tem de falhar e reportar.
  assert(!device->reflectPipeline(gpu::PipelineHandle(), reflection));
  gpu::GPUError error;
  assert(device->getError(error));
  assert(error.operation == gpu::GPUOperation::ReflectPipeline);
  device->clearErrors();

  device->destroy(pipeline);
  gpu::destroyDevice(device);
}

void testEveryRejectionReportsAnError() {
  gpu::Device *device = createModernDevice();
  gpu::BufferDesc bufferDesc;
  bufferDesc.size = 256;
  bufferDesc.usage = gpu::BufferUsageVertex | gpu::BufferUsageIndex |
                     gpu::BufferUsageUniform;
  const gpu::BufferHandle buffer = device->createBuffer(bufferDesc);
  assert(buffer.valid());
  gpu::TextureDesc textureDesc;
  textureDesc.format = gpu::Format::RGBA8;
  textureDesc.width = 8;
  textureDesc.height = 8;
  textureDesc.usage = gpu::TextureUsageSampled;
  const gpu::TextureHandle texture = device->createTexture(textureDesc);
  assert(texture.valid());
  const gpu::SamplerHandle sampler = device->createSampler(gpu::SamplerDesc{});
  assert(sampler.valid());
  device->clearErrors();

  std::uint64_t expected = device->totalErrorCount();
  assert(!device->bindVertexBuffer(0, buffer, 0));
  assert(device->totalErrorCount() == ++expected);
  assert(!device->bindIndexBuffer(buffer, gpu::IndexFormat::Uint16, 0));
  assert(device->totalErrorCount() == ++expected);
  assert(!device->bindUniformBuffer(0, buffer, 0, 16));
  assert(device->totalErrorCount() == ++expected);
  assert(!device->bindTexture(0, texture, sampler));
  assert(device->totalErrorCount() == ++expected);
  assert(!device->bindVertexBuffer(4096, buffer, 0));
  assert(device->totalErrorCount() == ++expected);
  assert(device->pendingErrorCount() == 5);

  device->clearErrors();
  device->suspendSurface();
  assert(device->surfaceState() == gpu::SurfaceState::Suspended);
  assert(device->pendingErrorCount() == 0);

  device->destroy(sampler);
  device->destroy(texture);
  device->destroy(buffer);
  gpu::destroyDevice(device);
}

gpu::PipelineHandle createDrawPipeline(gpu::Device &device) {
  static const char source[] = "shader";
  gpu::PipelineDesc desc;
  desc.vertex.source = {source, sizeof(source) - 1};
  desc.fragment.source = {source, sizeof(source) - 1};
  desc.vertexBufferCount = 1;
  desc.vertexBuffers[0].stride = 8;
  desc.vertexBuffers[0].attributeCount = 1;
  desc.colorTargets[0].format = gpu::Format::RGBA8;
  const gpu::PipelineHandle pipeline = device.createPipeline(desc);
  assert(pipeline.valid());
  return pipeline;
}

void testStaleIndexBindingsAreRejected() {
  gpu::Device *device = createModernDevice();
  gpu::TextureDesc targetDesc;
  targetDesc.format = gpu::Format::RGBA8;
  targetDesc.width = 16;
  targetDesc.height = 16;
  targetDesc.usage =
      gpu::TextureUsageRenderTarget | gpu::TextureUsageCopySource;
  const gpu::TextureHandle target = device->createTexture(targetDesc);
  assert(target.valid());
  gpu::BufferDesc indexDesc;
  indexDesc.size = 64;
  indexDesc.usage = gpu::BufferUsageIndex;
  gpu::RenderPassDesc pass;
  pass.colorCount = 1;
  pass.colors[0].target.texture = target;
  pass.colors[0].loadOp = gpu::LoadOp::Clear;

  const gpu::PipelineHandle first = createDrawPipeline(*device);
  const gpu::PipelineHandle second = createDrawPipeline(*device);
  gpu::BufferHandle indices = device->createBuffer(indexDesc);
  assert(indices.valid());
  assert(device->beginRenderPass(pass));
  assert(device->setPipeline(first));
  assert(device->bindIndexBuffer(indices, gpu::IndexFormat::Uint16, 0));
  assert(device->setPipeline(second));
  device->clearErrors();
  assert(!device->drawIndexed(3));
  assert(device->pendingErrorCount() == 1);
  device->endRenderPass();

  indices = device->createBuffer(indexDesc);
  assert(indices.valid());
  assert(device->beginRenderPass(pass));
  assert(device->setPipeline(first));
  assert(device->bindIndexBuffer(indices, gpu::IndexFormat::Uint16, 0));
  device->destroy(indices);
  device->clearErrors();
  assert(!device->drawIndexed(3));
  assert(device->pendingErrorCount() == 1);
  device->endRenderPass();

  gpu::destroyDevice(device);
}

void testBufferOffsetAlignment() {
  gpu::Device *device = createModernDevice();
  const std::uint32_t alignment =
      device->capabilities().uniformBufferOffsetAlignment;
  assert(alignment >= 1);
  gpu::BufferDesc uniformDesc;
  uniformDesc.size = alignment * 4;
  uniformDesc.usage = gpu::BufferUsageUniform;
  const gpu::BufferHandle uniform = device->createBuffer(uniformDesc);
  assert(uniform.valid());
  gpu::TextureDesc targetDesc;
  targetDesc.format = gpu::Format::RGBA8;
  targetDesc.width = 16;
  targetDesc.height = 16;
  targetDesc.usage = gpu::TextureUsageRenderTarget;
  const gpu::TextureHandle target = device->createTexture(targetDesc);
  gpu::RenderPassDesc pass;
  pass.colorCount = 1;
  pass.colors[0].target.texture = target;
  assert(device->beginRenderPass(pass));
  device->clearErrors();
  assert(device->bindUniformBuffer(0, uniform, alignment, alignment));
  assert(device->pendingErrorCount() == 0);
  if (alignment > 1) {
    assert(!device->bindUniformBuffer(0, uniform, 1, alignment));
    assert(device->pendingErrorCount() == 1);
  }
  device->endRenderPass();
  gpu::destroyDevice(device);
}

void testEntryPointMustBeMain() {
  gpu::Device *device = createModernDevice();
  static const char source[] = "shader";
  gpu::PipelineDesc desc;
  desc.vertex.source = {source, sizeof(source) - 1};
  desc.vertex.entryPoint = "vertexMain";
  desc.fragment.source = {source, sizeof(source) - 1};
  desc.colorTargets[0].format = gpu::Format::RGBA8;
  device->clearErrors();
  assert(!device->createPipeline(desc).valid());
  assert(takeError(*device).code == gpu::GPUErrorCode::UnsupportedFeature);
  gpu::destroyDevice(device);
}

void testBufferUsageIsRequired() {
  gpu::Device *device = createModernDevice();
  gpu::BufferDesc desc;
  desc.size = 64;
  device->clearErrors();
  assert(!device->createBuffer(desc).valid());
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidArgument);
  gpu::destroyDevice(device);
}

void testDeviceCreationReportsFailure() {
  gpu::DeviceDesc unmetRequirements;
  unmetRequirements.requiredCapabilities.compute = true;
  gpu::GPUError requirementError;
  assert(gpu::createDevice(unmetRequirements, &requirementError) == nullptr);
  assert(requirementError.code == gpu::GPUErrorCode::UnsupportedFeature);
  assert(requirementError.operation == gpu::GPUOperation::CreateDevice);
  assert(requirementError.severity == gpu::GPUErrorSeverity::Fatal);
  assert(requirementError.message != nullptr);

  gpu::DeviceDesc absentBackend;
  absentBackend.backend = gpu::Backend::Metal;
  gpu::GPUError backendError;
  assert(gpu::createDevice(absentBackend, &backendError) == nullptr);
  assert(backendError.code == gpu::GPUErrorCode::UnsupportedFeature);
  assert(backendError.operation == gpu::GPUOperation::CreateDevice);
  assert(backendError.message != nullptr);

  assert(gpu::createDevice(absentBackend) == nullptr);

  gpu::DeviceDesc valid;
  gpu::GPUError untouched;
  untouched.code = gpu::GPUErrorCode::DeviceLost;
  gpu::Device *device = gpu::createDevice(valid, &untouched);
  assert(device != nullptr);
  assert(untouched.code == gpu::GPUErrorCode::None);
  gpu::destroyDevice(device);
}

void testBaseInstanceFollowsCapability() {
  gpu::DeviceDesc portableDesc;
  portableDesc.surface.width = 64;
  portableDesc.surface.height = 64;
  gpu::Device *portable = gpu::createDevice(portableDesc);
  assert(portable != nullptr);
  assert(!portable->capabilities().baseInstance);
  portable->clearErrors();
  assert(!portable->draw(3, 1, 0, 1));
  assert(takeError(*portable).code == gpu::GPUErrorCode::UnsupportedFeature);
  gpu::destroyDevice(portable);

  gpu::Device *modern = createModernDevice();
  assert(modern->capabilities().baseInstance);
  gpu::destroyDevice(modern);
}

} // namespace

int main() {
  testLifecycle();
  testCapabilities();
  testInvalidStateAndReuse();
  testPortableCommands();
  testResourcePoolGrowth();
  testIntegerFormatsAndTargets();
  testCopyAndReadTexture();
  testComputePipeline();
  testComputeUnavailableInPortableProfile();
  testMapBuffer();
  testDrawIndirect();
  testQueries();
  testQueriesUnavailableInPortableProfile();
  testDrawIndirectCount();
  testDrawIndirectCountUnavailableInPortableProfile();
  testFences();
  testFencesUnavailableInPortableProfile();
  testRenderPassScope();
  testDegenerateTextureExtents();
  testFeedbackLoopAndMipmaps();
  testReflectionOnNullBackend();
  testEveryRejectionReportsAnError();
  testStaleIndexBindingsAreRejected();
  testBufferOffsetAlignment();
  testEntryPointMustBeMain();
  testBufferUsageIsRequired();
  testBaseInstanceFollowsCapability();
  testDeviceCreationReportsFailure();
  return 0;
}
