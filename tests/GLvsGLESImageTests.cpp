#include "../demos/SDLGLESSurface.h"
#include "../demos/SDLGLSurface.h"

#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"

#include <SDL2/SDL.h>

#include <cassert>
#include <cstdint>
#include <cstring>

namespace
{

  void renderSolidGreenTriangle(gpu::Device &device,
                                std::uint8_t (&pixels)[4],
                                const char *vertexSource,
                                const char *fragmentSource)
  {
    gpu::TextureDesc targetDesc;
    targetDesc.format = gpu::Format::RGBA8;
    targetDesc.width = 16;
    targetDesc.height = 16;
    targetDesc.usage = gpu::TextureUsageRenderTarget | gpu::TextureUsageCopySource;
    const gpu::TextureHandle target = device.createTexture(targetDesc);
    assert(target.valid());

    gpu::PipelineDesc pipelineDesc;
    pipelineDesc.vertex.source = {vertexSource, std::strlen(vertexSource)};
    pipelineDesc.fragment.source = {fragmentSource, std::strlen(fragmentSource)};
    pipelineDesc.raster.cullMode = gpu::CullMode::None;
    const gpu::PipelineHandle pipeline = device.createPipeline(pipelineDesc);
    assert(pipeline.valid());

    gpu::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0].target.texture = target;
    pass.colors[0].loadOp = gpu::LoadOp::Clear;
    assert(device.beginRenderPass(pass));
    assert(device.setPipeline(pipeline));
    assert(device.draw(3));
    device.endRenderPass();
    assert(device.pendingErrorCount() == 0);

    gpu::TextureRegion region;
    region.width = 1;
    region.height = 1;
    assert(device.readTexture(target, region, {pixels, sizeof(pixels)}));

    device.destroy(pipeline);
    device.destroy(target);
  }

}

int main()
{
  assert(SDL_Init(SDL_INIT_VIDEO) == 0);

  SDLGLSurface glSurface;
  assert(createSDLGLSurface(glSurface, "GPU GL Compare", 64, 64));
  SDL_HideWindow(glSurface.window);
  gpu::DeviceDesc glDeviceDesc;
  glDeviceDesc.backend = gpu::Backend::OpenGL;
  glDeviceDesc.surface.nativeHandle = &glSurface.gpuSurface;
  glDeviceDesc.surface.width = 64;
  glDeviceDesc.surface.height = 64;
  gpu::Device *glDevice = gpu::createDevice(glDeviceDesc);
  assert(glDevice != nullptr);

  static const char glVertexSource[] =
      "#version 330 core\n"
      "void main() {\n"
      "  vec2 p[3] = vec2[3](vec2(-1.0,-1.0), vec2(3.0,-1.0), vec2(-1.0,3.0));\n"
      "  gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);\n"
      "}\n";
  static const char glFragmentSource[] =
      "#version 330 core\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color = vec4(0.0, 1.0, 0.0, 1.0); }\n";

  std::uint8_t glPixel[4] = {};
  renderSolidGreenTriangle(*glDevice, glPixel, glVertexSource, glFragmentSource);

  gpu::destroyDevice(glDevice);
  destroySDLGLSurface(glSurface);

  SDLGLESSurface glesSurface;
  assert(createSDLGLESSurface(glesSurface, "GPU GLES Compare", 64, 64));
  SDL_HideWindow(glesSurface.window);
  gpu::DeviceDesc glesDeviceDesc;
  glesDeviceDesc.backend = gpu::Backend::OpenGLES;
  glesDeviceDesc.surface.nativeHandle = &glesSurface.gpuSurface;
  glesDeviceDesc.surface.width = 64;
  glesDeviceDesc.surface.height = 64;
  gpu::Device *glesDevice = gpu::createDevice(glesDeviceDesc);
  assert(glesDevice != nullptr);

  static const char glesVertexSource[] =
      "#version 300 es\n"
      "void main() {\n"
      "  vec2 p[3] = vec2[3](vec2(-1.0,-1.0), vec2(3.0,-1.0), vec2(-1.0,3.0));\n"
      "  gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);\n"
      "}\n";
  static const char glesFragmentSource[] =
      "#version 300 es\n"
      "precision highp float;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color = vec4(0.0, 1.0, 0.0, 1.0); }\n";

  std::uint8_t glesPixel[4] = {};
  renderSolidGreenTriangle(*glesDevice, glesPixel, glesVertexSource,
                           glesFragmentSource);

  gpu::destroyDevice(glesDevice);
  destroySDLGLESSurface(glesSurface);

  assert(std::memcmp(glPixel, glesPixel, sizeof(glPixel)) == 0);
  const std::uint8_t expected[4] = {0, 255, 0, 255};
  assert(std::memcmp(glPixel, expected, sizeof(expected)) == 0);

  SDL_Quit();
  return 0;
}
