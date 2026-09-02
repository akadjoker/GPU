// Demonstrates gpu::Device::reflectPipeline() on the Vulkan backend: instead
// of hardcoding which descriptor slot the "transform" uniform block and the
// "colorMap" sampler live at, this demo asks the pipeline itself and binds
// resources by name - the same pattern a material system would use to bind
// resources without every shader author having to agree on fixed slots.
#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"

#include "SDLVulkanSurface.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{

  bool reportErrors(gpu::Device &device)
  {
    bool found = false;
    gpu::GPUError error;
    while (device.getError(error))
    {
      std::fprintf(stderr, "GPU error %u in operation %u: %s\n",
                   static_cast<unsigned>(error.code),
                   static_cast<unsigned>(error.operation),
                   error.message ? error.message : "no diagnostic");
      found = true;
    }
    return found;
  }

  bool readShader(const char *name, std::vector<std::uint32_t> &words)
  {
    const std::string path = std::string(GPU_VULKAN_DEMO_SHADER_DIR) + "/" + name;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
      return false;
    const std::streamsize size = file.tellg();
    if (size <= 0 || size % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0)
      return false;
    words.resize(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(words.data()), size);
    return static_cast<bool>(file);
  }

  // Finds a reflected resource by name - this is the whole point of the
  // demo: the caller never needs to know (or keep in sync with the shader
  // source) which descriptor slot "transform"/"colorMap" were assigned.
  const gpu::ShaderResource *findResource(const gpu::PipelineReflection &reflection,
                                          const char *name)
  {
    for (std::uint32_t index = 0; index < reflection.resourceCount; ++index)
      if (std::strcmp(reflection.resources[index].name, name) == 0)
        return &reflection.resources[index];
    return nullptr;
  }

  const char *resourceTypeName(gpu::ShaderResourceType type)
  {
    switch (type)
    {
    case gpu::ShaderResourceType::Sampler:
      return "sampler";
    case gpu::ShaderResourceType::UniformBuffer:
      return "uniform buffer";
    case gpu::ShaderResourceType::StorageBuffer:
      return "storage buffer";
    case gpu::ShaderResourceType::StorageTexture:
      return "storage texture";
    }
    return "unknown";
  }

} // namespace

