#include "ShadowDemoCommon.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
  const std::uint32_t MapResolution = 1024;
  const std::uint32_t CubeResolution = 512;
  const std::uint32_t ObjectCount = 2;

  struct FrameUniforms
  {
    float viewProjection[16];
    float directionalViewProjection[16];
    float spotViewProjection[16];
    float directionalDirection[4];
    float directionalColour[4];
    float spotPosition[4];
    float spotDirection[4];
    float spotColour[4];
    float spotParameters[4];
    float pointPosition[4];
    float pointColour[4];
    float shadowParameters[4];
  };

  struct MapUniforms
  {
    float viewProjection[16];
  };

  struct CubeUniforms
  {
    float viewProjection[16];
    float position[4];
    float parameters[4];
  };

  void writeMatrix(float *destination, const demo::Mat4 &matrix)
  {
    std::memcpy(destination, matrix.m, sizeof(matrix.m));
  }

  demo::Mat4 cubeView(const demo::Vec3 &position, std::uint32_t face)
  {
    static const demo::Vec3 forward[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
        {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const demo::Vec3 up[6] = {
        {0, -1, 0}, {0, -1, 0}, {0, 0, 1},
        {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    return demo::lookAt(position, demo::add(position, forward[face]), up[face]);
  }

  std::string mapVertex(bool es)
  {
    return demo::preamble(es) + R"(
layout(location=0) in vec3 inPosition;
layout(std140) uniform MapBlock { mat4 lightViewProjection; };
void main() { gl_Position = lightViewProjection * vec4(inPosition, 1.0); }
)";
  }

  std::string mapFragment(bool es)
  {
    return demo::preamble(es) + "void main() {}\n";
  }

  std::string cubeVertex(bool es)
  {
    return demo::preamble(es) + R"(
layout(location=0) in vec3 inPosition;
layout(std140) uniform CubeBlock {
  mat4 lightViewProjection;
  vec4 lightPosition;
  vec4 lightParameters;
};
out vec3 lightRay;
void main() {
  lightRay = inPosition - lightPosition.xyz;
  gl_Position = lightViewProjection * vec4(inPosition, 1.0);
}
)";
  }

  std::string cubeFragment(bool es)
  {
    return demo::preamble(es) + R"(
layout(std140) uniform CubeBlock {
  mat4 lightViewProjection;
  vec4 lightPosition;
  vec4 lightParameters;
};
in vec3 lightRay;
void main() {
  gl_FragDepth = clamp(length(lightRay) * lightParameters.x, 0.0, 1.0);
}
)";
  }

  const char *frameBlock()
  {
    return R"(
layout(std140) uniform FrameBlock {
  mat4 viewProjection;
  mat4 directionalViewProjection;
  mat4 spotViewProjection;
  vec4 directionalDirection;
  vec4 directionalColour;
  vec4 spotPosition;
  vec4 spotDirection;
  vec4 spotColour;
  vec4 spotParameters;
  vec4 pointPosition;
  vec4 pointColour;
  vec4 shadowParameters;
};
)";
  }

  std::string sceneVertex(bool es)
  {
    return demo::preamble(es) +
           "layout(location=0) in vec3 inPosition;\n"
           "layout(location=1) in vec3 inNormal;\n" + frameBlock() + R"(
out vec3 worldPosition;
out vec3 worldNormal;
void main() {
  worldPosition = inPosition;
  worldNormal = normalize(inNormal);
  gl_Position = viewProjection * vec4(inPosition, 1.0);
}
)";
  }

  std::string sceneFragment(bool es)
  {
    return demo::preamble(es) + R"(
uniform sampler2DShadow directionalShadow;
uniform sampler2DShadow spotShadow;
uniform samplerCubeShadow pointShadow;
)" + frameBlock() + R"(
in vec3 worldPosition;
in vec3 worldNormal;
out vec4 fragmentColour;

float sampleMap(sampler2DShadow shadowMap, mat4 matrix, vec3 position) {
  vec4 coord = matrix * vec4(position, 1.0);
  vec3 projected = coord.xyz / coord.w * 0.5 + 0.5;
  if (projected.x < 0.0 || projected.x > 1.0 ||
      projected.y < 0.0 || projected.y > 1.0 ||
      projected.z < 0.0 || projected.z > 1.0) return 1.0;
  float result = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
      result += texture(shadowMap, vec3(
          projected.xy + vec2(float(x), float(y)) * shadowParameters.x,
          projected.z));
  return result / 9.0;
}

void main() {
  vec3 normal = normalize(worldNormal);
  vec3 lighting = vec3(0.045, 0.05, 0.065);

  vec3 toDirectional = normalize(-directionalDirection.xyz);
  float directionalSlope = 1.0 - max(dot(normal, toDirectional), 0.0);
  vec3 directionalOffset = worldPosition + normal * directionalSlope *
                           shadowParameters.y;
  float directionalVisibility = sampleMap(
      directionalShadow, directionalViewProjection, directionalOffset);
  lighting += directionalColour.rgb * directionalColour.a *
      max(dot(normal, toDirectional), 0.0) * directionalVisibility;

  vec3 spotDelta = spotPosition.xyz - worldPosition;
  float spotDistance = length(spotDelta);
  vec3 toSpot = spotDelta / max(spotDistance, 0.0001);
  float spotSlope = 1.0 - max(dot(normal, toSpot), 0.0);
  vec3 spotOffset = worldPosition + normal * spotSlope * shadowParameters.y;
  float spotVisibility = sampleMap(spotShadow, spotViewProjection, spotOffset);
  float cone = dot(-toSpot, normalize(spotDirection.xyz));
  float coneFade = clamp((cone - spotParameters.y) * spotParameters.x, 0.0, 1.0);
  float spotRangeFade = max((spotParameters.z - spotDistance) /
                            max(spotParameters.z, 0.0001), 0.0);
  float spotAttenuation = coneFade * coneFade * spotRangeFade /
      (0.5 + 0.09 * spotDistance + 0.032 * spotDistance * spotDistance);
  lighting += spotColour.rgb * spotColour.a * max(dot(normal, toSpot), 0.0) *
              spotAttenuation * spotVisibility;

  vec3 pointDelta = pointPosition.xyz - worldPosition;
  float pointDistance = length(pointDelta);
  vec3 toPoint = pointDelta / max(pointDistance, 0.0001);
  float pointSlope = 1.0 - max(dot(normal, toPoint), 0.0);
  vec3 pointOffset = worldPosition + normal * pointSlope * shadowParameters.y;
  vec3 pointRay = pointOffset - pointPosition.xyz;
  float pointReference = length(pointRay) * shadowParameters.z -
                         shadowParameters.w;
  float pointVisibility = texture(
      pointShadow, vec4(pointRay, clamp(pointReference, 0.0, 1.0)));
  float pointRangeFade = max((pointPosition.w - pointDistance) /
                             max(pointPosition.w, 0.0001), 0.0);
  float pointAttenuation = pointRangeFade /
      (0.5 + 0.09 * pointDistance + 0.032 * pointDistance * pointDistance);
  lighting += pointColour.rgb * pointColour.a * max(dot(normal, toPoint), 0.0) *
              pointAttenuation * pointVisibility;

  vec3 ground = vec3(0.34, 0.39, 0.32);
  vec3 cube = vec3(0.76, 0.27, 0.14);
  vec3 baseColour = mix(cube, ground, step(0.95, normal.y));
  vec3 colour = baseColour * lighting;
  colour = colour / (colour + vec3(1.0));
  fragmentColour = vec4(pow(colour, vec3(1.0 / 2.2)), 1.0);
}
)";
  }

  gpu::PipelineHandle depthPipeline(gpu::Device &device,
                                     const std::string &vertex,
                                     const std::string &fragment,
                                     gpu::CullMode cull, const char *name)
  {
    gpu::PipelineDesc desc;
    desc.vertex.source = demo::view(vertex);
    desc.fragment.source = demo::view(fragment);
    desc.vertexBufferCount = 1;
    desc.vertexBuffers[0].stride = 24;
    desc.vertexBuffers[0].attributeCount = 1;
    desc.vertexBuffers[0].attributes[0].format = gpu::VertexFormat::Float32x3;
    desc.depthStencil.format = gpu::Format::Depth32Float;
    desc.depthStencil.depthTestEnabled = true;
    desc.depthStencil.depthWriteEnabled = true;
    desc.depthStencil.depthCompare = gpu::CompareOp::LessEqual;
    desc.raster.cullMode = cull;
    if (cull != gpu::CullMode::None)
    {
      desc.raster.depthBiasConstant = 4.0f;
      desc.raster.depthBiasSlope = 2.0f;
    }
    desc.debugName = name;
    return device.createPipeline(desc);
  }

  gpu::PipelineHandle forwardPipeline(gpu::Device &device,
                                       const std::string &vertex,
                                       const std::string &fragment)
  {
    gpu::PipelineDesc desc;
    desc.vertex.source = demo::view(vertex);
    desc.fragment.source = demo::view(fragment);
    desc.vertexBufferCount = 1;
    desc.vertexBuffers[0].stride = 24;
    desc.vertexBuffers[0].attributeCount = 2;
    desc.vertexBuffers[0].attributes[0].format = gpu::VertexFormat::Float32x3;
    desc.vertexBuffers[0].attributes[1].format = gpu::VertexFormat::Float32x3;
    desc.vertexBuffers[0].attributes[1].offset = 12;
    desc.vertexBuffers[0].attributes[1].shaderLocation = 1;
    desc.colorTargetCount = 1;
    desc.colorTargets[0].format = gpu::Format::RGBA8;
    desc.depthStencil.format = gpu::Format::Depth32Float;
    desc.depthStencil.depthTestEnabled = true;
    desc.depthStencil.depthWriteEnabled = true;
    desc.depthStencil.depthCompare = gpu::CompareOp::LessEqual;
    desc.raster.cullMode = gpu::CullMode::Back;
    desc.debugName = "three shadowed lights forward pass";
    return device.createPipeline(desc);
  }

  gpu::TextureHandle shadowTexture(gpu::Device &device,
                                    gpu::TextureDimension dimension,
                                    std::uint32_t resolution, const char *name)
  {
    gpu::TextureDesc desc;
    desc.dimension = dimension;
    desc.format = gpu::Format::Depth32Float;
    desc.width = resolution;
    desc.height = resolution;
    desc.usage = gpu::TextureUsageRenderTarget |
                 gpu::TextureUsageSampled | gpu::TextureUsageCopySource;
    desc.debugName = name;
    return device.createTexture(desc);
  }

  gpu::BufferHandle uniformBuffer(gpu::Device &device, std::uint64_t size,
                                   const char *name)
  {
    gpu::BufferDesc desc;
    desc.size = size;
    desc.usage = gpu::BufferUsageUniform;
    desc.debugName = name;
    return device.createBuffer(desc);
  }

  void renderMap(gpu::Device &device, gpu::TextureHandle map,
                 gpu::PipelineHandle pipeline, gpu::BufferHandle uniforms,
                 gpu::BufferHandle vertices, std::uint32_t vertexCount)
  {
    gpu::RenderPassDesc pass;
    pass.hasDepthStencil = true;
    pass.depthStencil.target.texture = map;
    pass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
    pass.depthStencil.clearDepth = 1.0f;
    if (!device.beginRenderPass(pass))
      return;
    gpu::Viewport viewport;
    viewport.width = static_cast<float>(MapResolution);
    viewport.height = static_cast<float>(MapResolution);
    device.setViewport(viewport);
    device.setPipeline(pipeline);
    device.bindUniformBuffer(0, uniforms, 0, sizeof(MapUniforms));
    device.bindVertexBuffer(0, vertices, 0);
    device.draw(vertexCount);
    device.endRenderPass();
  }
}

