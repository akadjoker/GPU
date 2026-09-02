#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"
#include "../backends/vulkan/VulkanSurface.h"
#include "RenderPathProbe.h"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{

  bool requiredInstanceExtensions(void *, const char *const *&extensions,
                                  std::uint32_t &count)
  {
    static const char *const names[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                        VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME};
    extensions = names;
    count = 2;
    return true;
  }

  bool createSurface(void *, VkInstance instance, VkSurfaceKHR &surface)
  {
    PFN_vkCreateHeadlessSurfaceEXT create =
        reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateHeadlessSurfaceEXT"));
    if (!create)
      return false;
    VkHeadlessSurfaceCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
    return create(instance, &createInfo, nullptr, &surface) == VK_SUCCESS;
  }

  void drawableSize(void *, std::uint32_t &width, std::uint32_t &height)
  {
    width = 64;
    height = 64;
  }

  bool headlessSurfaceSupported()
  {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) !=
        VK_SUCCESS)
      return false;
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count,
                                               extensions.data()) != VK_SUCCESS)
      return false;
    for (const VkExtensionProperties &extension : extensions)
    {
      if (std::strcmp(extension.extensionName,
                      VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME) == 0)
        return true;
    }
    return false;
  }

  std::vector<std::uint32_t> readShader(const char *name)
  {
    const std::string path = std::string(GPU_VULKAN_TEST_SHADER_DIR) + "/" + name;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    assert(file);
    const std::streamsize size = file.tellg();
    assert(size > 0 && size % static_cast<std::streamsize>(sizeof(std::uint32_t)) == 0);
    std::vector<std::uint32_t> words(
        static_cast<std::size_t>(size) / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(words.data()), size);
    assert(file);
    return words;
  }

} // namespace

