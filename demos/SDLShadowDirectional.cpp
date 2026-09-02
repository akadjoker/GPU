#include "ShadowDemoCommon.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

  const std::uint32_t CascadeResolution = 1024;
  const std::uint32_t CascadeCount = 4;

  struct DirectionalLight
  {
    demo::Vec3 direction{-0.45f, -1.0f, -0.35f};
    demo::Vec3 colour{1.0f, 0.96f, 0.88f};
    float intensity = 2.6f;
  };

  struct FrameUniforms
  {
    float viewProjection[16];
    float cascadeViewProjection[CascadeCount][16];
    float cascadeSplits[4];
    float lightDirection[4];
    float lightColour[4];
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
           "  mat4 cascadeViewProjection[4];\n"
           "  vec4 cascadeSplits;\n"
           "  vec4 lightDirection;\n"
           "  vec4 lightColour;\n"
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
           "out float viewDepth;\n"
           "void main() {\n"
           "  worldPosition = inPosition;\n"
           "  worldNormal = normalize(inNormal);\n"
           "  vec4 clip = viewProjection * vec4(inPosition, 1.0);\n"
           "  viewDepth = clip.w;\n"
           "  gl_Position = clip;\n"
           "}\n";
  }

  std::string sceneFragmentSource(bool es)
  {
    return demo::preamble(es) +
           "uniform sampler2DArrayShadow cascades;\n" +
           frameBlock() +
           "in vec3 worldPosition;\n"
           "in vec3 worldNormal;\n"
           "in float viewDepth;\n"
           "out vec4 fragmentColour;\n"
           "float sampleCascade(int index, vec3 position) {\n"
           "  vec4 coord = cascadeViewProjection[index] * vec4(position, 1.0);\n"
           "  vec3 projected = coord.xyz / coord.w;\n"
           "  projected = projected * 0.5 + 0.5;\n"
           "  if (projected.x < 0.0 || projected.x > 1.0 ||\n"
           "      projected.y < 0.0 || projected.y > 1.0 ||\n"
           "      projected.z > 1.0) return 1.0;\n"
           "  float texel = shadowParameters.x;\n"
           "  float sum = 0.0;\n"
           "  for (int y = -1; y <= 1; ++y)\n"
           "    for (int x = -1; x <= 1; ++x)\n"
           "      sum += texture(cascades,\n"
           "          vec4(projected.xy + vec2(float(x), float(y)) * texel,\n"
           "               float(index), projected.z));\n"
           "  return sum / 9.0;\n"
           "}\n"
           "void main() {\n"
           "  vec3 normal = normalize(worldNormal);\n"
           "  vec3 toLight = normalize(-lightDirection.xyz);\n"
           "  float slope = 1.0 - max(dot(normal, toLight), 0.0);\n"
           "  vec3 offset = worldPosition + normal * slope * shadowParameters.y;\n"
           "  int index = 3;\n"
           "  if (viewDepth < cascadeSplits.x) index = 0;\n"
           "  else if (viewDepth < cascadeSplits.y) index = 1;\n"
           "  else if (viewDepth < cascadeSplits.z) index = 2;\n"
           "  float shadow = sampleCascade(index, offset);\n"
           "  if (index < 3) {\n"
           "    float lower = index == 0 ? 0.0 :\n"
           "                  (index == 1 ? cascadeSplits.x : cascadeSplits.y);\n"
           "    float upper = index == 0 ? cascadeSplits.x :\n"
           "                  (index == 1 ? cascadeSplits.y : cascadeSplits.z);\n"
           "    float band = (upper - lower) * shadowParameters.z;\n"
           "    float blend = smoothstep(upper - band, upper, viewDepth);\n"
           "    if (blend > 0.0)\n"
           "      shadow = mix(shadow, sampleCascade(index + 1, offset), blend);\n"
           "  }\n"
           "  shadow = mix(shadow, 1.0,\n"
           "               smoothstep(shadowParameters.w * 0.75,\n"
           "                          shadowParameters.w, viewDepth));\n"
           "  float lambert = max(dot(normal, toLight), 0.0);\n"
           "  vec3 ambient = vec3(0.12, 0.13, 0.16);\n"
           "  vec3 lit = lightColour.rgb * lightColour.a * lambert * shadow;\n"
           "  vec3 colour = vec3(0.72, 0.70, 0.66) * (ambient + lit);\n"
           "  colour = colour / (colour + vec3(1.0));\n"
           "  fragmentColour = vec4(pow(colour, vec3(1.0 / 2.2)), 1.0);\n"
           "}\n";
  }

  void writeMatrix(float *destination, const demo::Mat4 &source)
  {
    std::memcpy(destination, source.m, sizeof(source.m));
  }

  float snapTo(float value, float unit)
  {
    return unit > 0.0f ? std::floor(value / unit) * unit : value;
  }

  demo::Mat4 buildCascade(const demo::Camera &camera, float aspect,
                          const demo::Vec3 &lightDirection, float nearSplit,
                          float farSplit, float extrude)
  {
    demo::Vec3 corners[8];
    demo::frustumCorners(camera.eye, camera.forward(), camera.up,
                         camera.fovYRadians, aspect, nearSplit, farSplit,
                         corners);

    demo::Vec3 centre{0.0f, 0.0f, 0.0f};
    for (const demo::Vec3 &corner : corners)
      centre = demo::add(centre, corner);
    centre = demo::scale(centre, 1.0f / 8.0f);

    float radius = 0.0f;
    for (const demo::Vec3 &corner : corners)
      radius = std::max(radius, demo::length(demo::subtract(corner, centre)));
    radius *= static_cast<float>(CascadeResolution) /
              static_cast<float>(CascadeResolution - 2);

    const demo::Vec3 forward = demo::normalize(lightDirection);
    const demo::Vec3 reference =
        std::fabs(forward.y) > 0.95f ? demo::Vec3{0.0f, 0.0f, 1.0f}
                                     : demo::Vec3{0.0f, 1.0f, 0.0f};
    const demo::Vec3 right = demo::normalize(demo::cross(forward, reference));
    const demo::Vec3 up = demo::cross(right, forward);

    const float unit = radius * 2.0f / static_cast<float>(CascadeResolution);
    const float centreX = snapTo(demo::dot(right, centre), unit);
    const float centreY = snapTo(demo::dot(up, centre), unit);
    const float centreZ = demo::dot(forward, centre);

    const demo::Vec3 snappedCentre =
        demo::add(demo::add(demo::scale(right, centreX),
                            demo::scale(up, centreY)),
                  demo::scale(forward, centreZ));
    const demo::Vec3 eye =
        demo::subtract(snappedCentre, demo::scale(forward, radius + extrude));

    const demo::Mat4 view = demo::lookAt(eye, snappedCentre, up);
    const demo::Mat4 projection = demo::orthographic(
        -radius, radius, -radius, radius, 0.0f, radius * 2.0f + extrude);
    return demo::multiply(projection, view);
  }

} // namespace