int main(int argc, char **argv)
{
  demo::Options options = demo::parseOptions(argc, argv, "mini_engine.ppm");
  const std::uint32_t captureWidth = 512;
  const std::uint32_t captureHeight = 384;
  demo::Host *host = demo::createHost(
      "Mini engine: directional, spot and point shadows", 1024, 640,
      !options.forceGL, options.capture);
  if (!host)
    return 1;
  gpu::Device *device = demo::device(*host);
  const bool es = demo::isES(*host);
  std::printf("backend: %s\n", es ? "OpenGL ES 3" : "OpenGL desktop");

  demo::Scene scene = demo::createScene(*device);
  const std::uint32_t vertexCount =
      scene.objects[ObjectCount - 1].firstVertex +
      scene.objects[ObjectCount - 1].vertexCount;
  const gpu::TextureHandle directionalMap = shadowTexture(
      *device, gpu::TextureDimension::Texture2D, MapResolution,
      "directional shadow map");
  const gpu::TextureHandle spotMap = shadowTexture(
      *device, gpu::TextureDimension::Texture2D, MapResolution,
      "spot shadow map");
  const gpu::TextureHandle pointCube = shadowTexture(
      *device, gpu::TextureDimension::TextureCube, CubeResolution,
      "point shadow cube");

  gpu::SamplerDesc samplerDesc;
  samplerDesc.minFilter = gpu::Filter::Linear;
  samplerDesc.magFilter = gpu::Filter::Linear;
  samplerDesc.addressU = gpu::AddressMode::ClampToEdge;
  samplerDesc.addressV = gpu::AddressMode::ClampToEdge;
  samplerDesc.addressW = gpu::AddressMode::ClampToEdge;
  samplerDesc.compareEnabled = true;
  samplerDesc.compare = gpu::CompareOp::LessEqual;
  samplerDesc.debugName = "three shadows comparison sampler";
  const gpu::SamplerHandle sampler = device->createSampler(samplerDesc);

  const std::string mapVS = mapVertex(es);
  const std::string mapFS = mapFragment(es);
  const std::string cubeVS = cubeVertex(es);
  const std::string cubeFS = cubeFragment(es);
  const std::string sceneVS = sceneVertex(es);
  const std::string sceneFS = sceneFragment(es);
  const gpu::PipelineHandle mapPipe = depthPipeline(
      *device, mapVS, mapFS, gpu::CullMode::None, "directional and spot depth");
  const gpu::PipelineHandle cubePipe = depthPipeline(
      *device, cubeVS, cubeFS, gpu::CullMode::None, "point cube depth");
  const gpu::PipelineHandle scenePipe =
      forwardPipeline(*device, sceneVS, sceneFS);
  const gpu::BufferHandle frameBuffer =
      uniformBuffer(*device, sizeof(FrameUniforms), "three lights frame");
  const gpu::BufferHandle directionalBuffer =
      uniformBuffer(*device, sizeof(MapUniforms), "directional matrix");
  const gpu::BufferHandle spotBuffer =
      uniformBuffer(*device, sizeof(MapUniforms), "spot matrix");
  const gpu::BufferHandle pointBuffer =
      uniformBuffer(*device, sizeof(CubeUniforms), "point cube matrices");

  gpu::TextureHandle captureColour;
  gpu::TextureHandle captureDepth;
  if (options.capture)
  {
    gpu::TextureDesc desc;
    desc.width = captureWidth;
    desc.height = captureHeight;
    desc.format = gpu::Format::RGBA8;
    desc.usage = gpu::TextureUsageRenderTarget | gpu::TextureUsageCopySource;
    captureColour = device->createTexture(desc);
    desc.format = gpu::Format::Depth32Float;
    desc.usage = gpu::TextureUsageRenderTarget;
    captureDepth = device->createTexture(desc);
  }

  demo::Camera camera;
  camera.eye = {3.0f, 4.5f, 5.5f};
  camera.target = {-3.0f, 1.0f, -1.0f};
  const demo::Vec3 target = scene.objects[1].centre;
  const demo::Vec3 directionalDirection = demo::normalize({-0.55f, -1, -0.35f});
  const demo::Vec3 directionalEye =
      demo::subtract(target, demo::scale(directionalDirection, 16.0f));
  const demo::Mat4 directionalMatrix = demo::multiply(
      demo::orthographic(-9, 9, -9, 9, 0.1f, 36),
      demo::lookAt(directionalEye, target, {0, 1, 0}));
  const demo::Vec3 spotPosition{4, 9, 3};
  const float spotInner = 0.46f;
  const float spotOuter = 0.72f;
  const float spotRange = 32.0f;
  const demo::Mat4 spotMatrix = demo::multiply(
      demo::perspective(spotOuter * 2, 1, 0.5f, spotRange),
      demo::lookAt(spotPosition, target, {0, 1, 0}));
  const demo::Vec3 pointPosition{-6, 4, 3};
  const float pointRange = 18.0f;
  const demo::Mat4 pointProjection =
      demo::perspective(1.5707963f, 1, 0.2f, pointRange);

  MapUniforms mapData;
  writeMatrix(mapData.viewProjection, directionalMatrix);
  device->updateBuffer(directionalBuffer, 0, {&mapData, sizeof(mapData)});
  writeMatrix(mapData.viewProjection, spotMatrix);
  device->updateBuffer(spotBuffer, 0, {&mapData, sizeof(mapData)});

  if (demo::reportErrors(*device, "setup"))
  {
    demo::destroyHost(host);
    return 1;
  }

  std::uint32_t frameIndex = 0;
  bool running = true;
  while (running && demo::pumpEvents(*host))
  {
    std::uint32_t width = 1024;
    std::uint32_t height = 640;
    if (options.capture)
    {
      width = captureWidth;
      height = captureHeight;
    }
    else
      demo::drawableSize(*host, width, height);

    renderMap(*device, directionalMap, mapPipe, directionalBuffer,
              scene.vertices, vertexCount);
    renderMap(*device, spotMap, mapPipe, spotBuffer, scene.vertices,
              vertexCount);
    for (std::uint32_t face = 0; face < 6; ++face)
    {
      CubeUniforms cubeData;
      std::memset(&cubeData, 0, sizeof(cubeData));
      writeMatrix(cubeData.viewProjection,
                  demo::multiply(pointProjection, cubeView(pointPosition, face)));
      cubeData.position[0] = pointPosition.x;
      cubeData.position[1] = pointPosition.y;
      cubeData.position[2] = pointPosition.z;
      cubeData.parameters[0] = 1.0f / pointRange;
      device->updateBuffer(pointBuffer, 0, {&cubeData, sizeof(cubeData)});
      gpu::RenderPassDesc pass;
      pass.hasDepthStencil = true;
      pass.depthStencil.target.texture = pointCube;
      pass.depthStencil.target.layer = face;
      pass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
      pass.depthStencil.clearDepth = 1;
      if (device->beginRenderPass(pass))
      {
        gpu::Viewport viewport;
        viewport.width = CubeResolution;
        viewport.height = CubeResolution;
        device->setViewport(viewport);
        device->setPipeline(cubePipe);
        device->bindUniformBuffer(0, pointBuffer, 0, sizeof(CubeUniforms));
        device->bindVertexBuffer(0, scene.vertices, 0);
        device->draw(vertexCount);
        device->endRenderPass();
      }
    }

    FrameUniforms frame;
    std::memset(&frame, 0, sizeof(frame));
    writeMatrix(frame.viewProjection, demo::multiply(
        camera.projection(static_cast<float>(width) / height), camera.view()));
    writeMatrix(frame.directionalViewProjection, directionalMatrix);
    writeMatrix(frame.spotViewProjection, spotMatrix);
    frame.directionalDirection[0] = directionalDirection.x;
    frame.directionalDirection[1] = directionalDirection.y;
    frame.directionalDirection[2] = directionalDirection.z;
    frame.directionalColour[0] = 0.95f;
    frame.directionalColour[1] = 0.96f;
    frame.directionalColour[2] = 0.72f;
    frame.directionalColour[3] = 0.55f;
    frame.spotPosition[0] = spotPosition.x;
    frame.spotPosition[1] = spotPosition.y;
    frame.spotPosition[2] = spotPosition.z;
    const demo::Vec3 spotDirection = demo::normalize(demo::subtract(target, spotPosition));
    frame.spotDirection[0] = spotDirection.x;
    frame.spotDirection[1] = spotDirection.y;
    frame.spotDirection[2] = spotDirection.z;
    frame.spotColour[0] = 1;
    frame.spotColour[1] = 0.28f;
    frame.spotColour[2] = 0.12f;
    frame.spotColour[3] = 13;
    frame.spotParameters[0] = 1.0f /
        std::max(std::cos(spotInner) - std::cos(spotOuter), 0.0001f);
    frame.spotParameters[1] = std::cos(spotOuter);
    frame.spotParameters[2] = spotRange;
    frame.pointPosition[0] = pointPosition.x;
    frame.pointPosition[1] = pointPosition.y;
    frame.pointPosition[2] = pointPosition.z;
    frame.pointPosition[3] = pointRange;
    frame.pointColour[0] = 0.18f;
    frame.pointColour[1] = 0.42f;
    frame.pointColour[2] = 1;
    frame.pointColour[3] = 7;
    frame.shadowParameters[0] = 1.0f / MapResolution;
    frame.shadowParameters[1] = 0.045f;
    frame.shadowParameters[2] = 1.0f / pointRange;
    frame.shadowParameters[3] = 0.004f;
    device->updateBuffer(frameBuffer, 0, {&frame, sizeof(frame)});

    gpu::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0].surface = !options.capture;
    if (options.capture)
      pass.colors[0].target.texture = captureColour;
    pass.colors[0].loadOp = gpu::LoadOp::Clear;
    pass.colors[0].clearColor[0] = 0.02f;
    pass.colors[0].clearColor[1] = 0.025f;
    pass.colors[0].clearColor[2] = 0.04f;
    pass.colors[0].clearColor[3] = 1;
    pass.hasDepthStencil = options.capture;
    if (options.capture)
    {
      pass.depthStencil.target.texture = captureDepth;
      pass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
      pass.depthStencil.clearDepth = 1;
    }
    if (device->beginRenderPass(pass))
    {
      gpu::Viewport viewport;
      viewport.width = width;
      viewport.height = height;
      device->setViewport(viewport);
      device->setPipeline(scenePipe);
      device->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
      device->bindTexture(0, directionalMap, sampler);
      device->bindTexture(1, spotMap, sampler);
      device->bindTexture(2, pointCube, sampler);
      device->bindVertexBuffer(0, scene.vertices, 0);
      for (std::uint32_t i = 0; i < ObjectCount; ++i)
        device->draw(scene.objects[i].vertexCount, 1, scene.objects[i].firstVertex);
      device->endRenderPass();
    }
    if (!options.capture)
      device->present();
    if (demo::reportErrors(*device, "frame"))
      running = false;
    ++frameIndex;
    if (options.frames != 0 && frameIndex >= options.frames)
    {
      if (options.capture)
      {
        std::vector<std::uint8_t> pixels(captureWidth * captureHeight * 4);
        gpu::TextureRegion region;
        region.width = captureWidth;
        region.height = captureHeight;
        if (device->readTexture(captureColour, region,
                                {pixels.data(), pixels.size()}))
        {
          demo::writePPM(options.capturePath.c_str(), pixels.data(),
                         captureWidth, captureHeight);
          std::printf("captura: %s\n", options.capturePath.c_str());
          demo::describeCapture(pixels.data(), captureWidth, captureHeight);
        }
        const bool canReadDepth = device->capabilities().depthReadback;
        if (!canReadDepth)
          std::printf("mapas: este backend nao le texturas de profundidade "
                      "(ES/WebGL2)\n");
        const auto probe = [&](const char *name, gpu::TextureHandle map,
                               std::uint32_t side, std::uint32_t layer) {
          if (!canReadDepth)
            return;
          std::vector<float> depth(side * side);
          gpu::TextureRegion region;
          region.width = side;
          region.height = side;
          region.z = layer;
          if (!device->readTexture(map, region,
                                   {depth.data(), depth.size() * sizeof(float)}))
          {
            std::printf("  %-12s leitura falhou\n", name);
            return;
          }
          std::uint32_t written = 0;
          for (float value : depth)
            if (value < 0.999f)
              ++written;
          std::printf("  %-12s escrito %5.1f%%\n", name,
                      100.0 * written / depth.size());
        };
        probe("direcional", directionalMap, MapResolution, 0);
        probe("spot", spotMap, MapResolution, 0);
        for (std::uint32_t face = 0; face < 6; ++face)
        {
          char label[16];
          std::snprintf(label, sizeof(label), "point.%u", face);
          probe(label, pointCube, CubeResolution, face);
        }
        demo::reportErrors(*device, "capture");
      }
      running = false;
    }
  }

  if (captureDepth.valid()) device->destroy(captureDepth);
  if (captureColour.valid()) device->destroy(captureColour);
  device->destroy(pointBuffer);
  device->destroy(spotBuffer);
  device->destroy(directionalBuffer);
  device->destroy(frameBuffer);
  device->destroy(scenePipe);
  device->destroy(cubePipe);
  device->destroy(mapPipe);
  device->destroy(sampler);
  device->destroy(pointCube);
  device->destroy(spotMap);
  device->destroy(directionalMap);
  demo::destroyScene(*device, scene);
  demo::destroyHost(host);
  return 0;
}
