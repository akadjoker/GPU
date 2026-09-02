#include "ShadowDemoCommon.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

  const std::uint32_t ShadowResolution = 1024;

  struct SpotLight
  {
    demo::Vec3 position{4.0f, 9.0f, 5.0f};
    demo::Vec3 target{0.0f, 0.0f, 0.0f};
    demo::Vec3 colour{1.0f, 0.95f, 0.85f};
    float range = 40.0f;
    float innerRadians = 0.55f;
    float outerRadians = 0.75f;
    float intensity = 22.0f;
  };

  struct FrameUniforms
  {
    float viewProjection[16];
    float lightViewProjection[16];
    float lightPosition[4];
    float lightDirection[4];
    float lightColour[4];
    float lightParameters[4];
    float shadowParameters[4];
  };

  struct ShadowUniforms
  {
    float lightViewProjection[16];
  };

  const char *frameBlock()
  {
    return "layout(std140) uniform FrameBlock {\n"
           "  mat4 viewProjection;\n"
           "  mat4 lightViewProjection;\n"
           "  vec4 lightPosition;\n"
           "  vec4 lightDirection;\n"
           "  vec4 lightColour;\n"
           "  vec4 lightParameters;\n"
           "  vec4 shadowParameters;\n"
           "};\n";
  }

  std::string depthVertexSource(bool es)
  {
    return demo::preamble(es) +
           "layout(location = 0) in vec3 inPosition;\n"
           "layout(std140) uniform ShadowBlock { mat4 lightViewProjection; };\n"
           "void main() {\n"
           "  gl_Position = lightViewProjection * vec4(inPosition, 1.0);\n"
           "}\n";
  }

  std::string depthFragmentSource(bool es)
  {
    return demo::preamble(es) + "void main() {}\n";
  }

  std::string sceneVertexSource(bool es)
  {
    return demo::preamble(es) +
           "layout(location = 0) in vec3 inPosition;\n"
           "layout(location = 1) in vec3 inNormal;\n" +
           frameBlock() +
           "out vec3 worldPosition;\n"
           "out vec3 worldNormal;\n"
           "out vec4 shadowCoord;\n"
           "void main() {\n"
           "  worldPosition = inPosition;\n"
           "  worldNormal = normalize(inNormal);\n"
           "  vec3 toLight = normalize(lightPosition.xyz - inPosition);\n"
           "  float slope = 1.0 - max(dot(worldNormal, toLight), 0.0);\n"
           "  vec3 offset = inPosition + worldNormal * slope * shadowParameters.y;\n"
           "  shadowCoord = lightViewProjection * vec4(offset, 1.0);\n"
           "  gl_Position = viewProjection * vec4(inPosition, 1.0);\n"
           "}\n";
  }

  std::string sceneFragmentSource(bool es)
  {
    return demo::preamble(es) +
           "uniform sampler2DShadow shadowMap;\n" +
           frameBlock() +
           "in vec3 worldPosition;\n"
           "in vec3 worldNormal;\n"
           "in vec4 shadowCoord;\n"
           "out vec4 fragmentColour;\n"
           "float samplePcf(vec4 coord) {\n"
           "  if (coord.w <= 0.0) return 1.0;\n"
           "  vec3 projected = coord.xyz / coord.w;\n"
           "  projected = projected * 0.5 + 0.5;\n"
           "  if (projected.x < 0.0 || projected.x > 1.0 ||\n"
           "      projected.y < 0.0 || projected.y > 1.0 ||\n"
           "      projected.z > 1.0) return 1.0;\n"
           "  float texel = shadowParameters.x;\n"
           "  float sum = 0.0;\n"
           "  for (int y = -1; y <= 1; ++y)\n"
           "    for (int x = -1; x <= 1; ++x)\n"
           "      sum += texture(shadowMap,\n"
           "          vec3(projected.xy + vec2(float(x), float(y)) * texel,\n"
           "               projected.z));\n"
           "  return sum / 9.0;\n"
           "}\n"
           "void main() {\n"
           "  vec3 normal = normalize(worldNormal);\n"
           "  vec3 toLight = lightPosition.xyz - worldPosition;\n"
           "  float distanceToLight = length(toLight);\n"
           "  toLight /= max(distanceToLight, 0.0001);\n"
           "  float attenuation = 1.0 / (0.5 + 0.09 * distanceToLight +\n"
           "      0.032 * distanceToLight * distanceToLight);\n"
           "  attenuation *= max((lightParameters.z - distanceToLight) /\n"
           "                     max(lightParameters.z, 0.0001), 0.0);\n"
           "  float spotCosine = dot(-toLight, normalize(lightDirection.xyz));\n"
           "  float spotFalloff = clamp((spotCosine - lightParameters.y) *\n"
           "                            lightParameters.x, 0.0, 1.0);\n"
           "  attenuation *= spotFalloff * spotFalloff;\n"
           "  float lambert = max(dot(normal, toLight), 0.0);\n"
           "  float shadow = samplePcf(shadowCoord);\n"
           "  vec3 ambient = vec3(0.10, 0.11, 0.14);\n"
           "  vec3 lit = lightColour.rgb * lightColour.a * lambert *\n"
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

} // namespace