int main()
{
  gpu::DeviceDesc desc;
  desc.backend = gpu::Backend::Vulkan;
  gpu::GPUError error;
  gpu::Device *device = gpu::createDevice(desc, &error);
  assert(device != nullptr);
  assert(error.code == gpu::GPUErrorCode::None);
  assert(device->capabilities().maxTextureDimension2D > 0);
  assert(device->capabilities().maxColorAttachments >= 1);
  assert(device->surfaceState() == gpu::SurfaceState::Suspended);
  assert(!device->present());
  assert(takeError(*device).code == gpu::GPUErrorCode::SurfaceLost);

  const std::uint32_t initial[] = {1, 2, 3, 4};
  gpu::BufferDesc bufferDesc;
  bufferDesc.size = sizeof(initial);
  bufferDesc.usage = gpu::BufferUsageVertex | gpu::BufferUsageReadback;
  bufferDesc.initialData = {initial, sizeof(initial)};
  const gpu::BufferHandle source = device->createBuffer(bufferDesc);
  const gpu::BufferHandle destination = device->createBuffer(bufferDesc);
  assert(source.valid());
  assert(destination.valid());
  const std::uint32_t replacement[] = {5, 6};
  assert(device->updateBuffer(source, 0, {replacement, sizeof(replacement)}));
  assert(device->copyBuffer(destination, 0, source, 0, sizeof(initial)));
  const std::uint32_t *copied = static_cast<const std::uint32_t *>(
      device->mapBuffer(destination, 0, sizeof(initial), gpu::MapMode::Read));
  assert(copied[0] == 5 && copied[1] == 6 && copied[2] == 3 && copied[3] == 4);
  assert(device->unmapBuffer(destination));
  device->destroy(source);
  device->destroy(destination);

  const gpu::FenceHandle fence = device->insertFence();
  assert(fence.valid());
  // isFenceSignaled() is a non-blocking poll: an empty submission still
  // completes asynchronously, so give the driver a bounded number of
  // attempts rather than assuming the very first poll already sees it
  // signaled. Poll a second time to confirm the signaled state sticks
  // (it must not be consumed by observing it, unlike a semaphore wait).
  bool fenceSignaled = false;
  for (int attempt = 0; attempt < 10000 && !fenceSignaled; ++attempt)
    fenceSignaled = device->isFenceSignaled(fence);
  assert(fenceSignaled);
  assert(device->isFenceSignaled(fence));
  device->destroy(fence);
  assert(!device->isFenceSignaled(fence));
  assert(takeError(*device).code == gpu::GPUErrorCode::InvalidHandle);

  gpu::SamplerDesc samplerDesc;
  samplerDesc.compareEnabled = true;
  const gpu::SamplerHandle sampler = device->createSampler(samplerDesc);
  assert(sampler.valid());
  device->destroy(sampler);
  gpu::TextureDesc textureDesc;
  textureDesc.width = 4;
  textureDesc.height = 4;
  textureDesc.usage = gpu::TextureUsageSampled | gpu::TextureUsageRenderTarget;
  const gpu::TextureHandle texture = device->createTexture(textureDesc);
  assert(texture.valid());
  device->destroy(texture);

  const std::uint8_t initialPixels[] = {1, 2, 3, 4,
                                        5, 6, 7, 8,
                                        9, 10, 11, 12,
                                        13, 14, 15, 16};
  textureDesc.format = gpu::Format::R8;
  textureDesc.usage = gpu::TextureUsageCopySource;
  textureDesc.initialData = {initialPixels, sizeof(initialPixels)};
  const gpu::TextureHandle textureSource = device->createTexture(textureDesc);
  assert(textureSource.valid());
  textureDesc.usage = gpu::TextureUsageCopyDestination |
                     gpu::TextureUsageCopySource;
  textureDesc.initialData = {};
  const gpu::TextureHandle textureDestination = device->createTexture(textureDesc);
  assert(textureDestination.valid());
  gpu::TextureRegion textureRegion;
  textureRegion.x = 1;
  textureRegion.y = 1;
  textureRegion.width = 2;
  textureRegion.height = 2;
  gpu::TextureOrigin textureOrigin;
  assert(device->copyTexture(textureDestination, textureOrigin, textureSource,
                             textureRegion));
  std::uint8_t copiedPixels[4] = {};
  gpu::TextureRegion readRegion;
  readRegion.width = 2;
  readRegion.height = 2;
  assert(device->readTexture(textureDestination, readRegion,
                             {copiedPixels, sizeof(copiedPixels)}));
  assert(copiedPixels[0] == 6 && copiedPixels[1] == 7 &&
         copiedPixels[2] == 10 && copiedPixels[3] == 11);
  const std::uint8_t updatePixels[] = {20, 21, 22, 23};
  assert(device->updateTexture(textureDestination, readRegion,
                               {updatePixels, sizeof(updatePixels)}));
  assert(device->readTexture(textureDestination, readRegion,
                             {copiedPixels, sizeof(copiedPixels)}));
  assert(std::memcmp(copiedPixels, updatePixels, sizeof(copiedPixels)) == 0);
  device->destroy(textureDestination);
  device->destroy(textureSource);

  const std::uint8_t mipPixels[] = {
      40, 80, 120, 255, 40, 80, 120, 255, 40, 80, 120, 255, 40, 80, 120, 255,
      40, 80, 120, 255, 40, 80, 120, 255, 40, 80, 120, 255, 40, 80, 120, 255,
      40, 80, 120, 255, 40, 80, 120, 255, 40, 80, 120, 255, 40, 80, 120, 255,
      40, 80, 120, 255, 40, 80, 120, 255, 40, 80, 120, 255, 40, 80, 120, 255};
  gpu::TextureDesc mipTextureDesc;
  mipTextureDesc.width = 4;
  mipTextureDesc.height = 4;
  mipTextureDesc.mipCount = 3;
  mipTextureDesc.format = gpu::Format::RGBA8;
  mipTextureDesc.usage = gpu::TextureUsageSampled | gpu::TextureUsageCopySource;
  mipTextureDesc.initialData = {mipPixels, sizeof(mipPixels)};
  const gpu::TextureHandle mipTexture = device->createTexture(mipTextureDesc);
  assert(mipTexture.valid());
  assert(device->generateMipmaps(mipTexture));
  std::uint8_t generatedMip[16] = {};
  gpu::TextureRegion mipRegion;
  mipRegion.mipLevel = 1;
  mipRegion.width = 2;
  mipRegion.height = 2;
  assert(device->readTexture(mipTexture, mipRegion,
                             {generatedMip, sizeof(generatedMip)}));
  for (std::size_t index = 0; index < sizeof(generatedMip); index += 4)
    assert(std::memcmp(generatedMip + index, mipPixels, 4) == 0);
  device->destroy(mipTexture);

  const std::vector<std::uint32_t> vertexShader =
      readShader("triangle.vert.spv");
  const std::vector<std::uint32_t> fragmentShader =
      readShader("triangle.frag.spv");
  struct Vertex
  {
    float position[2];
    float color[3];
  };
  const Vertex vertices[] = {
      {{-0.75f, -0.75f}, {1.0f, 0.0f, 0.0f}},
      {{0.75f, -0.75f}, {0.0f, 1.0f, 0.0f}},
      {{0.0f, 0.75f}, {0.0f, 0.0f, 1.0f}}};
  gpu::BufferDesc vertexBufferDesc;
  vertexBufferDesc.size = sizeof(vertices);
  vertexBufferDesc.usage = gpu::BufferUsageVertex;
  vertexBufferDesc.initialData = {vertices, sizeof(vertices)};
  const gpu::BufferHandle vertexBuffer = device->createBuffer(vertexBufferDesc);
  assert(vertexBuffer.valid());
  const float translation[2] = {0.0f, 0.0f};
  gpu::BufferDesc uniformBufferDesc;
  uniformBufferDesc.size = sizeof(translation);
  uniformBufferDesc.usage = gpu::BufferUsageUniform;
  uniformBufferDesc.initialData = {translation, sizeof(translation)};
  const gpu::BufferHandle uniformBuffer = device->createBuffer(uniformBufferDesc);
  assert(uniformBuffer.valid());
  const std::uint8_t sampledPixel[] = {31, 125, 241, 255};
  gpu::TextureDesc sampledTextureDesc;
  sampledTextureDesc.width = 1;
  sampledTextureDesc.height = 1;
  sampledTextureDesc.format = gpu::Format::RGBA8;
  sampledTextureDesc.usage = gpu::TextureUsageSampled;
  sampledTextureDesc.initialData = {sampledPixel, sizeof(sampledPixel)};
  const gpu::TextureHandle sampledTexture = device->createTexture(sampledTextureDesc);
  assert(sampledTexture.valid());
  gpu::SamplerDesc sampledTextureSamplerDesc;
  sampledTextureSamplerDesc.minFilter = gpu::Filter::Nearest;
  sampledTextureSamplerDesc.magFilter = gpu::Filter::Nearest;
  const gpu::SamplerHandle sampledTextureSampler =
      device->createSampler(sampledTextureSamplerDesc);
  assert(sampledTextureSampler.valid());
  gpu::TextureDesc renderTextureDesc;
  renderTextureDesc.width = 64;
  renderTextureDesc.height = 64;
  renderTextureDesc.format = gpu::Format::RGBA8;
  renderTextureDesc.usage = gpu::TextureUsageRenderTarget |
                            gpu::TextureUsageCopySource;
  const gpu::TextureHandle renderTexture = device->createTexture(renderTextureDesc);
  assert(renderTexture.valid());
  gpu::PipelineDesc pipelineDesc;
  pipelineDesc.vertex.source = {vertexShader.data(),
                                vertexShader.size() * sizeof(std::uint32_t)};
  pipelineDesc.fragment.source = {fragmentShader.data(),
                                  fragmentShader.size() * sizeof(std::uint32_t)};
  pipelineDesc.vertexBufferCount = 1;
  pipelineDesc.vertexBuffers[0].stride = sizeof(Vertex);
  pipelineDesc.vertexBuffers[0].attributeCount = 2;
  pipelineDesc.vertexBuffers[0].attributes[0].format = gpu::VertexFormat::Float32x2;
  pipelineDesc.vertexBuffers[0].attributes[0].offset = offsetof(Vertex, position);
  pipelineDesc.vertexBuffers[0].attributes[0].shaderLocation = 0;
  pipelineDesc.vertexBuffers[0].attributes[1].format = gpu::VertexFormat::Float32x3;
  pipelineDesc.vertexBuffers[0].attributes[1].offset = offsetof(Vertex, color);
  pipelineDesc.vertexBuffers[0].attributes[1].shaderLocation = 1;
  pipelineDesc.colorTargets[0].format = gpu::Format::RGBA8;
  pipelineDesc.raster.cullMode = gpu::CullMode::None;
  const gpu::PipelineHandle pipeline = device->createPipeline(pipelineDesc);
  assert(pipeline.valid());
  {
    // Drain the validation layer's benign perf warning about vertexBuffers[0]
    // attribute location 1 (color) not being read by triangle.vert (only
    // location 0/position is) - otherwise it sits as the oldest queued
    // entry and the takeError() calls below would pick it up instead of
    // the errors they actually mean to check.
    gpu::GPUError pending;
    while (device->getError(pending))
      assert(pending.severity != gpu::GPUErrorSeverity::Error);
  }

  {
    gpu::PipelineReflection reflection;
    assert(device->reflectPipeline(pipeline, reflection));
    assert(reflection.resourceCount == 2);
    assert(!reflection.truncated);
    bool foundUniform = false;
    bool foundSampler = false;
    for (std::uint32_t index = 0; index < reflection.resourceCount; ++index)
    {
      const gpu::ShaderResource &resource = reflection.resources[index];
      if (resource.type == gpu::ShaderResourceType::UniformBuffer)
      {
        assert(resource.slot == 0);
        assert(resource.elementCount == 1);
        // std140 rounds the lone `vec2 offset` member up to 16 bytes.
        assert(resource.blockSize == 16);
        assert(std::strcmp(resource.name, "transform") == 0);
        foundUniform = true;
      }
      else if (resource.type == gpu::ShaderResourceType::Sampler)
      {
        assert(resource.slot == 0);
        assert(resource.elementCount == 1);
        assert(std::strcmp(resource.name, "colorMap") == 0);
        foundSampler = true;
      }
    }
    assert(foundUniform && foundSampler);

    gpu::PipelineReflection invalid;
    assert(!device->reflectPipeline(gpu::PipelineHandle(), invalid));
    assert(takeError(*device).code == gpu::GPUErrorCode::InvalidHandle);
  }

  gpu::RenderPassDesc renderPass;
  renderPass.colorCount = 1;
  renderPass.colors[0].target.texture = renderTexture;
  renderPass.colors[0].loadOp = gpu::LoadOp::Clear;
  renderPass.colors[0].clearColor[3] = 1.0f;
  assert(device->beginRenderPass(renderPass));
  assert(device->setPipeline(pipeline));
  assert(device->bindVertexBuffer(0, vertexBuffer, 0));
  assert(device->bindUniformBuffer(0, uniformBuffer, 0, sizeof(translation)));
  assert(device->bindTexture(0, sampledTexture, sampledTextureSampler));
  assert(device->draw(3));
  device->endRenderPass();
  std::uint8_t centerPixel[4] = {};
  gpu::TextureRegion centerRegion;
  centerRegion.x = 32;
  centerRegion.y = 32;
  centerRegion.width = 1;
  centerRegion.height = 1;
  assert(device->readTexture(renderTexture, centerRegion,
                             {centerPixel, sizeof(centerPixel)}));
  assert(std::memcmp(centerPixel, sampledPixel, sizeof(centerPixel)) == 0);

  // setPipeline() only replays sticky uniform/texture bindings once per
  // fresh command buffer, trusting that every pipeline shares the same
  // descriptor set layouts (see the comment in setPipeline()). Prove that
  // by switching to a second, layout-compatible pipeline *mid render
  // pass* without rebinding anything, and checking the second draw still
  // reads the same uniform/texture through the still-bound descriptor
  // sets.
  // frontFace only affects rasterization when cullMode != None, and
  // pipelineDesc.raster.cullMode is None (set above) - flipping it here
  // gives a genuinely distinct VkPipeline object without risking a
  // silently-culled (and therefore falsely-passing) second draw.
  gpu::PipelineDesc pipeline2Desc = pipelineDesc;
  pipeline2Desc.raster.frontFace = gpu::FrontFace::Clockwise;
  const gpu::PipelineHandle pipeline2 = device->createPipeline(pipeline2Desc);
  assert(pipeline2.valid());
  assert(device->beginRenderPass(renderPass));
  assert(device->setPipeline(pipeline));
  assert(device->bindVertexBuffer(0, vertexBuffer, 0));
  assert(device->bindUniformBuffer(0, uniformBuffer, 0, sizeof(translation)));
  assert(device->bindTexture(0, sampledTexture, sampledTextureSampler));
  assert(device->draw(3));
  assert(device->setPipeline(pipeline2));
  assert(device->bindVertexBuffer(0, vertexBuffer, 0));
  assert(device->draw(3));
  device->endRenderPass();
  assert(device->readTexture(renderTexture, centerRegion,
                             {centerPixel, sizeof(centerPixel)}));
  assert(std::memcmp(centerPixel, sampledPixel, sizeof(centerPixel)) == 0);
  device->destroy(pipeline2);

  gpu::DrawIndirectArgs indirectArgs;
  indirectArgs.vertexCount = 3;
  gpu::BufferDesc indirectBufferDesc;
  indirectBufferDesc.size = sizeof(indirectArgs);
  indirectBufferDesc.usage = gpu::BufferUsageIndirect;
  indirectBufferDesc.initialData = {&indirectArgs, sizeof(indirectArgs)};
  const gpu::BufferHandle indirectBuffer = device->createBuffer(indirectBufferDesc);
  assert(indirectBuffer.valid());
  assert(device->capabilities().indirectDraw);
  assert(device->beginRenderPass(renderPass));
  assert(device->setPipeline(pipeline));
  assert(device->bindVertexBuffer(0, vertexBuffer, 0));
  assert(device->drawIndirect(indirectBuffer, 0));
  device->endRenderPass();
  assert(device->readTexture(renderTexture, centerRegion,
                             {centerPixel, sizeof(centerPixel)}));
  assert(std::memcmp(centerPixel, sampledPixel, sizeof(centerPixel)) == 0);
  const std::uint16_t indices[] = {0, 1, 2};
  gpu::BufferDesc indexBufferDesc;
  indexBufferDesc.size = sizeof(indices);
  indexBufferDesc.usage = gpu::BufferUsageIndex;
  indexBufferDesc.initialData = {indices, sizeof(indices)};
  const gpu::BufferHandle indexBuffer = device->createBuffer(indexBufferDesc);
  assert(indexBuffer.valid());
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
  assert(device->beginRenderPass(renderPass));
  assert(device->setPipeline(pipeline));
  assert(device->bindVertexBuffer(0, vertexBuffer, 0));
  assert(device->bindIndexBuffer(indexBuffer, gpu::IndexFormat::Uint16, 0));
  assert(device->drawIndexedIndirect(indexedIndirectBuffer, 0));
  device->endRenderPass();
  assert(device->readTexture(renderTexture, centerRegion,
                             {centerPixel, sizeof(centerPixel)}));
  assert(std::memcmp(centerPixel, sampledPixel, sizeof(centerPixel)) == 0);
  if (device->capabilities().indirectCount)
  {
    const std::uint32_t drawCount = 1;
    gpu::BufferDesc countBufferDesc;
    countBufferDesc.size = sizeof(drawCount);
    countBufferDesc.usage = gpu::BufferUsageIndirect;
    countBufferDesc.initialData = {&drawCount, sizeof(drawCount)};
    const gpu::BufferHandle countBuffer = device->createBuffer(countBufferDesc);
    assert(countBuffer.valid());
    assert(device->beginRenderPass(renderPass));
    assert(device->setPipeline(pipeline));
    assert(device->bindVertexBuffer(0, vertexBuffer, 0));
    assert(device->drawIndirectCount(indirectBuffer, 0, countBuffer, 0, 1,
                                     sizeof(gpu::DrawIndirectArgs)));
    device->endRenderPass();
    assert(device->readTexture(renderTexture, centerRegion,
                               {centerPixel, sizeof(centerPixel)}));
    assert(std::memcmp(centerPixel, sampledPixel, sizeof(centerPixel)) == 0);
    assert(device->beginRenderPass(renderPass));
    assert(device->setPipeline(pipeline));
    assert(device->bindVertexBuffer(0, vertexBuffer, 0));
    assert(device->bindIndexBuffer(indexBuffer, gpu::IndexFormat::Uint16, 0));
    assert(device->drawIndexedIndirectCount(
        indexedIndirectBuffer, 0, countBuffer, 0, 1,
        sizeof(gpu::DrawIndexedIndirectArgs)));
    device->endRenderPass();
    assert(device->readTexture(renderTexture, centerRegion,
                               {centerPixel, sizeof(centerPixel)}));
    assert(std::memcmp(centerPixel, sampledPixel, sizeof(centerPixel)) == 0);
    device->destroy(countBuffer);
  }
  if (device->capabilities().occlusionQueries)
  {
    const gpu::QueryHandle occlusionQuery =
        device->createQuery(gpu::QueryType::Occlusion);
    assert(occlusionQuery.valid());
    assert(device->beginRenderPass(renderPass));
    assert(device->beginQuery(occlusionQuery));
    assert(device->setPipeline(pipeline));
    assert(device->bindVertexBuffer(0, vertexBuffer, 0));
    assert(device->draw(3));
    device->endQuery(occlusionQuery);
    device->endRenderPass();
    assert(device->isQueryResultAvailable(occlusionQuery));
    std::uint64_t occlusionResult = 0;
    assert(device->getQueryResult(occlusionQuery, occlusionResult));
    assert(occlusionResult > 0);
    device->destroy(occlusionQuery);
  }
  if (device->capabilities().timestampQueries)
  {
    const gpu::QueryHandle timestampQuery =
        device->createQuery(gpu::QueryType::Timestamp);
    assert(timestampQuery.valid());
    assert(device->writeTimestamp(timestampQuery));
    assert(device->isQueryResultAvailable(timestampQuery));
    std::uint64_t timestampResult = 0;
    assert(device->getQueryResult(timestampQuery, timestampResult));
    assert(timestampResult > 0);
    device->destroy(timestampQuery);
  }
  device->destroy(indexedIndirectBuffer);
  device->destroy(indexBuffer);
  device->destroy(indirectBuffer);
  const gpu::TextureHandle secondRenderTexture =
      device->createTexture(renderTextureDesc);
  assert(secondRenderTexture.valid());
  gpu::PipelineDesc mrtPipelineDesc = pipelineDesc;
  mrtPipelineDesc.colorTargetCount = 2;
  mrtPipelineDesc.colorTargets[1] = mrtPipelineDesc.colorTargets[0];
  const gpu::PipelineHandle mrtPipeline = device->createPipeline(mrtPipelineDesc);
  assert(mrtPipeline.valid());
  gpu::RenderPassDesc mrtRenderPass = renderPass;
  mrtRenderPass.colorCount = 2;
  mrtRenderPass.colors[1] = mrtRenderPass.colors[0];
  mrtRenderPass.colors[1].target.texture = secondRenderTexture;
  assert(device->beginRenderPass(mrtRenderPass));
  assert(device->setPipeline(mrtPipeline));
  assert(device->bindVertexBuffer(0, vertexBuffer, 0));
  assert(device->draw(3));
  device->endRenderPass();
  assert(device->readTexture(renderTexture, centerRegion,
                             {centerPixel, sizeof(centerPixel)}));
  assert(std::memcmp(centerPixel, sampledPixel, sizeof(centerPixel)) == 0);
  assert(device->readTexture(secondRenderTexture, centerRegion,
                             {centerPixel, sizeof(centerPixel)}));
  assert(std::memcmp(centerPixel, sampledPixel, sizeof(centerPixel)) == 0);
  device->destroy(mrtPipeline);
  device->destroy(secondRenderTexture);
  gpu::TextureDesc depthTextureDesc;
  depthTextureDesc.width = 64;
  depthTextureDesc.height = 64;
  depthTextureDesc.format = gpu::Format::Depth32Float;
  depthTextureDesc.usage = gpu::TextureUsageRenderTarget;
  const gpu::TextureHandle depthTexture = device->createTexture(depthTextureDesc);
  assert(depthTexture.valid());
  gpu::PipelineDesc depthPipelineDesc = pipelineDesc;
  depthPipelineDesc.depthStencil.format = gpu::Format::Depth32Float;
  depthPipelineDesc.depthStencil.depthTestEnabled = true;
  depthPipelineDesc.depthStencil.depthWriteEnabled = true;
  depthPipelineDesc.depthStencil.depthCompare = gpu::CompareOp::Always;
  const gpu::PipelineHandle depthPipeline = device->createPipeline(depthPipelineDesc);
  assert(depthPipeline.valid());
  gpu::RenderPassDesc depthRenderPass = renderPass;
  depthRenderPass.hasDepthStencil = true;
  depthRenderPass.depthStencil.target.texture = depthTexture;
  depthRenderPass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
  depthRenderPass.depthStencil.depthStoreOp = gpu::StoreOp::Store;
  depthRenderPass.depthStencil.clearDepth = 1.0f;
  assert(device->beginRenderPass(depthRenderPass));
  assert(device->setPipeline(depthPipeline));
  assert(device->bindVertexBuffer(0, vertexBuffer, 0));
  assert(device->draw(3));
  device->endRenderPass();
  assert(device->readTexture(renderTexture, centerRegion,
                             {centerPixel, sizeof(centerPixel)}));
  assert(std::memcmp(centerPixel, sampledPixel, sizeof(centerPixel)) == 0);
  device->destroy(depthPipeline);
  device->destroy(depthTexture);
  gpu::TextureDesc stencilTextureDesc = depthTextureDesc;
  stencilTextureDesc.format = gpu::Format::Depth24Stencil8;
  const gpu::TextureHandle stencilTexture = device->createTexture(stencilTextureDesc);
  if (stencilTexture.valid())
  {
    gpu::PipelineDesc stencilPipelineDesc = pipelineDesc;
    stencilPipelineDesc.depthStencil.format = gpu::Format::Depth24Stencil8;
    stencilPipelineDesc.depthStencil.stencilEnabled = true;
    stencilPipelineDesc.depthStencil.stencilFront.compare = gpu::CompareOp::Always;
    stencilPipelineDesc.depthStencil.stencilFront.passOperation =
        gpu::StencilOperation::Replace;
    stencilPipelineDesc.depthStencil.stencilBack =
        stencilPipelineDesc.depthStencil.stencilFront;
    const gpu::PipelineHandle stencilPipeline =
        device->createPipeline(stencilPipelineDesc);
    assert(stencilPipeline.valid());
    gpu::RenderPassDesc stencilRenderPass = renderPass;
    stencilRenderPass.hasDepthStencil = true;
    stencilRenderPass.depthStencil.target.texture = stencilTexture;
    stencilRenderPass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
    stencilRenderPass.depthStencil.stencilLoadOp = gpu::LoadOp::Clear;
    stencilRenderPass.depthStencil.depthStoreOp = gpu::StoreOp::Store;
    stencilRenderPass.depthStencil.stencilStoreOp = gpu::StoreOp::Store;
    assert(device->beginRenderPass(stencilRenderPass));
    assert(device->setPipeline(stencilPipeline));
    assert(device->setStencilReference(7));
    assert(device->bindVertexBuffer(0, vertexBuffer, 0));
    assert(device->draw(3));
    device->endRenderPass();
    assert(device->readTexture(renderTexture, centerRegion,
                               {centerPixel, sizeof(centerPixel)}));
    assert(std::memcmp(centerPixel, sampledPixel, sizeof(centerPixel)) == 0);
    device->destroy(stencilPipeline);
    device->destroy(stencilTexture);
  }
  else
  {
    device->clearErrors();
  }

  gpu::GPUError validationError;
  if (device->capabilities().compute && device->capabilities().storageTextures)
  {
    const std::vector<std::uint32_t> computeImageShader =
        readShader("vulkan_compute_image.comp.spv");
    gpu::PipelineDesc computeImageDesc;
    computeImageDesc.compute.source = {
        computeImageShader.data(), computeImageShader.size() * sizeof(std::uint32_t)};
    const gpu::PipelineHandle computeImagePipeline =
        device->createPipeline(computeImageDesc);
    assert(computeImagePipeline.valid());

    {
      gpu::PipelineReflection reflection;
      assert(device->reflectPipeline(computeImagePipeline, reflection));
      assert(reflection.resourceCount == 1);
      assert(reflection.resources[0].type == gpu::ShaderResourceType::StorageTexture);
      assert(reflection.resources[0].slot == 0);
      assert(std::strcmp(reflection.resources[0].name, "dst") == 0);
    }

    gpu::TextureDesc storageTextureDesc;
    storageTextureDesc.width = 4;
    storageTextureDesc.height = 4;
    storageTextureDesc.format = gpu::Format::RGBA8;
    storageTextureDesc.usage =
        gpu::TextureUsageStorage | gpu::TextureUsageCopySource;
    const gpu::TextureHandle storageTexture = device->createTexture(storageTextureDesc);
    assert(storageTexture.valid());

    assert(device->setPipeline(computeImagePipeline));
    assert(device->bindStorageTexture(0, storageTexture, 0));
    assert(device->dispatch(2, 2, 1));
    assert(device->memoryBarrier(gpu::BarrierTexture));

    std::uint8_t imagePixels[64] = {};
    gpu::TextureRegion imageRegion;
    imageRegion.width = 4;
    imageRegion.height = 4;
    assert(device->readTexture(storageTexture, imageRegion,
                               {imagePixels, sizeof(imagePixels)}));
    const std::uint8_t expectedImagePixel[4] = {255, 0, 0, 255};
    for (std::uint32_t texel = 0; texel < 16; ++texel)
      assert(std::memcmp(imagePixels + texel * 4, expectedImagePixel, 4) == 0);

    // A zero-sized dispatch is invalid and must not silently succeed.
    assert(!device->dispatch(0, 1, 1));
    device->clearErrors();

    device->destroy(computeImagePipeline);
    device->destroy(storageTexture);
  }

  if (device->capabilities().compute && device->capabilities().storageBuffers)
  {
    const std::vector<std::uint32_t> computeBufferShader =
        readShader("vulkan_compute_buffer.comp.spv");
    gpu::PipelineDesc computeBufferDesc;
    computeBufferDesc.compute.source = {
        computeBufferShader.data(), computeBufferShader.size() * sizeof(std::uint32_t)};
    const gpu::PipelineHandle computeBufferPipeline =
        device->createPipeline(computeBufferDesc);
    assert(computeBufferPipeline.valid());

    {
      gpu::PipelineReflection reflection;
      assert(device->reflectPipeline(computeBufferPipeline, reflection));
      assert(reflection.resourceCount == 1);
      assert(reflection.resources[0].type == gpu::ShaderResourceType::StorageBuffer);
      assert(reflection.resources[0].slot == 0);
    }

    gpu::BufferDesc storageBufferDesc;
    storageBufferDesc.size = 64;
    storageBufferDesc.usage = gpu::BufferUsageStorage | gpu::BufferUsageReadback;
    const gpu::BufferHandle storageBuffer = device->createBuffer(storageBufferDesc);
    assert(storageBuffer.valid());

    assert(device->setPipeline(computeBufferPipeline));
    assert(device->bindStorageBuffer(0, storageBuffer, 0, storageBufferDesc.size));
    assert(device->dispatch(1, 1, 1));
    assert(device->memoryBarrier(gpu::BarrierStorage));

    // Unlike the OpenGL reference test, read the buffer back: the values
    // written by gl_GlobalInvocationID.x prove the dispatch actually ran
    // with the right work-group count, not just that nothing errored.
    const std::uint32_t *values = static_cast<const std::uint32_t *>(
        device->mapBuffer(storageBuffer, 0, 4 * sizeof(std::uint32_t), gpu::MapMode::Read));
    assert(values != nullptr);
    assert(values[0] == 0 && values[1] == 1 && values[2] == 2 && values[3] == 3);
    assert(device->unmapBuffer(storageBuffer));

    device->destroy(computeBufferPipeline);
    device->destroy(storageBuffer);
  }
  while (device->getError(validationError))
  {
    if (validationError.severity == gpu::GPUErrorSeverity::Error)
      std::fprintf(stderr, "[validation] %s\n", validationError.message);
    assert(validationError.severity != gpu::GPUErrorSeverity::Error);
  }

  device->destroy(pipeline);
  device->destroy(renderTexture);
  device->destroy(sampledTextureSampler);
  device->destroy(sampledTexture);
  device->destroy(uniformBuffer);
  device->destroy(vertexBuffer);
  // With the Khronos validation layer active (enabled automatically by
  // VulkanDevice when installed), any API misuse in the sequence above
  // would have queued a diagnostic here instead of just happening to not
  // crash on this particular driver.
  while (device->getError(validationError))
  {
    if (validationError.severity == gpu::GPUErrorSeverity::Error)
      std::fprintf(stderr, "[validation] %s\n", validationError.message);
    assert(validationError.severity != gpu::GPUErrorSeverity::Error);
  }
  gpu::destroyDevice(device);

  if (!headlessSurfaceSupported())
    return 0;

  gpu::VulkanSurface surface;
  surface.requiredInstanceExtensions = requiredInstanceExtensions;
  surface.create = createSurface;
  surface.drawableSize = drawableSize;
  desc.surface.nativeHandle = &surface;
  device = gpu::createDevice(desc, &error);
  assert(device != nullptr);
  assert(device->surfaceState() == gpu::SurfaceState::Ready);
  gpu::RenderPassDesc surfacePass;
  surfacePass.colorCount = 1;
  surfacePass.colors[0].surface = true;
  surfacePass.colors[0].loadOp = gpu::LoadOp::Clear;
  surfacePass.colors[0].clearColor[0] = 0.25f;
  surfacePass.colors[0].clearColor[3] = 1.0f;
  assert(device->beginRenderPass(surfacePass));
  device->endRenderPass();
  assert(device->present());

  // Exercise a real pipeline draw against the swapchain, not just a clear:
  // this is the path that requires a graphics pipeline to be created with
  // the surface's actual (often BGRA-ordered) native format rather than the
  // caller's abstract `Format` request, via ColorTargetState::surface.
  const gpu::BufferHandle surfaceVertexBuffer = device->createBuffer(vertexBufferDesc);
  assert(surfaceVertexBuffer.valid());
  const gpu::BufferHandle surfaceUniformBuffer = device->createBuffer(uniformBufferDesc);
  assert(surfaceUniformBuffer.valid());
  const gpu::TextureHandle surfaceSampledTexture =
      device->createTexture(sampledTextureDesc);
  assert(surfaceSampledTexture.valid());
  const gpu::SamplerHandle surfaceSampledTextureSampler =
      device->createSampler(sampledTextureSamplerDesc);
  assert(surfaceSampledTextureSampler.valid());
  gpu::PipelineDesc surfacePipelineDesc = pipelineDesc;
  surfacePipelineDesc.colorTargets[0].surface = true;
  const gpu::PipelineHandle surfacePipeline = device->createPipeline(surfacePipelineDesc);
  assert(surfacePipeline.valid());
  assert(device->beginRenderPass(surfacePass));
  assert(device->setPipeline(surfacePipeline));
  assert(device->bindVertexBuffer(0, surfaceVertexBuffer, 0));
  assert(device->bindUniformBuffer(0, surfaceUniformBuffer, 0, sizeof(translation)));
  assert(device->bindTexture(0, surfaceSampledTexture, surfaceSampledTextureSampler));
  assert(device->draw(3));
  device->endRenderPass();
  assert(device->present());
  device->destroy(surfacePipeline);
  device->destroy(surfaceSampledTextureSampler);
  device->destroy(surfaceSampledTexture);
  device->destroy(surfaceUniformBuffer);
  device->destroy(surfaceVertexBuffer);

  // A depth attachment alongside the surface color attachment used to be
  // rejected outright (beginRenderPass() had a blanket
  // `usesSurface && desc.hasDepthStencil` guard, and the size check below it
  // would have dereferenced a null `color` pointer had that guard ever been
  // lifted without fixing it too). Exercise the combination for real: two
  // draws at different depths, with depth testing deciding what's on top.
  {
    const gpu::BufferHandle depthVertexBuffer = device->createBuffer(vertexBufferDesc);
    assert(depthVertexBuffer.valid());
    const gpu::BufferHandle depthUniformBuffer = device->createBuffer(uniformBufferDesc);
    assert(depthUniformBuffer.valid());
    const gpu::TextureHandle depthSampledTexture =
        device->createTexture(sampledTextureDesc);
    assert(depthSampledTexture.valid());
    const gpu::SamplerHandle depthSampledTextureSampler =
        device->createSampler(sampledTextureSamplerDesc);
    assert(depthSampledTextureSampler.valid());
    gpu::TextureDesc surfaceDepthTextureDesc;
    surfaceDepthTextureDesc.width = 64;
    surfaceDepthTextureDesc.height = 64;
    surfaceDepthTextureDesc.format = gpu::Format::Depth32Float;
    surfaceDepthTextureDesc.usage = gpu::TextureUsageRenderTarget;
    const gpu::TextureHandle surfaceDepthTexture =
        device->createTexture(surfaceDepthTextureDesc);
    assert(surfaceDepthTexture.valid());
    gpu::PipelineDesc depthPipelineDesc = pipelineDesc;
    depthPipelineDesc.colorTargets[0].surface = true;
    depthPipelineDesc.depthStencil.format = gpu::Format::Depth32Float;
    depthPipelineDesc.depthStencil.depthTestEnabled = true;
    depthPipelineDesc.depthStencil.depthWriteEnabled = true;
    depthPipelineDesc.depthStencil.depthCompare = gpu::CompareOp::Less;
    const gpu::PipelineHandle surfaceDepthPipeline =
        device->createPipeline(depthPipelineDesc);
    assert(surfaceDepthPipeline.valid());
    gpu::RenderPassDesc surfaceDepthPass = surfacePass;
    surfaceDepthPass.hasDepthStencil = true;
    surfaceDepthPass.depthStencil.target.texture = surfaceDepthTexture;
    surfaceDepthPass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
    surfaceDepthPass.depthStencil.depthStoreOp = gpu::StoreOp::Store;
    surfaceDepthPass.depthStencil.clearDepth = 1.0f;
    assert(device->beginRenderPass(surfaceDepthPass));
    assert(device->setPipeline(surfaceDepthPipeline));
    assert(device->bindVertexBuffer(0, depthVertexBuffer, 0));
    assert(device->bindUniformBuffer(0, depthUniformBuffer, 0, sizeof(translation)));
    assert(device->bindTexture(0, depthSampledTexture, depthSampledTextureSampler));
    assert(device->draw(3));
    assert(device->draw(3));
    device->endRenderPass();
    assert(device->present());
    device->destroy(surfaceDepthPipeline);
    device->destroy(surfaceDepthTexture);
    device->destroy(depthSampledTextureSampler);
    device->destroy(depthSampledTexture);
    device->destroy(depthUniformBuffer);
    device->destroy(depthVertexBuffer);
  }
  while (device->getError(validationError))
  {
    if (validationError.severity == gpu::GPUErrorSeverity::Error)
      std::fprintf(stderr, "[validation] %s\n", validationError.message);
    assert(validationError.severity != gpu::GPUErrorSeverity::Error);
  }

  assert(device->resizeSurface(32, 32));
  device->suspendSurface();
  assert(device->surfaceState() == gpu::SurfaceState::Suspended);
  assert(device->resumeSurface());
  assert(device->present());
  gpu::destroyDevice(device);
  return 0;
}
