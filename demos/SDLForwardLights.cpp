#include "ShadowDemoCommon.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

  const std::uint32_t SceneLightCount = 9;
  const std::uint32_t LightsPerObject = 4;

  struct Light
  {
    demo::Vec3 position;
    demo::Vec3 direction{0.0f, -1.0f, 0.0f};
    demo::Vec3 colour{1.0f, 1.0f, 1.0f};
    float range = 14.0f;
    float intensity = 12.0f;
    bool directional = false;
    bool castsShadow = false;
    float squaredDistance = 0.0f;
  };

  struct GpuLight
  {
    float position[4];
    float direction[4];
    float colour[4];
    float parameters[4];
  };

  struct ObjectUniforms
  {
    GpuLight lights[LightsPerObject];
    float lightCount[4];
  };

  struct FrameUniforms
  {
    float viewProjection[16];
  };

  const char *lightBlock()
  {
    return "struct GpuLight {\n"
           "  vec4 position;\n"
           "  vec4 direction;\n"
           "  vec4 colour;\n"
           "  vec4 parameters;\n"
           "};\n"
           "layout(std140) uniform ObjectBlock {\n"
           "  GpuLight lights[4];\n"
           "  vec4 lightCount;\n"
           "};\n";
  }

  std::string vertexSource(bool es)
  {
    return demo::preamble(es) +
           "layout(location = 0) in vec3 inPosition;\n"
           "layout(location = 1) in vec3 inNormal;\n"
           "layout(std140) uniform FrameBlock { mat4 viewProjection; };\n"
           "out vec3 worldPosition;\n"
           "out vec3 worldNormal;\n"
           "void main() {\n"
           "  worldPosition = inPosition;\n"
           "  worldNormal = normalize(inNormal);\n"
           "  gl_Position = viewProjection * vec4(inPosition, 1.0);\n"
           "}\n";
  }

  std::string fragmentSource(bool es)
  {
    return demo::preamble(es) + lightBlock() +
           "in vec3 worldPosition;\n"
           "in vec3 worldNormal;\n"
           "out vec4 fragmentColour;\n"
           "void main() {\n"
           "  vec3 normal = normalize(worldNormal);\n"
           "  vec3 accumulated = vec3(0.06, 0.065, 0.08);\n"
           "  int count = int(lightCount.x);\n"
           "  for (int index = 0; index < 4; ++index) {\n"
           "    if (index >= count) break;\n"
           "    GpuLight light = lights[index];\n"
           "    vec3 toLight;\n"
           "    float attenuation;\n"
           "    if (light.parameters.w > 0.5) {\n"
           "      toLight = normalize(-light.direction.xyz);\n"
           "      attenuation = 1.0;\n"
           "    } else {\n"
           "      vec3 delta = light.position.xyz - worldPosition;\n"
           "      float distanceToLight = length(delta);\n"
           "      toLight = delta / max(distanceToLight, 0.0001);\n"
           "      attenuation = 1.0 / (0.5 + 0.09 * distanceToLight +\n"
           "          0.032 * distanceToLight * distanceToLight);\n"
           "      attenuation *= max((light.parameters.x - distanceToLight) /\n"
           "                         max(light.parameters.x, 0.0001), 0.0);\n"
           "    }\n"
           "    float lambert = max(dot(normal, toLight), 0.0);\n"
           "    accumulated += light.colour.rgb * light.parameters.y * lambert *\n"
           "                   attenuation;\n"
           "  }\n"
           "  vec3 colour = vec3(0.74, 0.72, 0.68) * accumulated;\n"
           "  colour = colour / (colour + vec3(1.0));\n"
           "  fragmentColour = vec4(pow(colour, vec3(1.0 / 2.2)), 1.0);\n"
           "}\n";
  }

  std::uint32_t selectLights(const demo::SceneObject &object,
                             std::vector<Light> &lights, GpuLight *destination)
  {
    std::vector<Light *> candidates;
    candidates.reserve(lights.size());
    std::uint32_t shadowCasters = 0;

    for (Light &light : lights)
    {
      if (light.directional)
      {
        candidates.push_back(&light);
        ++shadowCasters;
        continue;
      }
      const demo::Vec3 delta = demo::subtract(light.position, object.centre);
      const float distance = demo::length(delta);
      if (distance > light.range + object.radius)
        continue;
      light.squaredDistance = distance * distance;
      if (light.castsShadow)
      {
        candidates.push_back(&light);
        ++shadowCasters;
      }
    }
    for (Light &light : lights)
    {
      if (light.directional || light.castsShadow)
        continue;
      const demo::Vec3 delta = demo::subtract(light.position, object.centre);
      const float distance = demo::length(delta);
      if (distance > light.range + object.radius)
        continue;
      light.squaredDistance = distance * distance;
      candidates.push_back(&light);
    }

    std::stable_sort(candidates.begin() + shadowCasters, candidates.end(),
                     [](const Light *a, const Light *b) {
                       return a->squaredDistance < b->squaredDistance;
                     });

    const std::uint32_t used = static_cast<std::uint32_t>(
        std::min<std::size_t>(candidates.size(), LightsPerObject));
    for (std::uint32_t index = 0; index < used; ++index)
    {
      const Light &light = *candidates[index];
      GpuLight &out = destination[index];
      std::memset(&out, 0, sizeof(out));
      out.position[0] = light.position.x;
      out.position[1] = light.position.y;
      out.position[2] = light.position.z;
      out.direction[0] = light.direction.x;
      out.direction[1] = light.direction.y;
      out.direction[2] = light.direction.z;
      out.colour[0] = light.colour.x;
      out.colour[1] = light.colour.y;
      out.colour[2] = light.colour.z;
      out.parameters[0] = light.range;
      out.parameters[1] = light.intensity;
      out.parameters[3] = light.directional ? 1.0f : 0.0f;
    }
    return used;
  }

} // namespace

