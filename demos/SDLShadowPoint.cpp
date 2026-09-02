#include "ShadowDemoCommon.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

  const std::uint32_t CubeResolution = 512;

  struct PointLight
  {
    demo::Vec3 position{0.0f, 4.0f, 0.0f};
    demo::Vec3 colour{1.0f, 0.9f, 0.8f};
    float range = 30.0f;
    float intensity = 26.0f;
  };

  struct FrameUniforms
  {
    float viewProjection[16];
    float lightPosition[4];
    float lightColour[4];
    float shadowParameters[4];
  };

  struct ShadowUniforms
  {
    float lightViewProjection[16];
    float lightPosition[4];
    float lightParameters[4];
  };

  const char *frameBlock()
  {
    return "layout(std140) uniform FrameBlock {\n"
           "  mat4 viewProjection;\n"
           "  vec4 lightPosition;\n"
           "  vec4 lightColour;\n"
           "  vec4 shadowParameters;\n"
           "};\n";
  }

  std::string depthVertexSource(bool es)
  {
    return demo::preamble(es) +
           "layout(location = 0) in vec3 inPosition;\n"
           "layout(std140) uniform ShadowBlock {\n"
           "  mat4 lightViewProjection;\n"
           "  vec4 lightPosition;\n"
           "  vec4 lightParameters;\n"
           "};\n"
           "out vec3 lightRay;\n"
           "void main() {\n"
           "  lightRay = inPosition - lightPosition.xyz;\n"
           "  gl_Position = lightViewProjection * vec4(inPosition, 1.0);\n"
           "}\n";
  }

  std::string depthFragmentSource(bool es)
  {
    return demo::preamble(es) +
           "layout(std140) uniform ShadowBlock {\n"
           "  mat4 lightViewProjection;\n"
           "  vec4 lightPosition;\n"
           "  vec4 lightParameters;\n"
           "};\n"
           "in vec3 lightRay;\n"
           "void main() {\n"
           "  gl_FragDepth = clamp(length(lightRay) * lightParameters.x, 0.0,"
           " 1.0);\n"
           "}\n";
  }

  std::string sceneVertexSource(bool es)
  {
    return demo::preamble(es) +
           "layout(location = 0) in vec3 inPosition;\n"
           "layout(location = 1) in vec3 inNormal;\n" +
           frameBlock() +
           "out vec3 worldPosition;\n"
           "out vec3 worldNormal;\n"
           "void main() {\n"
           "  worldPosition = inPosition;\n"
           "  worldNormal = normalize(inNormal);\n"
           "  gl_Position = viewProjection * vec4(inPosition, 1.0);\n"
           "}\n";
  }

  std::string sceneFragmentSource(bool es)
  {
    return demo::preamble(es) +
           "uniform samplerCubeShadow pointShadow;\n" +
           frameBlock() +
           "in vec3 worldPosition;\n"
           "in vec3 worldNormal;\n"
           "out vec4 fragmentColour;\n"
           "void main() {\n"
           "  vec3 normal = normalize(worldNormal);\n"
           "  vec3 toLightVector = lightPosition.xyz - worldPosition;\n"
           "  float distanceToLight = length(toLightVector);\n"
           "  vec3 toLight = toLightVector / max(distanceToLight, 0.0001);\n"
           "  float slope = 1.0 - max(dot(normal, toLight), 0.0);\n"
           "  vec3 offset = worldPosition + normal * slope * shadowParameters.y;\n"
           "  vec3 lightRay = offset - lightPosition.xyz;\n"
           "  float reference = length(lightRay) * shadowParameters.x -\n"
           "                    shadowParameters.z;\n"
           "  float shadow = texture(pointShadow,\n"
           "                         vec4(lightRay, clamp(reference, 0.0, 1.0)));\n"
           "  float attenuation = 1.0 / (0.5 + 0.09 * distanceToLight +\n"
           "      0.032 * distanceToLight * distanceToLight);\n"
           "  attenuation *= max((lightColour.a > 0.0 ? 1.0 : 0.0), 0.0);\n"
           "  float lambert = max(dot(normal, toLight), 0.0);\n"
           "  vec3 ambient = vec3(0.10, 0.11, 0.14);\n"
           "  vec3 lit = lightColour.rgb * shadowParameters.w * lambert *\n"
           "             attenuation * shadow;\n"
           "  vec3 colour = vec3(0.72, 0.70, 0.66) * (ambient + lit);\n"
           "  colour = colour / (colour + vec3(1.0));\n"
           "  fragmentColour = vec4(pow(colour, vec3(1.0 / 2.2)), 1.0);\n"
           "}\n";
  }

  void writeMatrix(float *destination, const demo::Mat4 &source)
  {
    std::memcpy(destination, source.m, sizeof(source.m));
  }

  demo::Mat4 cubeFaceView(const demo::Vec3 &position, std::uint32_t face)
  {
    static const demo::Vec3 forward[6] = {
        {1.0f, 0.0f, 0.0f},  {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, -1.0f}};
    static const demo::Vec3 up[6] = {
        {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
    return demo::lookAt(position, demo::add(position, forward[face]), up[face]);
  }

} // namespace

int main(int argc, char **argv)
{
  const demo::Options options =
      demo::parseOptions(argc, argv, "shadow_point.ppm");
  const std::uint32_t captureWidth = 512;
  const std::uint32_t captureHeight = 384;
  demo::Host *host =
      demo::createHost("Shadow: point light", 1024, 640, options.preferES,
                        options.capture);
  if (!host)
    return 1;
  gpu::Device *device = demo::device(*host);
  const bool es = demo::isES(*host);
  std::printf("backend: %s\n", es ? "OpenGL ES" : "OpenGL desktop");

  demo::Scene scene = demo::createScene(*device);

  gpu::TextureDesc cubeDesc;
  cubeDesc.dimension = gpu::TextureDimension::TextureCube;
  cubeDesc.format = gpu::Format::Depth32Float;
  cubeDesc.width = CubeResolution;
  cubeDesc.height = CubeResolution;
  cubeDesc.usage = gpu::TextureUsageRenderTarget | gpu::TextureUsageSampled;
  cubeDesc.debugName = "point shadow cube";
  const gpu::TextureHandle shadowCube = device->createTexture(cubeDesc);

  gpu::SamplerDesc shadowSamplerDesc;
  shadowSamplerDesc.minFilter = gpu::Filter::Linear;
  shadowSamplerDesc.magFilter = gpu::Filter::Linear;
  shadowSamplerDesc.mipFilter = gpu::Filter::Nearest;
  shadowSamplerDesc.addressU = gpu::AddressMode::ClampToEdge;
  shadowSamplerDesc.addressV = gpu::AddressMode::ClampToEdge;
  shadowSamplerDesc.addressW = gpu::AddressMode::ClampToEdge;
  shadowSamplerDesc.compareEnabled = true;
  shadowSamplerDesc.compare = gpu::CompareOp::LessEqual;
  shadowSamplerDesc.debugName = "point shadow sampler";
  const gpu::SamplerHandle shadowSampler =
      device->createSampler(shadowSamplerDesc);

  const std::string depthVS = depthVertexSource(es);
  const std::string depthFS = depthFragmentSource(es);
  gpu::PipelineDesc depthPipelineDesc;
  depthPipelineDesc.vertex.source = demo::view(depthVS);
  depthPipelineDesc.fragment.source = demo::view(depthFS);
  depthPipelineDesc.vertexBufferCount = 1;
  depthPipelineDesc.vertexBuffers[0].stride = 24;
  depthPipelineDesc.vertexBuffers[0].attributeCount = 1;
  depthPipelineDesc.vertexBuffers[0].attributes[0].format =
      gpu::VertexFormat::Float32x3;
  depthPipelineDesc.colorTargetCount = 0;
  depthPipelineDesc.depthStencil.format = gpu::Format::Depth32Float;
  depthPipelineDesc.depthStencil.depthTestEnabled = true;
  depthPipelineDesc.depthStencil.depthWriteEnabled = true;
  depthPipelineDesc.depthStencil.depthCompare = gpu::CompareOp::LessEqual;
  depthPipelineDesc.raster.cullMode = gpu::CullMode::None;
  depthPipelineDesc.debugName = "point shadow pass";
  const gpu::PipelineHandle depthPipeline =
      device->createPipeline(depthPipelineDesc);

  const std::string sceneVS = sceneVertexSource(es);
  const std::string sceneFS = sceneFragmentSource(es);
  gpu::PipelineDesc scenePipelineDesc = depthPipelineDesc;
  scenePipelineDesc.vertex.source = demo::view(sceneVS);
  scenePipelineDesc.fragment.source = demo::view(sceneFS);
  scenePipelineDesc.vertexBuffers[0].attributeCount = 2;
  scenePipelineDesc.vertexBuffers[0].attributes[1].format =
      gpu::VertexFormat::Float32x3;
  scenePipelineDesc.vertexBuffers[0].attributes[1].offset = 12;
  scenePipelineDesc.vertexBuffers[0].attributes[1].shaderLocation = 1;
  scenePipelineDesc.colorTargetCount = 1;
  scenePipelineDesc.colorTargets[0].format = gpu::Format::RGBA8;
  scenePipelineDesc.raster.cullMode = gpu::CullMode::Back;
  scenePipelineDesc.debugName = "point scene pass";
  const gpu::PipelineHandle scenePipeline =
      device->createPipeline(scenePipelineDesc);

  gpu::BufferDesc frameBufferDesc;
  frameBufferDesc.size = sizeof(FrameUniforms);
  frameBufferDesc.usage = gpu::BufferUsageUniform;
  frameBufferDesc.debugName = "point frame uniforms";
  const gpu::BufferHandle frameBuffer = device->createBuffer(frameBufferDesc);

  gpu::BufferDesc shadowBufferDesc;
  shadowBufferDesc.size = sizeof(ShadowUniforms);
  shadowBufferDesc.usage = gpu::BufferUsageUniform;
  shadowBufferDesc.debugName = "point shadow uniforms";
  const gpu::BufferHandle shadowBuffer = device->createBuffer(shadowBufferDesc);

  if (demo::reportErrors(*device, "setup"))
  {
    demo::destroyHost(host);
    return 1;
  }

  gpu::TextureHandle captureTarget;
  gpu::TextureHandle captureDepth;
  if (options.capture)
  {
    gpu::TextureDesc colourDesc;
    colourDesc.format = gpu::Format::RGBA8;
    colourDesc.width = captureWidth;
    colourDesc.height = captureHeight;
    colourDesc.usage =
        gpu::TextureUsageRenderTarget | gpu::TextureUsageCopySource;
    colourDesc.debugName = "capture colour";
    captureTarget = device->createTexture(colourDesc);
    gpu::TextureDesc depthDesc = colourDesc;
    depthDesc.format = gpu::Format::Depth32Float;
    depthDesc.usage = gpu::TextureUsageRenderTarget;
    depthDesc.debugName = "capture depth";
    captureDepth = device->createTexture(depthDesc);
  }

  demo::Camera camera;
  camera.eye = {0.0f, 7.0f, 13.0f};
  PointLight light;
  std::uint32_t width = 1024;
  std::uint32_t height = 640;
  float time = 0.0f;
  std::uint32_t frameIndex = 0;

  while (demo::pumpEvents(*host))
  {
    time += 0.016f;
    light.position = {std::cos(time * 0.5f) * 2.5f, 4.0f,
                      std::sin(time * 0.5f) * 2.5f};
    demo::drawableSize(*host, width, height);
    if (options.capture)
    {
      width = captureWidth;
      height = captureHeight;
    }

    const demo::Mat4 faceProjection =
        demo::perspective(1.5707963f, 1.0f, 0.2f, light.range);

    for (std::uint32_t face = 0; face < 6; ++face)
    {
      ShadowUniforms shadowUniforms;
      std::memset(&shadowUniforms, 0, sizeof(shadowUniforms));
      writeMatrix(shadowUniforms.lightViewProjection,
                  demo::multiply(faceProjection,
                                 cubeFaceView(light.position, face)));
      shadowUniforms.lightPosition[0] = light.position.x;
      shadowUniforms.lightPosition[1] = light.position.y;
      shadowUniforms.lightPosition[2] = light.position.z;
      shadowUniforms.lightParameters[0] = 1.0f / light.range;
      device->updateBuffer(shadowBuffer, 0,
                           {&shadowUniforms, sizeof(shadowUniforms)});

      gpu::RenderPassDesc shadowPass;
      shadowPass.colorCount = 0;
      shadowPass.hasDepthStencil = true;
      shadowPass.depthStencil.target.texture = shadowCube;
      shadowPass.depthStencil.target.layer = face;
      shadowPass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
      shadowPass.depthStencil.clearDepth = 1.0f;
      if (device->beginRenderPass(shadowPass))
      {
        gpu::Viewport viewport;
        viewport.width = static_cast<float>(CubeResolution);
        viewport.height = static_cast<float>(CubeResolution);
        device->setViewport(viewport);
        device->setPipeline(depthPipeline);
        device->bindUniformBuffer(0, shadowBuffer, 0, sizeof(ShadowUniforms));
        device->bindVertexBuffer(0, scene.vertices, 0);
        device->draw(scene.vertexCount);
        device->endRenderPass();
      }
    }

    FrameUniforms frame;
    std::memset(&frame, 0, sizeof(frame));
    writeMatrix(frame.viewProjection,
                demo::multiply(camera.projection(static_cast<float>(width) /
                                                 static_cast<float>(height)),
                               camera.view()));
    frame.lightPosition[0] = light.position.x;
    frame.lightPosition[1] = light.position.y;
    frame.lightPosition[2] = light.position.z;
    frame.lightColour[0] = light.colour.x;
    frame.lightColour[1] = light.colour.y;
    frame.lightColour[2] = light.colour.z;
    frame.lightColour[3] = 1.0f;
    frame.shadowParameters[0] = 1.0f / light.range;
    frame.shadowParameters[1] = 0.08f;
    frame.shadowParameters[2] = 0.004f;
    frame.shadowParameters[3] = light.intensity;
    device->updateBuffer(frameBuffer, 0, {&frame, sizeof(frame)});

    gpu::RenderPassDesc scenePass;
    scenePass.colorCount = 1;
    scenePass.colors[0].surface = !options.capture;
    scenePass.hasDepthStencil = true;
    scenePass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
    scenePass.depthStencil.clearDepth = 1.0f;
    if (options.capture)
    {
      scenePass.colors[0].target.texture = captureTarget;
      scenePass.depthStencil.target.texture = captureDepth;
    }
    scenePass.colors[0].loadOp = gpu::LoadOp::Clear;
    scenePass.colors[0].clearColor[0] = 0.04f;
    scenePass.colors[0].clearColor[1] = 0.04f;
    scenePass.colors[0].clearColor[2] = 0.06f;
    scenePass.colors[0].clearColor[3] = 1.0f;
    if (device->beginRenderPass(scenePass))
    {
      gpu::Viewport viewport;
      viewport.width = static_cast<float>(width);
      viewport.height = static_cast<float>(height);
      device->setViewport(viewport);
      device->setPipeline(scenePipeline);
      device->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
      device->bindTexture(0, shadowCube, shadowSampler);
      device->bindVertexBuffer(0, scene.vertices, 0);
      device->draw(scene.vertexCount);
      device->endRenderPass();
    }

    if (!options.capture)
      device->present();
    if (demo::reportErrors(*device, "frame"))
      break;

    ++frameIndex;
    if (options.frames != 0 && frameIndex >= options.frames)
    {
      if (options.capture)
      {
        std::vector<std::uint8_t> pixels(captureWidth * captureHeight * 4);
        gpu::TextureRegion region;
        region.width = captureWidth;
        region.height = captureHeight;
        if (device->readTexture(captureTarget, region,
                                {pixels.data(), pixels.size()}))
        {
          demo::writePPM(options.capturePath.c_str(), pixels.data(),
                         captureWidth, captureHeight);
          std::printf("captura: %s\n", options.capturePath.c_str());
          demo::describeCapture(pixels.data(), captureWidth, captureHeight);
        }
        demo::reportErrors(*device, "capture");
      }
      break;
    }
  }

  if (captureDepth.valid())
    device->destroy(captureDepth);
  if (captureTarget.valid())
    device->destroy(captureTarget);
  device->destroy(shadowBuffer);
  device->destroy(frameBuffer);
  device->destroy(scenePipeline);
  device->destroy(depthPipeline);
  device->destroy(shadowSampler);
  device->destroy(shadowCube);
  demo::destroyScene(*device, scene);
  demo::destroyHost(host);
  return 0;
}
