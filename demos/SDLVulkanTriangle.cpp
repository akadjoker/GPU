#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"

#include "SDLVulkanSurface.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
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

} // namespace

int main(int argc, char **argv)
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    return 1;

  SDLVulkanSurface surface;
  if (!createSDLVulkanSurface(surface, "GPU Vulkan Triangle", 960, 540))
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
  if (!readShader("vulkan_demo_triangle.vert.spv", vertexShader) ||
      !readShader("vulkan_demo_triangle.frag.spv", fragmentShader))
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
    float color[3];
  };
  // Vulkan NDC has +Y pointing down (the opposite of OpenGL), so the apex
  // needs +Y to appear at the top of the window.
  const Vertex vertices[] = {
      {{0.0f, 0.65f}, {0.95f, 0.25f, 0.2f}},
      {{0.65f, -0.65f}, {0.2f, 0.9f, 0.35f}},
      {{-0.65f, -0.65f}, {0.2f, 0.45f, 0.95f}}};
  gpu::BufferDesc vertexBufferDesc;
  vertexBufferDesc.size = sizeof(vertices);
  vertexBufferDesc.usage = gpu::BufferUsageVertex;
  vertexBufferDesc.initialData = {vertices, sizeof(vertices)};
  const gpu::BufferHandle vertexBuffer = device->createBuffer(vertexBufferDesc);

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
  pipelineDesc.colorTargets[0].surface = true;
  pipelineDesc.raster.cullMode = gpu::CullMode::None;
  pipelineDesc.debugName = "vulkan-triangle";
  const gpu::PipelineHandle pipeline = device->createPipeline(pipelineDesc);

  bool running = pipeline.valid() && vertexBuffer.valid();
  bool failed = !running;
  const unsigned long frameLimit =
      argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 0;
  unsigned long frameCount = 0;
  reportErrors(*device);

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
    if (!device->beginRenderPass(pass) || !device->setPipeline(pipeline) ||
        !device->bindVertexBuffer(0, vertexBuffer, 0) || !device->draw(3))
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
  device->destroy(vertexBuffer);
  reportErrors(*device);
  gpu::destroyDevice(device);
  destroySDLVulkanSurface(surface);
  SDL_Quit();
  return failed ? 1 : 0;
}
