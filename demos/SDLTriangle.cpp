#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"

#include "SDLGLSurface.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

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

} // namespace

int main(int argc, char **argv)
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    return 1;

  SDLGLSurface surface;
  if (!createSDLGLSurface(surface, "GPU Triangle", 960, 540))
  {
    SDL_Quit();
    return 1;
  }

  gpu::DeviceDesc deviceDesc;
  deviceDesc.backend = gpu::Backend::OpenGL;
  deviceDesc.profile = gpu::RendererProfile::Portable;
  deviceDesc.surface.nativeHandle = &surface.gpuSurface;
  deviceDesc.surface.width = 960;
  deviceDesc.surface.height = 540;
  gpu::Device *device = gpu::createDevice(deviceDesc);
  if (!device)
  {
    destroySDLGLSurface(surface);
    SDL_Quit();
    return 1;
  }

  static const char vertexSource[] =
      "#version 330 core\n"
      "layout(location = 0) in vec2 position;\n"
      "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
  static const char fragmentSource[] =
      "#version 330 core\n"
      "out vec4 color;\n"
      "void main() { color = vec4(0.12, 0.72, 0.95, 1.0); }\n";

  const float vertices[] = {0.0f, 0.65f, -0.65f, -0.65f, 0.65f, -0.65f};
  const std::uint16_t indices[] = {0, 1, 2};
  gpu::BufferDesc vertexBufferDesc;
  vertexBufferDesc.size = sizeof(vertices);
  vertexBufferDesc.usage = gpu::BufferUsageVertex;
  vertexBufferDesc.initialData = {vertices, sizeof(vertices)};
  gpu::BufferDesc indexBufferDesc;
  indexBufferDesc.size = sizeof(indices);
  indexBufferDesc.usage = gpu::BufferUsageIndex;
  indexBufferDesc.initialData = {indices, sizeof(indices)};
  const gpu::BufferHandle vertexBuffer =
      device->createBuffer(vertexBufferDesc);
  const gpu::BufferHandle indexBuffer = device->createBuffer(indexBufferDesc);

  gpu::PipelineDesc pipelineDesc;
  pipelineDesc.vertex.source.data = vertexSource;
  pipelineDesc.vertex.source.size = std::strlen(vertexSource);
  pipelineDesc.fragment.source.data = fragmentSource;
  pipelineDesc.fragment.source.size = std::strlen(fragmentSource);
  pipelineDesc.vertexBufferCount = 1;
  pipelineDesc.vertexBuffers[0].stride = sizeof(float) * 2;
  pipelineDesc.vertexBuffers[0].attributeCount = 1;
  pipelineDesc.vertexBuffers[0].attributes[0].format =
      gpu::VertexFormat::Float32x2;
  pipelineDesc.vertexBuffers[0].attributes[0].shaderLocation = 0;
  pipelineDesc.debugName = "triangle";
  const gpu::PipelineHandle pipeline = device->createPipeline(pipelineDesc);
  bool running = pipeline.valid() && vertexBuffer.valid() && indexBuffer.valid();
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
      if (event.type == SDL_QUIT)
        running = false;

    if (!running)
      break;
    if (!device->beginRenderPass(pass) || !device->setPipeline(pipeline) ||
        !device->bindVertexBuffer(0, vertexBuffer, 0) ||
        !device->bindIndexBuffer(indexBuffer, gpu::IndexFormat::Uint16, 0) ||
        !device->drawIndexed(3))
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
  device->destroy(indexBuffer);
  device->destroy(vertexBuffer);
  reportErrors(*device);
  gpu::destroyDevice(device);
  destroySDLGLSurface(surface);
  SDL_Quit();
  return failed ? 1 : 0;
}