int main(int argc, char **argv)
{
  const demo::Options options =
      demo::parseOptions(argc, argv, "shadow_directional.ppm");
  const std::uint32_t captureWidth = 512;
  const std::uint32_t captureHeight = 384;
  demo::Host *host = demo::createHost("Shadow: directional cascades", 1024, 640,
                                      options.preferES, options.capture);
  if (!host)
    return 1;
  gpu::Device *device = demo::device(*host);
  const bool es = demo::isES(*host);
  std::printf("backend: %s\n", es ? "OpenGL ES" : "OpenGL desktop");

  demo::Scene scene = demo::createScene(*device);

  gpu::TextureDesc cascadeDesc;
  cascadeDesc.dimension = gpu::TextureDimension::Texture2DArray;
  cascadeDesc.format = gpu::Format::Depth32Float;
  cascadeDesc.width = CascadeResolution;
  cascadeDesc.height = CascadeResolution;
  cascadeDesc.depthOrLayers = CascadeCount;
  cascadeDesc.usage = gpu::TextureUsageRenderTarget | gpu::TextureUsageSampled;
  cascadeDesc.debugName = "directional cascades";
  const gpu::TextureHandle cascades = device->createTexture(cascadeDesc);

  gpu::SamplerDesc shadowSamplerDesc;
  shadowSamplerDesc.minFilter = gpu::Filter::Linear;
  shadowSamplerDesc.magFilter = gpu::Filter::Linear;
  shadowSamplerDesc.mipFilter = gpu::Filter::Nearest;
  shadowSamplerDesc.addressU = gpu::AddressMode::ClampToEdge;
  shadowSamplerDesc.addressV = gpu::AddressMode::ClampToEdge;
  shadowSamplerDesc.compareEnabled = true;
  shadowSamplerDesc.compare = gpu::CompareOp::LessEqual;
  shadowSamplerDesc.debugName = "cascade sampler";
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
  depthPipelineDesc.raster.depthBiasConstant = 3.0f;
  depthPipelineDesc.raster.depthBiasSlope = 2.0f;
  depthPipelineDesc.debugName = "cascade shadow pass";
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
  scenePipelineDesc.debugName = "cascade scene pass";
  const gpu::PipelineHandle scenePipeline =
      device->createPipeline(scenePipelineDesc);

  gpu::BufferDesc frameBufferDesc;
  frameBufferDesc.size = sizeof(FrameUniforms);
  frameBufferDesc.usage = gpu::BufferUsageUniform;
  frameBufferDesc.debugName = "cascade frame uniforms";
  const gpu::BufferHandle frameBuffer = device->createBuffer(frameBufferDesc);

  gpu::BufferDesc shadowBufferDesc;
  shadowBufferDesc.size = sizeof(ShadowUniforms);
  shadowBufferDesc.usage = gpu::BufferUsageUniform;
  shadowBufferDesc.debugName = "cascade shadow uniforms";
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
  DirectionalLight light;
  const float shadowFar = 45.0f;
  const float lambda = 0.85f;
  std::uint32_t width = 1024;
  std::uint32_t height = 640;
  float time = 0.0f;
  std::uint32_t frameIndex = 0;

  while (demo::pumpEvents(*host))
  {
    time += 0.016f;
    light.direction = demo::normalize(
        {std::cos(time * 0.25f) * 0.6f, -1.0f, std::sin(time * 0.25f) * 0.6f});
    demo::drawableSize(*host, width, height);
    if (options.capture)
    {
      width = captureWidth;
      height = captureHeight;
    }
    const float aspect =
        static_cast<float>(width) / static_cast<float>(height);

    float splits[CascadeCount + 1];
    splits[0] = camera.nearPlane;
    for (std::uint32_t index = 1; index <= CascadeCount; ++index)
    {
      const float ratio =
          static_cast<float>(index) / static_cast<float>(CascadeCount);
      const float uniformSplit =
          camera.nearPlane + (shadowFar - camera.nearPlane) * ratio;
      const float logSplit =
          camera.nearPlane * std::pow(shadowFar / camera.nearPlane, ratio);
      splits[index] = logSplit * lambda + uniformSplit * (1.0f - lambda);
    }

    demo::Mat4 cascadeMatrices[CascadeCount];
    for (std::uint32_t index = 0; index < CascadeCount; ++index)
    {
      cascadeMatrices[index] =
          buildCascade(camera, aspect, light.direction, splits[index],
                       splits[index + 1], 60.0f);

      ShadowUniforms shadowUniforms;
      writeMatrix(shadowUniforms.lightViewProjection, cascadeMatrices[index]);
      device->updateBuffer(shadowBuffer, 0,
                           {&shadowUniforms, sizeof(shadowUniforms)});

      gpu::RenderPassDesc shadowPass;
      shadowPass.colorCount = 0;
      shadowPass.hasDepthStencil = true;
      shadowPass.depthStencil.target.texture = cascades;
      shadowPass.depthStencil.target.layer = index;
      shadowPass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
      shadowPass.depthStencil.clearDepth = 1.0f;
      if (device->beginRenderPass(shadowPass))
      {
        gpu::Viewport viewport;
        viewport.width = static_cast<float>(CascadeResolution);
        viewport.height = static_cast<float>(CascadeResolution);
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
                demo::multiply(camera.projection(aspect), camera.view()));
    for (std::uint32_t index = 0; index < CascadeCount; ++index)
      writeMatrix(frame.cascadeViewProjection[index], cascadeMatrices[index]);
    for (std::uint32_t index = 0; index < 4; ++index)
      frame.cascadeSplits[index] =
          index < CascadeCount ? splits[index + 1] : shadowFar;
    frame.lightDirection[0] = light.direction.x;
    frame.lightDirection[1] = light.direction.y;
    frame.lightDirection[2] = light.direction.z;
    frame.lightColour[0] = light.colour.x;
    frame.lightColour[1] = light.colour.y;
    frame.lightColour[2] = light.colour.z;
    frame.lightColour[3] = light.intensity;
    frame.shadowParameters[0] = 1.0f / static_cast<float>(CascadeResolution);
    frame.shadowParameters[1] = 0.06f;
    frame.shadowParameters[2] = 0.25f;
    frame.shadowParameters[3] = shadowFar;
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
    scenePass.colors[0].clearColor[0] = 0.30f;
    scenePass.colors[0].clearColor[1] = 0.42f;
    scenePass.colors[0].clearColor[2] = 0.58f;
    scenePass.colors[0].clearColor[3] = 1.0f;
    if (device->beginRenderPass(scenePass))
    {
      gpu::Viewport viewport;
      viewport.width = static_cast<float>(width);
      viewport.height = static_cast<float>(height);
      device->setViewport(viewport);
      device->setPipeline(scenePipeline);
      device->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
      device->bindTexture(0, cascades, shadowSampler);
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
  device->destroy(cascades);
  demo::destroyScene(*device, scene);
  demo::destroyHost(host);
  return 0;
}