int main(int argc, char **argv)
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    return 1;

  SDLVulkanSurface surface;
  if (!createSDLVulkanSurface(surface, "GPU Vulkan Reflection", 960, 540))
  {
    SDL_Quit();
    return 1;
  }

  gpu::DeviceDesc deviceDesc;
  deviceDesc.backend = gpu::Backend::Vulkan;
  deviceDesc.surface.nativeHandle = &surface.gpuSurface;
  deviceDesc.surface.width = 960;
  deviceDesc.surface.height = 540;
  gpu::GPUError deviceError;
  gpu::Device *device = gpu::createDevice(deviceDesc, &deviceError);
  if (!device)
  {
    std::fprintf(stderr, "failed to create the Vulkan device: %s\n",
                 deviceError.message ? deviceError.message : "no diagnostic");
    destroySDLVulkanSurface(surface);
    SDL_Quit();
    return 1;
  }

  std::vector<std::uint32_t> vertexShader;
  std::vector<std::uint32_t> fragmentShader;
  if (!readShader("vulkan_demo_reflection.vert.spv", vertexShader) ||
      !readShader("vulkan_demo_reflection.frag.spv", fragmentShader))
  {
    std::fprintf(stderr, "failed to load the Vulkan demo shaders\n");
    gpu::destroyDevice(device);
    destroySDLVulkanSurface(surface);
    SDL_Quit();
    return 1;
  }

  struct Vertex
  {
    float position[2];
    float uv[2];
  };
  // Same winding-order note as the plain triangle demo: Vulkan NDC has +Y
  // pointing down.
  const Vertex vertices[] = {
      {{0.0f, 0.65f}, {0.5f, 0.0f}},
      {{0.65f, -0.65f}, {1.0f, 1.0f}},
      {{-0.65f, -0.65f}, {0.0f, 1.0f}}};
  gpu::BufferDesc vertexBufferDesc;
  vertexBufferDesc.size = sizeof(vertices);
  vertexBufferDesc.usage = gpu::BufferUsageVertex;
  vertexBufferDesc.initialData = {vertices, sizeof(vertices)};
  const gpu::BufferHandle vertexBuffer = device->createBuffer(vertexBufferDesc);

  // A tiny checkerboard so the sampled colors visibly vary across the
  // triangle - no image file to load, just four texels.
  const std::uint8_t checkerPixels[] = {
      255, 220, 90, 255, 40, 60, 200, 255,
      40, 60, 200, 255, 255, 220, 90, 255};
  gpu::TextureDesc checkerDesc;
  checkerDesc.width = 2;
  checkerDesc.height = 2;
  checkerDesc.format = gpu::Format::RGBA8;
  checkerDesc.usage = gpu::TextureUsageSampled;
  checkerDesc.initialData = {checkerPixels, sizeof(checkerPixels)};
  const gpu::TextureHandle checkerTexture = device->createTexture(checkerDesc);
  gpu::SamplerDesc samplerDesc;
  samplerDesc.minFilter = gpu::Filter::Nearest;
  samplerDesc.magFilter = gpu::Filter::Nearest;
  const gpu::SamplerHandle sampler = device->createSampler(samplerDesc);

  const float initialOffset[2] = {0.0f, 0.0f};
  gpu::BufferDesc uniformBufferDesc;
  uniformBufferDesc.size = sizeof(initialOffset);
  uniformBufferDesc.usage = gpu::BufferUsageUniform;
  uniformBufferDesc.initialData = {initialOffset, sizeof(initialOffset)};
  const gpu::BufferHandle uniformBuffer = device->createBuffer(uniformBufferDesc);

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
  pipelineDesc.vertexBuffers[0].attributes[1].format = gpu::VertexFormat::Float32x2;
  pipelineDesc.vertexBuffers[0].attributes[1].offset = offsetof(Vertex, uv);
  pipelineDesc.vertexBuffers[0].attributes[1].shaderLocation = 1;
  pipelineDesc.colorTargets[0].surface = true;
  pipelineDesc.raster.cullMode = gpu::CullMode::None;
  pipelineDesc.debugName = "vulkan-reflection";
  const gpu::PipelineHandle pipeline = device->createPipeline(pipelineDesc);

  bool running = pipeline.valid() && vertexBuffer.valid() &&
                 checkerTexture.valid() && sampler.valid() && uniformBuffer.valid();
  bool failed = !running;
  reportErrors(*device);

  // The actual demonstration: read the pipeline's resources back instead
  // of assuming set/binding numbers, and print what it found.
  std::int32_t transformSlot = -1;
  std::int32_t colorMapSlot = -1;
  if (running)
  {
    gpu::PipelineReflection reflection;
    if (!device->reflectPipeline(pipeline, reflection))
    {
      std::fprintf(stderr, "reflectPipeline() failed\n");
      reportErrors(*device);
      running = false;
      failed = true;
    }
    else
    {
      std::printf("pipeline '%s' exposes %u resource%s:\n", pipelineDesc.debugName,
                 reflection.resourceCount, reflection.resourceCount == 1 ? "" : "s");
      for (std::uint32_t index = 0; index < reflection.resourceCount; ++index)
      {
        const gpu::ShaderResource &resource = reflection.resources[index];
        std::printf("  [%u] %-14s '%s' slot=%u elements=%u blockSize=%u\n", index,
                   resourceTypeName(resource.type), resource.name, resource.slot,
                   resource.elementCount, resource.blockSize);
      }
      const gpu::ShaderResource *transform = findResource(reflection, "transform");
      const gpu::ShaderResource *colorMap = findResource(reflection, "colorMap");
      if (!transform || transform->type != gpu::ShaderResourceType::UniformBuffer ||
          !colorMap || colorMap->type != gpu::ShaderResourceType::Sampler)
      {
        std::fprintf(stderr,
                     "expected a 'transform' uniform block and a 'colorMap' "
                     "sampler - the shader source and this demo have drifted "
                     "apart\n");
        running = false;
        failed = true;
      }
      else
      {
        transformSlot = static_cast<std::int32_t>(transform->slot);
        colorMapSlot = static_cast<std::int32_t>(colorMap->slot);
      }
    }
  }

  const unsigned long frameLimit =
      argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 0;
  unsigned long frameCount = 0;

  gpu::RenderPassDesc pass;
  pass.colorCount = 1;
  pass.colors[0].surface = true;
  pass.colors[0].loadOp = gpu::LoadOp::Clear;
  pass.colors[0].storeOp = gpu::StoreOp::Store;
  pass.colors[0].clearColor[0] = 0.025f;
  pass.colors[0].clearColor[1] = 0.035f;
  pass.colors[0].clearColor[2] = 0.06f;
  pass.colors[0].clearColor[3] = 1.0f;

  while (running)
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_QUIT)
        running = false;
      else if (event.type == SDL_WINDOWEVENT &&
               event.window.event == SDL_WINDOWEVENT_RESIZED)
        device->resizeSurface(static_cast<std::uint32_t>(event.window.data1),
                              static_cast<std::uint32_t>(event.window.data2));
      else if (event.type == SDL_WINDOWEVENT &&
               event.window.event == SDL_WINDOWEVENT_MINIMIZED)
        device->suspendSurface();
      else if (event.type == SDL_WINDOWEVENT &&
               event.window.event == SDL_WINDOWEVENT_RESTORED)
        device->resumeSurface();
    }

    if (!running)
      break;
    if (device->surfaceState() != gpu::SurfaceState::Ready)
    {
      SDL_Delay(16);
      continue;
    }

    const float seconds = static_cast<float>(frameCount) / 60.0f;
    const float offset[2] = {0.35f * std::cos(seconds), 0.35f * std::sin(seconds)};
    device->updateBuffer(uniformBuffer, 0, {offset, sizeof(offset)});

    if (!device->beginRenderPass(pass) || !device->setPipeline(pipeline) ||
        !device->bindVertexBuffer(0, vertexBuffer, 0) ||
        !device->bindUniformBuffer(static_cast<std::uint32_t>(transformSlot),
                                   uniformBuffer, 0, sizeof(offset)) ||
        !device->bindTexture(static_cast<std::uint32_t>(colorMapSlot), checkerTexture,
                             sampler) ||
        !device->draw(3))
    {
      reportErrors(*device);
      failed = true;
      break;
    }
    device->endRenderPass();
    if (!device->present())
    {
      reportErrors(*device);
      failed = true;
      break;
    }
    ++frameCount;
    if (frameLimit != 0 && frameCount >= frameLimit)
      running = false;
  }

  device->destroy(pipeline);
  device->destroy(uniformBuffer);
  device->destroy(sampler);
  device->destroy(checkerTexture);
  device->destroy(vertexBuffer);
  reportErrors(*device);
  gpu::destroyDevice(device);
  destroySDLVulkanSurface(surface);
  SDL_Quit();
  return failed ? 1 : 0;
}