int main(int argc, char **argv)
{
  const demo::Options options =
      demo::parseOptions(argc, argv, "shadow_spot.ppm");
  const std::uint32_t captureWidth = 512;
  const std::uint32_t captureHeight = 384;
  demo::Host *host = demo::createHost("Shadow: spot light", 1024, 640,
                                      options.preferES, options.capture);
  if (!host)
    return 1;
  gpu::Device *device = demo::device(*host);
  const bool es = demo::isES(*host);
  std::printf("backend: %s\n", es ? "OpenGL ES" : "OpenGL desktop");

  demo::Scene scene = demo::createScene(*device);

  gpu::TextureDesc shadowDesc;
  shadowDesc.format = gpu::Format::Depth32Float;
  shadowDesc.width = ShadowResolution;
  shadowDesc.height = ShadowResolution;
  shadowDesc.usage = gpu::TextureUsageRenderTarget | gpu::TextureUsageSampled;
  shadowDesc.debugName = "spot shadow map";
  const gpu::TextureHandle shadowMap = device->createTexture(shadowDesc);

  gpu::SamplerDesc shadowSamplerDesc;
  shadowSamplerDesc.minFilter = gpu::Filter::Linear;
  shadowSamplerDesc.magFilter = gpu::Filter::Linear;
  shadowSamplerDesc.mipFilter = gpu::Filter::Nearest;
  shadowSamplerDesc.addressU = gpu::AddressMode::ClampToEdge;
  shadowSamplerDesc.addressV = gpu::AddressMode::ClampToEdge;
  shadowSamplerDesc.compareEnabled = true;
  shadowSamplerDesc.compare = gpu::CompareOp::LessEqual;
  shadowSamplerDesc.debugName = "spot shadow sampler";
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
  depthPipelineDesc.raster.depthBiasConstant = 4.0f;
  depthPipelineDesc.raster.depthBiasSlope = 2.0f;
  depthPipelineDesc.debugName = "spot shadow pass";
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
  scenePipelineDesc.raster.depthBiasConstant = 0.0f;
  scenePipelineDesc.raster.depthBiasSlope = 0.0f;
  scenePipelineDesc.debugName = "spot scene pass";
  const gpu::PipelineHandle scenePipeline =
      device->createPipeline(scenePipelineDesc);

  gpu::BufferDesc frameBufferDesc;
  frameBufferDesc.size = sizeof(FrameUniforms);
  frameBufferDesc.usage = gpu::BufferUsageUniform;
  frameBufferDesc.debugName = "spot frame uniforms";
  const gpu::BufferHandle frameBuffer = device->createBuffer(frameBufferDesc);

  gpu::BufferDesc shadowBufferDesc;
  shadowBufferDesc.size = sizeof(ShadowUniforms);
  shadowBufferDesc.usage = gpu::BufferUsageUniform;
  shadowBufferDesc.debugName = "spot shadow uniforms";
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
  SpotLight light;
  std::uint32_t width = 1024;
  std::uint32_t height = 640;
  float time = 0.0f;
  std::uint32_t frameIndex = 0;

  while (demo::pumpEvents(*host))
  {
    time += 0.016f;
    light.position = {std::cos(time * 0.4f) * 6.0f, 11.0f,
                      std::sin(time * 0.4f) * 6.0f};
    demo::drawableSize(*host, width, height);
    if (options.capture)
    {
      width = captureWidth;
      height = captureHeight;
    }

    const demo::Mat4 lightView =
        demo::lookAt(light.position, light.target, {0.0f, 1.0f, 0.0f});
    const demo::Mat4 lightProjection =
        demo::perspective(light.outerRadians * 2.0f, 1.0f, 0.5f, light.range);
    const demo::Mat4 lightViewProjection =
        demo::multiply(lightProjection, lightView);

    ShadowUniforms shadowUniforms;
    writeMatrix(shadowUniforms.lightViewProjection, lightViewProjection);
    device->updateBuffer(shadowBuffer, 0,
                         {&shadowUniforms, sizeof(shadowUniforms)});

    gpu::RenderPassDesc shadowPass;
    shadowPass.colorCount = 0;
    shadowPass.hasDepthStencil = true;
    shadowPass.depthStencil.target.texture = shadowMap;
    shadowPass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
    shadowPass.depthStencil.clearDepth = 1.0f;
    if (device->beginRenderPass(shadowPass))
    {
      gpu::Viewport viewport;
      viewport.width = static_cast<float>(ShadowResolution);
      viewport.height = static_cast<float>(ShadowResolution);
      device->setViewport(viewport);
      device->setPipeline(depthPipeline);
      device->bindUniformBuffer(0, shadowBuffer, 0, sizeof(ShadowUniforms));
      device->bindVertexBuffer(0, scene.vertices, 0);
      device->draw(scene.vertexCount);
      device->endRenderPass();
    }

    const demo::Mat4 viewProjection = demo::multiply(
        camera.projection(static_cast<float>(width) /
                          static_cast<float>(height)),
        camera.view());
    const demo::Vec3 lightDirection =
        demo::normalize(demo::subtract(light.target, light.position));

    FrameUniforms frame;
    std::memset(&frame, 0, sizeof(frame));
    writeMatrix(frame.viewProjection, viewProjection);
    writeMatrix(frame.lightViewProjection, lightViewProjection);
    frame.lightPosition[0] = light.position.x;
    frame.lightPosition[1] = light.position.y;
    frame.lightPosition[2] = light.position.z;
    frame.lightDirection[0] = lightDirection.x;
    frame.lightDirection[1] = lightDirection.y;
    frame.lightDirection[2] = lightDirection.z;
    frame.lightColour[0] = light.colour.x;
    frame.lightColour[1] = light.colour.y;
    frame.lightColour[2] = light.colour.z;
    frame.lightColour[3] = light.intensity;
    frame.lightParameters[0] =
        1.0f / std::max(std::cos(light.innerRadians) -
                            std::cos(light.outerRadians),
                        0.0001f);
    frame.lightParameters[1] = std::cos(light.outerRadians);
    frame.lightParameters[2] = light.range;
    frame.shadowParameters[0] = 1.0f / static_cast<float>(ShadowResolution);
    frame.shadowParameters[1] = 0.05f;
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
    scenePass.colors[0].clearColor[0] = 0.05f;
    scenePass.colors[0].clearColor[1] = 0.06f;
    scenePass.colors[0].clearColor[2] = 0.08f;
    scenePass.colors[0].clearColor[3] = 1.0f;
    if (device->beginRenderPass(scenePass))
    {
      gpu::Viewport viewport;
      viewport.width = static_cast<float>(width);
      viewport.height = static_cast<float>(height);
      device->setViewport(viewport);
      device->setPipeline(scenePipeline);
      device->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
      device->bindTexture(0, shadowMap, shadowSampler);
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
  device->destroy(shadowMap);
  demo::destroyScene(*device, scene);
  demo::destroyHost(host);
  return 0;
}