int main(int argc, char **argv)
{
  const demo::Options options =
      demo::parseOptions(argc, argv, "forward_lights.ppm");
  const std::uint32_t captureWidth = 512;
  const std::uint32_t captureHeight = 384;
  demo::Host *host = demo::createHost("Forward: per-object light list", 1024,
                                      640, options.preferES, options.capture);
  if (!host)
    return 1;
  gpu::Device *device = demo::device(*host);
  const bool es = demo::isES(*host);
  const gpu::GPUCapabilities &capabilities = device->capabilities();
  std::printf("backend: %s | alinhamento de UBO: %u | tamanho maximo: %u\n",
              es ? "OpenGL ES" : "OpenGL desktop",
              capabilities.uniformBufferOffsetAlignment,
              capabilities.maxUniformBufferSize);

  demo::Scene scene = demo::createScene(*device);

  const std::uint32_t alignment =
      std::max(capabilities.uniformBufferOffsetAlignment, 1u);
  const std::uint32_t objectStride =
      ((static_cast<std::uint32_t>(sizeof(ObjectUniforms)) + alignment - 1) /
       alignment) *
      alignment;
  std::printf("objetos: %u | stride por objeto no UBO: %u bytes\n",
              scene.objectCount, objectStride);

  const std::string vertex = vertexSource(es);
  const std::string fragment = fragmentSource(es);
  gpu::PipelineDesc pipelineDesc;
  pipelineDesc.vertex.source = demo::view(vertex);
  pipelineDesc.fragment.source = demo::view(fragment);
  pipelineDesc.vertexBufferCount = 1;
  pipelineDesc.vertexBuffers[0].stride = 24;
  pipelineDesc.vertexBuffers[0].attributeCount = 2;
  pipelineDesc.vertexBuffers[0].attributes[0].format =
      gpu::VertexFormat::Float32x3;
  pipelineDesc.vertexBuffers[0].attributes[1].format =
      gpu::VertexFormat::Float32x3;
  pipelineDesc.vertexBuffers[0].attributes[1].offset = 12;
  pipelineDesc.vertexBuffers[0].attributes[1].shaderLocation = 1;
  pipelineDesc.colorTargetCount = 1;
  pipelineDesc.colorTargets[0].format = gpu::Format::RGBA8;
  pipelineDesc.depthStencil.format = gpu::Format::Depth32Float;
  pipelineDesc.depthStencil.depthTestEnabled = true;
  pipelineDesc.depthStencil.depthWriteEnabled = true;
  pipelineDesc.depthStencil.depthCompare = gpu::CompareOp::LessEqual;
  pipelineDesc.raster.cullMode = gpu::CullMode::Back;
  pipelineDesc.debugName = "forward light list";
  const gpu::PipelineHandle pipeline = device->createPipeline(pipelineDesc);

  gpu::BufferDesc frameBufferDesc;
  frameBufferDesc.size = sizeof(FrameUniforms);
  frameBufferDesc.usage = gpu::BufferUsageUniform;
  frameBufferDesc.debugName = "forward frame uniforms";
  const gpu::BufferHandle frameBuffer = device->createBuffer(frameBufferDesc);

  gpu::BufferDesc objectBufferDesc;
  objectBufferDesc.size = objectStride * scene.objectCount;
  objectBufferDesc.usage = gpu::BufferUsageUniform;
  objectBufferDesc.debugName = "forward per-object light lists";
  const gpu::BufferHandle objectBuffer = device->createBuffer(objectBufferDesc);

  gpu::TextureHandle depthTarget;
  gpu::TextureHandle captureTarget;

  if (demo::reportErrors(*device, "setup"))
  {
    demo::destroyHost(host);
    return 1;
  }

  std::vector<Light> lights(SceneLightCount);
  lights[0].directional = true;
  lights[0].direction = demo::normalize({-0.4f, -1.0f, -0.3f});
  lights[0].colour = {0.55f, 0.58f, 0.68f};
  lights[0].intensity = 0.9f;
  lights[0].castsShadow = true;
  for (std::uint32_t index = 1; index < SceneLightCount; ++index)
  {
    const float hue = static_cast<float>(index) / (SceneLightCount - 1);
    lights[index].colour = {0.5f + 0.5f * std::cos(hue * 6.28f),
                            0.5f + 0.5f * std::cos(hue * 6.28f + 2.09f),
                            0.5f + 0.5f * std::cos(hue * 6.28f + 4.19f)};
    lights[index].range = 11.0f;
    lights[index].intensity = 16.0f;
  }

  demo::Camera camera;
  camera.eye = {0.0f, 9.0f, 15.0f};
  std::uint32_t width = 1024;
  std::uint32_t height = 640;
  float time = 0.0f;
  std::uint32_t frameIndex = 0;
  std::vector<std::uint8_t> objectData(objectStride * scene.objectCount);

  while (demo::pumpEvents(*host))
  {
    time += 0.016f;
    for (std::uint32_t index = 1; index < SceneLightCount; ++index)
    {
      const float phase =
          time * 0.35f + static_cast<float>(index) * 0.7f;
      const float radius = 4.0f + static_cast<float>(index % 3) * 3.5f;
      lights[index].position = {std::cos(phase) * radius, 2.2f,
                                std::sin(phase) * radius};
    }

    demo::drawableSize(*host, width, height);
    if (options.capture)
    {
      width = captureWidth;
      height = captureHeight;
    }

    if (!depthTarget.valid() || options.capture)
    {
      if (!depthTarget.valid())
      {
        gpu::TextureDesc depthDesc;
        depthDesc.format = gpu::Format::Depth32Float;
        depthDesc.width = width;
        depthDesc.height = height;
        depthDesc.usage = gpu::TextureUsageRenderTarget;
        depthDesc.debugName = "forward depth";
        depthTarget = device->createTexture(depthDesc);
        if (options.capture)
        {
          gpu::TextureDesc colourDesc = depthDesc;
          colourDesc.format = gpu::Format::RGBA8;
          colourDesc.usage = gpu::TextureUsageRenderTarget |
                             gpu::TextureUsageCopySource;
          colourDesc.debugName = "capture colour";
          captureTarget = device->createTexture(colourDesc);
        }
      }
    }

    FrameUniforms frame;
    std::memcpy(frame.viewProjection,
                demo::multiply(camera.projection(static_cast<float>(width) /
                                                 static_cast<float>(height)),
                               camera.view())
                    .m,
                sizeof(frame.viewProjection));
    device->updateBuffer(frameBuffer, 0, {&frame, sizeof(frame)});

    std::uint32_t totalSelected = 0;
    for (std::uint32_t index = 0; index < scene.objectCount; ++index)
    {
      ObjectUniforms uniforms;
      std::memset(&uniforms, 0, sizeof(uniforms));
      const std::uint32_t used =
          selectLights(scene.objects[index], lights, uniforms.lights);
      uniforms.lightCount[0] = static_cast<float>(used);
      totalSelected += used;
      std::memcpy(objectData.data() + index * objectStride, &uniforms,
                  sizeof(uniforms));
    }
    device->updateBuffer(objectBuffer, 0,
                         {objectData.data(), objectData.size()});

    gpu::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0].surface = !options.capture;
    if (options.capture)
      pass.colors[0].target.texture = captureTarget;
    pass.colors[0].loadOp = gpu::LoadOp::Clear;
    pass.colors[0].clearColor[0] = 0.03f;
    pass.colors[0].clearColor[1] = 0.035f;
    pass.colors[0].clearColor[2] = 0.05f;
    pass.colors[0].clearColor[3] = 1.0f;
    pass.hasDepthStencil = true;
    if (options.capture)
      pass.depthStencil.target.texture = depthTarget;
    pass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
    pass.depthStencil.clearDepth = 1.0f;
    if (device->beginRenderPass(pass))
    {
      gpu::Viewport viewport;
      viewport.width = static_cast<float>(width);
      viewport.height = static_cast<float>(height);
      device->setViewport(viewport);
      device->setPipeline(pipeline);
      device->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
      device->bindVertexBuffer(0, scene.vertices, 0);
      for (std::uint32_t index = 0; index < scene.objectCount; ++index)
      {
        device->bindUniformBuffer(1, objectBuffer, index * objectStride,
                                  sizeof(ObjectUniforms));
        device->draw(scene.objects[index].vertexCount, 1,
                     scene.objects[index].firstVertex);
      }
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
        std::printf("luzes na cena: %u | seleccionadas no total: %u "
                    "(max %u por objeto)\n",
                    SceneLightCount, totalSelected, LightsPerObject);
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

  if (captureTarget.valid())
    device->destroy(captureTarget);
  if (depthTarget.valid())
    device->destroy(depthTarget);
  device->destroy(objectBuffer);
  device->destroy(frameBuffer);
  device->destroy(pipeline);
  demo::destroyScene(*device, scene);
  demo::destroyHost(host);
  return 0;
}
