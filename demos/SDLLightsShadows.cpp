#include "ShadowDemoCommon.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

  const std::uint32_t AtlasSize = 2048;
  const std::uint32_t CascadeCount = 4;
  const std::uint32_t MaxCascadeResolution = 512;
  const std::uint32_t MaxSpotResolution = 512;
  const std::uint32_t MaxPointResolution = 256;
  const std::uint32_t MaxLightsPerObject = 6;
  const std::uint32_t MaxTiles = 64;
  const std::uint32_t SceneSpotCount = 3;
  const std::uint32_t ScenePointCount = 6;

  struct Rect
  {
    std::uint32_t x = 0, y = 0, w = 0, h = 0;
  };

  Rect subTile(const demo::PackedRect &packed, std::uint32_t index,
               std::uint32_t count)
  {
    const std::uint32_t tileWidth =
        static_cast<std::uint32_t>(packed.w) / count;
    return {static_cast<std::uint32_t>(packed.x) + index * tileWidth,
            static_cast<std::uint32_t>(packed.y), tileWidth,
            static_cast<std::uint32_t>(packed.h)};
  }

  void writeRect(float *destination, const Rect &rect)
  {
    const float inverse = 1.0f / static_cast<float>(AtlasSize);
    destination[0] = static_cast<float>(rect.w) * inverse;
    destination[1] = static_cast<float>(rect.h) * inverse;
    destination[2] = static_cast<float>(rect.x) * inverse;
    destination[3] = static_cast<float>(rect.y) * inverse;
  }

  struct ShadowTileUniforms
  {
    float viewProjection[16];
    float lightPosition[4];
  };

  struct GpuLight
  {
    float position[4];
    float direction[4];
    float colour[4];
    float parameters[4];
    float shadowRect[4];
    float shadowMatrix[16];
  };

  struct ObjectUniforms
  {
    GpuLight lights[6];
    float lightCount[4];
  };

  struct FrameUniforms
  {
    float viewProjection[16];
    float cascadeMatrix[CascadeCount][16];
    float cascadeRects[CascadeCount][4];
    float cascadeSplits[4];
    float atlasParameters[4];
  };

  void writeMatrix(float *destination, const demo::Mat4 &source)
  {
    std::memcpy(destination, source.m, sizeof(source.m));
  }

  const char *lightStruct()
  {
    return "struct GpuLight {\n"
           "  vec4 position;\n"
           "  vec4 direction;\n"
           "  vec4 colour;\n"
           "  vec4 parameters;\n"
           "  vec4 shadowRect;\n"
           "  mat4 shadowMatrix;\n"
           "};\n";
  }

  const char *frameBlock()
  {
    return "layout(std140) uniform FrameBlock {\n"
           "  mat4 viewProjection;\n"
           "  mat4 cascadeMatrix[4];\n"
           "  vec4 cascadeRects[4];\n"
           "  vec4 cascadeSplits;\n"
           "  vec4 atlasParameters;\n"
           "};\n";
  }

  std::string shadowVertex(bool es)
  {
    return demo::preamble(es) +
           "layout(location = 0) in vec3 inPosition;\n"
           "layout(std140) uniform TileBlock {\n"
           "  mat4 lightViewProjection;\n"
           "  vec4 lightPosition;\n"
           "};\n"
           "out vec3 lightRay;\n"
           "void main() {\n"
           "  lightRay = inPosition - lightPosition.xyz;\n"
           "  gl_Position = lightViewProjection * vec4(inPosition, 1.0);\n"
           "}\n";
  }

  std::string shadowFragmentProjected(bool es)
  {
    return demo::preamble(es) + "in vec3 lightRay;\nvoid main() {}\n";
  }

  std::string shadowFragmentLinear(bool es)
  {
    return demo::preamble(es) +
           "layout(std140) uniform TileBlock {\n"
           "  mat4 lightViewProjection;\n"
           "  vec4 lightPosition;\n"
           "};\n"
           "in vec3 lightRay;\n"
           "void main() {\n"
           "  gl_FragDepth = clamp(length(lightRay) * lightPosition.w, 0.0, 1.0);\n"
           "}\n";
  }

  std::string sceneVertex(bool es)
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

  std::string sceneFragment(bool es)
  {
    return demo::preamble(es) + lightStruct() + frameBlock() +
           "layout(std140) uniform ObjectBlock {\n"
           "  GpuLight lights[6];\n"
           "  vec4 lightCount;\n"
           "};\n"
           "uniform sampler2DShadow shadowAtlas;\n"
           "in vec3 worldPosition;\n"
           "in vec3 worldNormal;\n"
           "in float viewDepth;\n"
           "out vec4 fragmentColour;\n"

           "vec2 toAtlas(vec2 tileUv, vec4 rect) {\n"
           "  vec2 halfTexel = vec2(1.5) * atlasParameters.x / rect.xy;\n"
           "  vec2 clamped = clamp(tileUv, halfTexel, vec2(1.0) - halfTexel);\n"
           "  return clamped * rect.xy + rect.zw;\n"
           "}\n"

           "float pcf13(vec2 uv, float reference, vec4 rect) {\n"
           "  float texel = atlasParameters.x;\n"
           "  float sum = texture(shadowAtlas, vec3(uv, reference));\n"
           "  sum += texture(shadowAtlas, vec3(uv + vec2( texel * 2.0, 0.0), reference));\n"
           "  sum += texture(shadowAtlas, vec3(uv + vec2(-texel * 2.0, 0.0), reference));\n"
           "  sum += texture(shadowAtlas, vec3(uv + vec2(0.0,  texel * 2.0), reference));\n"
           "  sum += texture(shadowAtlas, vec3(uv + vec2(0.0, -texel * 2.0), reference));\n"
           "  if (sum <= 0.000001) return 0.0;\n"
           "  if (sum >= 4.999999) return 1.0;\n"
           "  sum += texture(shadowAtlas, vec3(uv + vec2( texel, 0.0), reference));\n"
           "  sum += texture(shadowAtlas, vec3(uv + vec2(-texel, 0.0), reference));\n"
           "  sum += texture(shadowAtlas, vec3(uv + vec2(0.0,  texel), reference));\n"
           "  sum += texture(shadowAtlas, vec3(uv + vec2(0.0, -texel), reference));\n"
           "  sum += texture(shadowAtlas, vec3(uv + vec2( texel,  texel), reference));\n"
           "  sum += texture(shadowAtlas, vec3(uv + vec2(-texel,  texel), reference));\n"
           "  sum += texture(shadowAtlas, vec3(uv + vec2( texel, -texel), reference));\n"
           "  sum += texture(shadowAtlas, vec3(uv + vec2(-texel, -texel), reference));\n"
           "  return sum * (1.0 / 13.0);\n"
           "}\n"

           "float projectedShadow(mat4 matrix, vec4 rect, vec3 position) {\n"
           "  vec4 coord = matrix * vec4(position, 1.0);\n"
           "  if (coord.w <= 0.0) return 1.0;\n"
           "  vec3 projected = coord.xyz / coord.w;\n"
           "  projected = projected * 0.5 + 0.5;\n"
           "  if (projected.x < 0.0 || projected.x > 1.0 ||\n"
           "      projected.y < 0.0 || projected.y > 1.0 ||\n"
           "      projected.z > 1.0) return 1.0;\n"
           "  return pcf13(toAtlas(projected.xy, rect), projected.z, rect);\n"
           "}\n"

           "float cascadeShadow(vec3 position, float depth) {\n"
           "  int index = 3;\n"
           "  if (depth < cascadeSplits.x) index = 0;\n"
           "  else if (depth < cascadeSplits.y) index = 1;\n"
           "  else if (depth < cascadeSplits.z) index = 2;\n"
           "  float shadow = projectedShadow(cascadeMatrix[index],\n"
           "                                 cascadeRects[index], position);\n"
           "  if (index < 3) {\n"
           "    float lower = index == 0 ? 0.0 :\n"
           "                  (index == 1 ? cascadeSplits.x : cascadeSplits.y);\n"
           "    float upper = index == 0 ? cascadeSplits.x :\n"
           "                  (index == 1 ? cascadeSplits.y : cascadeSplits.z);\n"
           "    float band = (upper - lower) * atlasParameters.z;\n"
           "    float blend = smoothstep(upper - band, upper, depth);\n"
           "    if (blend > 0.0)\n"
           "      shadow = mix(shadow, projectedShadow(cascadeMatrix[index + 1],\n"
           "                   cascadeRects[index + 1], position), blend);\n"
           "  }\n"
           "  return mix(shadow, 1.0,\n"
           "             smoothstep(atlasParameters.w * 0.8, atlasParameters.w, depth));\n"
           "}\n"

           "float pointShadow(GpuLight light, vec3 position) {\n"
           "  vec3 ray = position - light.position.xyz;\n"
           "  vec3 magnitude = abs(ray);\n"
           "  int face;\n"
           "  vec2 tileUv;\n"
           "  if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {\n"
           "    face = ray.x > 0.0 ? 0 : 1;\n"
           "    tileUv = ray.x > 0.0 ? vec2(-ray.z, -ray.y) / magnitude.x\n"
           "                         : vec2( ray.z, -ray.y) / magnitude.x;\n"
           "  } else if (magnitude.y >= magnitude.z) {\n"
           "    face = ray.y > 0.0 ? 2 : 3;\n"
           "    tileUv = ray.y > 0.0 ? vec2( ray.x,  ray.z) / magnitude.y\n"
           "                         : vec2( ray.x, -ray.z) / magnitude.y;\n"
           "  } else {\n"
           "    face = ray.z > 0.0 ? 4 : 5;\n"
           "    tileUv = ray.z > 0.0 ? vec2( ray.x, -ray.y) / magnitude.z\n"
           "                         : vec2(-ray.x, -ray.y) / magnitude.z;\n"
           "  }\n"
           "  tileUv = tileUv * 0.5 + 0.5;\n"
           "  vec4 rect = light.shadowRect;\n"
           "  rect.z += float(face) * rect.x;\n"
           "  float reference = length(ray) * light.parameters.z - 0.004;\n"
           "  return pcf13(toAtlas(tileUv, rect), clamp(reference, 0.0, 1.0), rect);\n"
           "}\n"

           "void main() {\n"
           "  vec3 normal = normalize(worldNormal);\n"
           "  vec3 accumulated = vec3(0.03, 0.035, 0.05);\n"
           "  int count = int(lightCount.x);\n"
           "  for (int index = 0; index < 6; ++index) {\n"
           "    if (index >= count) break;\n"
           "    GpuLight light = lights[index];\n"
           "    int type = int(light.position.w);\n"
           "    vec3 toLight;\n"
           "    float attenuation = 1.0;\n"
           "    if (type == 0) {\n"
           "      toLight = normalize(-light.direction.xyz);\n"
           "    } else {\n"
           "      vec3 delta = light.position.xyz - worldPosition;\n"
           "      float distanceToLight = length(delta);\n"
           "      toLight = delta / max(distanceToLight, 0.0001);\n"
           "      attenuation = 1.0 / (0.5 + 0.09 * distanceToLight +\n"
           "          0.032 * distanceToLight * distanceToLight);\n"
           "      attenuation *= max((light.direction.w - distanceToLight) /\n"
           "                         max(light.direction.w, 0.0001), 0.0);\n"
           "      if (type == 2) {\n"
           "        float cosine = dot(-toLight, normalize(light.direction.xyz));\n"
           "        float falloff = clamp((cosine - light.parameters.y) *\n"
           "                              light.parameters.x, 0.0, 1.0);\n"
           "        attenuation *= falloff * falloff;\n"
           "      }\n"
           "    }\n"
           "    float lambert = max(dot(normal, toLight), 0.0);\n"
           "    if (lambert <= 0.0 || attenuation <= 0.0) continue;\n"
           "    float shadow = 1.0;\n"
           "    if (light.parameters.w > 0.5) {\n"
           "      float slope = 1.0 - lambert;\n"
           "      vec3 offset = worldPosition + normal * slope * atlasParameters.y;\n"
           "      if (type == 0) shadow = cascadeShadow(offset, viewDepth);\n"
           "      else if (type == 2) shadow = projectedShadow(light.shadowMatrix,\n"
           "                                                   light.shadowRect, offset);\n"
           "      else shadow = pointShadow(light, offset);\n"
           "    }\n"
           "    accumulated += light.colour.rgb * light.colour.a * lambert *\n"
           "                   attenuation * shadow;\n"
           "  }\n"
           "  vec3 colour = vec3(0.74, 0.72, 0.68) * accumulated;\n"
           "  colour = colour / (colour + vec3(1.0));\n"
           "  fragmentColour = vec4(pow(colour, vec3(1.0 / 2.2)), 1.0);\n"
           "}\n";
  }

  float snapTo(float value, float unit)
  {
    return unit > 0.0f ? std::floor(value / unit) * unit : value;
  }

  demo::Mat4 buildCascade(const demo::Camera &camera, float aspect,
                          const demo::Vec3 &lightDirection, float nearSplit,
                          float farSplit, std::uint32_t resolution)
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
    radius *= static_cast<float>(resolution) /
              static_cast<float>(resolution - 2);

    const demo::Vec3 forward = demo::normalize(lightDirection);
    const demo::Vec3 reference =
        std::fabs(forward.y) > 0.95f ? demo::Vec3{0.0f, 0.0f, 1.0f}
                                     : demo::Vec3{0.0f, 1.0f, 0.0f};
    const demo::Vec3 right = demo::normalize(demo::cross(forward, reference));
    const demo::Vec3 up = demo::cross(right, forward);
    const float unit = radius * 2.0f / static_cast<float>(resolution);
    const demo::Vec3 snapped =
        demo::add(demo::add(demo::scale(right, snapTo(demo::dot(right, centre), unit)),
                            demo::scale(up, snapTo(demo::dot(up, centre), unit))),
                  demo::scale(forward, demo::dot(forward, centre)));
    const float extrude = 70.0f;
    const demo::Vec3 eye =
        demo::subtract(snapped, demo::scale(forward, radius + extrude));
    return demo::multiply(
        demo::orthographic(-radius, radius, -radius, radius, 0.0f,
                           radius * 2.0f + extrude),
        demo::lookAt(eye, snapped, up));
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

  struct SceneLight
  {
    int type = 1;
    demo::Vec3 position;
    demo::Vec3 direction{0.0f, -1.0f, 0.0f};
    demo::Vec3 colour{1.0f, 1.0f, 1.0f};
    float range = 14.0f;
    float intensity = 18.0f;
    float outerRadians = 0.55f;
    bool castsShadow = true;
    bool hasTile = false;
    demo::PackedRect tile;
    demo::Mat4 shadowMatrix;
    float importance = 0.0f;
  };

} // namespace

int main(int argc, char **argv)
{
  const demo::Options options =
      demo::parseOptions(argc, argv, "lights_shadows.ppm");
  const std::uint32_t captureWidth = 640;
  const std::uint32_t captureHeight = 400;
  demo::Host *host = demo::createHost("Luzes + sombras", 1152, 720,
                                      options.preferES, options.capture);
  if (!host)
    return 1;
  gpu::Device *device = demo::device(*host);
  const bool es = demo::isES(*host);
  const gpu::GPUCapabilities &capabilities = device->capabilities();
  std::printf("backend: %s | atlas %ux%u\n",
              es ? "OpenGL ES" : "OpenGL desktop", AtlasSize, AtlasSize);

  demo::Scene scene = demo::createScene(*device);

  gpu::TextureDesc atlasDesc;
  atlasDesc.format = gpu::Format::Depth32Float;
  atlasDesc.width = AtlasSize;
  atlasDesc.height = AtlasSize;
  atlasDesc.usage = gpu::TextureUsageRenderTarget |
                    gpu::TextureUsageSampled | gpu::TextureUsageCopySource;
  atlasDesc.debugName = "shadow atlas";
  const gpu::TextureHandle atlas = device->createTexture(atlasDesc);

  gpu::SamplerDesc samplerDesc;
  samplerDesc.minFilter = gpu::Filter::Linear;
  samplerDesc.magFilter = gpu::Filter::Linear;
  samplerDesc.mipFilter = gpu::Filter::Nearest;
  samplerDesc.addressU = gpu::AddressMode::ClampToEdge;
  samplerDesc.addressV = gpu::AddressMode::ClampToEdge;
  samplerDesc.compareEnabled = true;
  samplerDesc.compare = gpu::CompareOp::LessEqual;
  samplerDesc.debugName = "atlas compare sampler";
  const gpu::SamplerHandle atlasSampler = device->createSampler(samplerDesc);

  const std::string shadowVS = shadowVertex(es);
  const std::string projectedFS = shadowFragmentProjected(es);
  const std::string linearFS = shadowFragmentLinear(es);
  gpu::PipelineDesc shadowPipelineDesc;
  shadowPipelineDesc.vertex.source = demo::view(shadowVS);
  shadowPipelineDesc.fragment.source = demo::view(projectedFS);
  shadowPipelineDesc.vertexBufferCount = 1;
  shadowPipelineDesc.vertexBuffers[0].stride = 24;
  shadowPipelineDesc.vertexBuffers[0].attributeCount = 1;
  shadowPipelineDesc.vertexBuffers[0].attributes[0].format =
      gpu::VertexFormat::Float32x3;
  shadowPipelineDesc.colorTargetCount = 0;
  shadowPipelineDesc.depthStencil.format = gpu::Format::Depth32Float;
  shadowPipelineDesc.depthStencil.depthTestEnabled = true;
  shadowPipelineDesc.depthStencil.depthWriteEnabled = true;
  shadowPipelineDesc.depthStencil.depthCompare = gpu::CompareOp::LessEqual;
  shadowPipelineDesc.raster.cullMode = gpu::CullMode::None;
  shadowPipelineDesc.raster.scissorEnabled = true;
  shadowPipelineDesc.raster.depthBiasConstant = 3.0f;
  shadowPipelineDesc.raster.depthBiasSlope = 2.0f;
  shadowPipelineDesc.debugName = "atlas projected depth";
  const gpu::PipelineHandle projectedPipeline =
      device->createPipeline(shadowPipelineDesc);

  gpu::PipelineDesc linearPipelineDesc = shadowPipelineDesc;
  linearPipelineDesc.fragment.source = demo::view(linearFS);
  linearPipelineDesc.raster.depthBiasConstant = 0.0f;
  linearPipelineDesc.raster.depthBiasSlope = 0.0f;
  linearPipelineDesc.debugName = "atlas linear depth";
  const gpu::PipelineHandle linearPipeline =
      device->createPipeline(linearPipelineDesc);

  const std::string sceneVS = sceneVertex(es);
  const std::string sceneFS = sceneFragment(es);
  gpu::PipelineDesc scenePipelineDesc = shadowPipelineDesc;
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
  scenePipelineDesc.raster.scissorEnabled = false;
  scenePipelineDesc.raster.depthBiasConstant = 0.0f;
  scenePipelineDesc.raster.depthBiasSlope = 0.0f;
  scenePipelineDesc.debugName = "atlas forward";
  const gpu::PipelineHandle scenePipeline =
      device->createPipeline(scenePipelineDesc);

  const std::uint32_t alignment =
      std::max(capabilities.uniformBufferOffsetAlignment, 1u);
  const auto align = [&](std::uint32_t size) {
    return ((size + alignment - 1) / alignment) * alignment;
  };
  const std::uint32_t tileStride = align(sizeof(ShadowTileUniforms));
  const std::uint32_t objectStride = align(sizeof(ObjectUniforms));

  gpu::BufferDesc tileBufferDesc;
  tileBufferDesc.size = tileStride * MaxTiles;
  tileBufferDesc.usage = gpu::BufferUsageUniform;
  tileBufferDesc.debugName = "tile uniforms";
  const gpu::BufferHandle tileBuffer = device->createBuffer(tileBufferDesc);

  gpu::BufferDesc frameBufferDesc;
  frameBufferDesc.size = sizeof(FrameUniforms);
  frameBufferDesc.usage = gpu::BufferUsageUniform;
  frameBufferDesc.debugName = "frame uniforms";
  const gpu::BufferHandle frameBuffer = device->createBuffer(frameBufferDesc);

  gpu::BufferDesc objectBufferDesc;
  objectBufferDesc.size = objectStride * scene.objectCount;
  objectBufferDesc.usage = gpu::BufferUsageUniform;
  objectBufferDesc.debugName = "object light lists";
  const gpu::BufferHandle objectBuffer = device->createBuffer(objectBufferDesc);

  gpu::TextureHandle depthTarget;
  gpu::TextureHandle captureTarget;

  if (demo::reportErrors(*device, "setup"))
  {
    demo::destroyHost(host);
    return 1;
  }

  std::vector<SceneLight> lights;
  lights.reserve(1 + SceneSpotCount + ScenePointCount);
  {
    SceneLight sun;
    sun.type = 0;
    sun.colour = {1.0f, 0.97f, 0.90f};
    sun.intensity = 1.15f;
    lights.push_back(sun);
    for (std::uint32_t index = 0; index < SceneSpotCount; ++index)
    {
      SceneLight spot;
      spot.type = 2;
      spot.range = 26.0f;
      spot.intensity = 8.0f;
      spot.outerRadians = 0.34f;
      spot.colour = {0.45f, 0.75f, 1.0f};
      lights.push_back(spot);
    }
    for (std::uint32_t index = 0; index < ScenePointCount; ++index)
    {
      SceneLight point;
      point.type = 1;
      point.range = 7.5f;
      point.intensity = 4.0f;
      const float hue = static_cast<float>(index) / ScenePointCount;
      point.colour = {0.5f + 0.5f * std::cos(hue * 6.28f),
                      0.5f + 0.5f * std::cos(hue * 6.28f + 2.09f),
                      0.5f + 0.5f * std::cos(hue * 6.28f + 4.19f)};
      lights.push_back(point);
    }
  }

  demo::Camera camera;
  camera.eye = {0.0f, 10.0f, 18.0f};
  camera.target = {0.0f, 1.5f, 0.0f};
  camera.farPlane = 90.0f;

  const float shadowFar = 50.0f;
  std::uint32_t width = 1152;
  std::uint32_t height = 720;
  float time = 0.0f;
  std::uint32_t frameIndex = 0;
  std::vector<std::uint8_t> tileData(tileStride * MaxTiles);
  std::vector<std::uint8_t> objectData(objectStride * scene.objectCount);
  demo::RectPacker packer;

  while (demo::pumpEvents(*host))
  {
    time += 0.016f;
    demo::drawableSize(*host, width, height);
    if (options.capture)
    {
      width = captureWidth;
      height = captureHeight;
    }
    const float aspect =
        static_cast<float>(width) / static_cast<float>(height);

    lights[0].direction = demo::normalize(
        {std::cos(time * 0.16f) * 0.9f, -0.75f,
         std::sin(time * 0.16f) * 0.55f - 0.55f});
    for (std::uint32_t index = 0; index < SceneSpotCount; ++index)
    {
      SceneLight &spot = lights[1 + index];
      const float phase = time * 0.4f + static_cast<float>(index) * 2.1f;
      spot.position = {std::cos(phase) * 8.0f, 9.0f, std::sin(phase) * 8.0f};
      spot.direction = demo::normalize(
          demo::subtract(demo::Vec3{0.0f, 0.0f, 0.0f}, spot.position));
    }
    for (std::uint32_t index = 0; index < ScenePointCount; ++index)
    {
      SceneLight &point = lights[1 + SceneSpotCount + index];
      const float phase = time * 0.7f + static_cast<float>(index) * 1.05f;
      const float radius = 3.0f + static_cast<float>(index % 3) * 2.5f;
      point.position = {std::cos(phase) * radius, 2.0f,
                        std::sin(phase) * radius};
    }

    FrameUniforms frame;
    std::memset(&frame, 0, sizeof(frame));

    float iterativeScaling = 1.0f;
    bool packedOk = false;
    while (iterativeScaling > 0.03f)
    {
      packer.clear();
      for (std::size_t index = 0; index < lights.size(); ++index)
      {
        SceneLight &light = lights[index];
        light.hasTile = false;
        if (!light.castsShadow)
          continue;
        float amount = iterativeScaling;
        if (light.type != 0)
        {
          const float distance =
              demo::length(demo::subtract(light.position, camera.eye));
          amount = std::min(1.0f, light.range / std::max(0.001f, distance)) *
                   iterativeScaling;
        }
        int rectWidth = 0;
        int rectHeight = 0;
        if (light.type == 0)
        {
          const int side =
              static_cast<int>(MaxCascadeResolution * amount);
          rectWidth = side * static_cast<int>(CascadeCount);
          rectHeight = side;
        }
        else if (light.type == 2)
        {
          const int side = static_cast<int>(MaxSpotResolution * amount);
          rectWidth = side;
          rectHeight = side;
        }
        else
        {
          const int side = static_cast<int>(MaxPointResolution * amount);
          rectWidth = side * 6;
          rectHeight = side;
        }
        if (rectWidth > 8 && rectHeight > 8)
          packer.addRect(static_cast<int>(index), rectWidth, rectHeight);
      }
      if (packer.rects().empty())
        break;
      if (packer.pack(static_cast<int>(AtlasSize)))
      {
        packedOk = true;
        break;
      }
      iterativeScaling *= 0.5f;
    }

    std::uint32_t tilesWithShadow = 0;
    if (packedOk)
    {
      for (const demo::PackedRect &rect : packer.rects())
      {
        if (!rect.packed)
          continue;
        SceneLight &light = lights[static_cast<std::size_t>(rect.id)];
        light.hasTile = true;
        light.tile = rect;
        ++tilesWithShadow;
      }
    }

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
      splits[index] = logSplit * 0.85f + uniformSplit * 0.15f;
    }

    const std::uint32_t cascadeResolution =
        lights[0].hasTile
            ? std::max<std::uint32_t>(
                  8, static_cast<std::uint32_t>(lights[0].tile.w) / CascadeCount)
            : MaxCascadeResolution;
    demo::Mat4 cascadeMatrices[CascadeCount];
    for (std::uint32_t index = 0; index < CascadeCount; ++index)
      cascadeMatrices[index] =
          buildCascade(camera, aspect, lights[0].direction, splits[index],
                       splits[index + 1], cascadeResolution);

    std::memset(tileData.data(), 0, tileData.size());
    struct TileDraw
    {
      Rect rect;
      std::uint32_t index;
      bool linear;
    };
    std::vector<TileDraw> tileDraws;
    std::uint32_t nextTile = 0;

    for (std::size_t index = 0; index < lights.size(); ++index)
    {
      SceneLight &light = lights[index];
      if (!light.hasTile)
        continue;
      if (light.type == 0)
      {
        for (std::uint32_t cascade = 0; cascade < CascadeCount; ++cascade)
        {
          if (nextTile >= MaxTiles)
            break;
          ShadowTileUniforms tile;
          std::memset(&tile, 0, sizeof(tile));
          writeMatrix(tile.viewProjection, cascadeMatrices[cascade]);
          std::memcpy(tileData.data() + nextTile * tileStride, &tile,
                      sizeof(tile));
          tileDraws.push_back({subTile(light.tile, cascade, CascadeCount),
                               nextTile, false});
          writeRect(frame.cascadeRects[cascade],
                    subTile(light.tile, cascade, CascadeCount));
          ++nextTile;
        }
      }
      else if (light.type == 2)
      {
        if (nextTile >= MaxTiles)
          continue;
        light.shadowMatrix = demo::multiply(
            demo::perspective(light.outerRadians * 2.0f, 1.0f, 0.5f,
                              light.range),
            demo::lookAt(light.position,
                         demo::add(light.position, light.direction),
                         {0.0f, 1.0f, 0.0f}));
        ShadowTileUniforms tile;
        std::memset(&tile, 0, sizeof(tile));
        writeMatrix(tile.viewProjection, light.shadowMatrix);
        std::memcpy(tileData.data() + nextTile * tileStride, &tile,
                    sizeof(tile));
        tileDraws.push_back({subTile(light.tile, 0, 1), nextTile, false});
        ++nextTile;
      }
      else
      {
        const demo::Mat4 projection =
            demo::perspective(1.5707963f, 1.0f, 0.2f, light.range);
        for (std::uint32_t face = 0; face < 6; ++face)
        {
          if (nextTile >= MaxTiles)
            break;
          ShadowTileUniforms tile;
          std::memset(&tile, 0, sizeof(tile));
          writeMatrix(tile.viewProjection,
                      demo::multiply(projection,
                                     cubeFaceView(light.position, face)));
          tile.lightPosition[0] = light.position.x;
          tile.lightPosition[1] = light.position.y;
          tile.lightPosition[2] = light.position.z;
          tile.lightPosition[3] = 1.0f / light.range;
          std::memcpy(tileData.data() + nextTile * tileStride, &tile,
                      sizeof(tile));
          tileDraws.push_back({subTile(light.tile, face, 6), nextTile, true});
          ++nextTile;
        }
      }
    }
    device->updateBuffer(tileBuffer, 0, {tileData.data(), tileData.size()});

    gpu::RenderPassDesc shadowPass;
    shadowPass.colorCount = 0;
    shadowPass.hasDepthStencil = true;
    shadowPass.depthStencil.target.texture = atlas;
    shadowPass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
    shadowPass.depthStencil.clearDepth = 1.0f;
    std::uint32_t drawnTiles = 0;
    if (device->beginRenderPass(shadowPass))
    {
      const auto drawTile = [&](const Rect &rect, std::uint32_t tileIndex) {
        gpu::Viewport tileViewport;
        tileViewport.x = static_cast<float>(rect.x);
        tileViewport.y = static_cast<float>(rect.y);
        tileViewport.width = static_cast<float>(rect.w);
        tileViewport.height = static_cast<float>(rect.h);
        device->setViewport(tileViewport);
        gpu::Rect scissor;
        scissor.x = static_cast<std::int32_t>(rect.x);
        scissor.y = static_cast<std::int32_t>(rect.y);
        scissor.width = rect.w;
        scissor.height = rect.h;
        device->setScissor(scissor);
        device->bindUniformBuffer(0, tileBuffer, tileIndex * tileStride,
                                  sizeof(ShadowTileUniforms));
        device->draw(scene.vertexCount);
        ++drawnTiles;
      };

      device->setPipeline(projectedPipeline);
      device->bindVertexBuffer(0, scene.vertices, 0);
      for (const TileDraw &draw : tileDraws)
        if (!draw.linear)
          drawTile(draw.rect, draw.index);

      device->setPipeline(linearPipeline);
      device->bindVertexBuffer(0, scene.vertices, 0);
      for (const TileDraw &draw : tileDraws)
        if (draw.linear)
          drawTile(draw.rect, draw.index);

      device->endRenderPass();
    }

    writeMatrix(frame.viewProjection,
                demo::multiply(camera.projection(aspect), camera.view()));
    for (std::uint32_t index = 0; index < CascadeCount; ++index)
    {
      writeMatrix(frame.cascadeMatrix[index], cascadeMatrices[index]);
      frame.cascadeSplits[index] = splits[index + 1];
    }
    frame.atlasParameters[0] = 1.0f / static_cast<float>(AtlasSize);
    frame.atlasParameters[1] = 0.07f;
    frame.atlasParameters[2] = 0.25f;
    frame.atlasParameters[3] = shadowFar;
    device->updateBuffer(frameBuffer, 0, {&frame, sizeof(frame)});

    std::uint32_t selectedTotal = 0;
    std::uint32_t shadowedTotal = 0;
    std::memset(objectData.data(), 0, objectData.size());
    for (std::uint32_t index = 0; index < scene.objectCount; ++index)
    {
      const demo::SceneObject &object = scene.objects[index];
      std::vector<SceneLight *> shadowed;
      std::vector<SceneLight *> plain;
      for (SceneLight &light : lights)
      {
        if (options.onlyLight >= 0 &&
            &light != &lights[static_cast<std::size_t>(options.onlyLight) %
                              lights.size()])
          continue;
        if (light.type != 0)
        {
          const float distance =
              demo::length(demo::subtract(light.position, object.centre));
          if (distance > light.range + object.radius)
            continue;
          light.importance = light.intensity /
                             std::max(distance * distance, 0.001f);
        }
        if (light.hasTile)
          shadowed.push_back(&light);
        else
          plain.push_back(&light);
      }
      std::stable_sort(plain.begin(), plain.end(),
                       [](const SceneLight *a, const SceneLight *b) {
                         return a->importance > b->importance;
                       });

      ObjectUniforms uniforms;
      std::memset(&uniforms, 0, sizeof(uniforms));
      std::uint32_t used = 0;
      const auto append = [&](const SceneLight &light) {
        if (used >= MaxLightsPerObject)
          return;
        GpuLight &out = uniforms.lights[used++];
        out.position[0] = light.position.x;
        out.position[1] = light.position.y;
        out.position[2] = light.position.z;
        out.position[3] = static_cast<float>(light.type);
        out.direction[0] = light.direction.x;
        out.direction[1] = light.direction.y;
        out.direction[2] = light.direction.z;
        out.direction[3] = light.range;
        out.colour[0] = light.colour.x;
        out.colour[1] = light.colour.y;
        out.colour[2] = light.colour.z;
        out.colour[3] = light.intensity;
        out.parameters[2] = 1.0f / light.range;
        if (light.type == 2)
        {
          out.parameters[0] =
              1.0f / std::max(std::cos(light.outerRadians * 0.7f) -
                                  std::cos(light.outerRadians),
                              0.0001f);
          out.parameters[1] = std::cos(light.outerRadians);
        }
        const bool hasShadow = light.hasTile;
        out.parameters[3] = hasShadow ? 1.0f : 0.0f;
        if (hasShadow)
          ++shadowedTotal;
        if (light.type == 2 && light.hasTile)
        {
          writeRect(out.shadowRect, subTile(light.tile, 0, 1));
          writeMatrix(out.shadowMatrix, light.shadowMatrix);
        }
        if (light.type == 1 && light.hasTile)
          writeRect(out.shadowRect, subTile(light.tile, 0, 6));
      };
      for (const SceneLight *light : shadowed)
        append(*light);
      for (const SceneLight *light : plain)
        append(*light);
      uniforms.lightCount[0] = static_cast<float>(used);
      selectedTotal += used;
      std::memcpy(objectData.data() + index * objectStride, &uniforms,
                  sizeof(uniforms));
    }
    device->updateBuffer(objectBuffer, 0,
                         {objectData.data(), objectData.size()});

    if (!depthTarget.valid())
    {
      gpu::TextureDesc depthDesc;
      depthDesc.format = gpu::Format::Depth32Float;
      depthDesc.width = width;
      depthDesc.height = height;
      depthDesc.usage = gpu::TextureUsageRenderTarget;
      depthDesc.debugName = "scene depth";
      depthTarget = device->createTexture(depthDesc);
      if (options.capture)
      {
        gpu::TextureDesc colourDesc = depthDesc;
        colourDesc.format = gpu::Format::RGBA8;
        colourDesc.usage =
            gpu::TextureUsageRenderTarget | gpu::TextureUsageCopySource;
        colourDesc.debugName = "capture colour";
        captureTarget = device->createTexture(colourDesc);
      }
    }

    gpu::RenderPassDesc scenePass;
    scenePass.colorCount = 1;
    scenePass.colors[0].surface = !options.capture;
    if (options.capture)
      scenePass.colors[0].target.texture = captureTarget;
    scenePass.colors[0].loadOp = gpu::LoadOp::Clear;
    scenePass.colors[0].clearColor[0] = 0.03f;
    scenePass.colors[0].clearColor[1] = 0.04f;
    scenePass.colors[0].clearColor[2] = 0.06f;
    scenePass.colors[0].clearColor[3] = 1.0f;
    scenePass.hasDepthStencil = true;
    if (options.capture)
      scenePass.depthStencil.target.texture = depthTarget;
    scenePass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
    scenePass.depthStencil.clearDepth = 1.0f;
    if (device->beginRenderPass(scenePass))
    {
      gpu::Viewport viewport;
      viewport.width = static_cast<float>(width);
      viewport.height = static_cast<float>(height);
      device->setViewport(viewport);
      device->setPipeline(scenePipeline);
      device->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
      device->bindTexture(0, atlas, atlasSampler);
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
        std::printf("luzes na cena: %u (1 sol + %u spot + %u point)\n",
                    static_cast<std::uint32_t>(lights.size()), SceneSpotCount,
                    ScenePointCount);
        std::printf("atlas: %dx%d de %ux%u | escala %.3f | tiles desenhados: %u\n",
                    packer.width(), packer.height(), AtlasSize, AtlasSize,
                    static_cast<double>(iterativeScaling), drawnTiles);
        std::printf("luzes escolhidas: %u no total, %u com sombra "
                    "(max %u por objeto, %u objetos)\n",
                    selectedTotal, shadowedTotal, MaxLightsPerObject,
                    scene.objectCount);
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
        if (!device->capabilities().depthReadback)
          std::printf("atlas: este backend nao le texturas de profundidade "
                      "(ES/WebGL2) — dump indisponivel\n");
        std::vector<float> depth(
            device->capabilities().depthReadback ? AtlasSize * AtlasSize : 0);
        gpu::TextureRegion atlasRegion;
        atlasRegion.width = AtlasSize;
        atlasRegion.height = AtlasSize;
        if (device->capabilities().depthReadback &&
            device->readTexture(atlas, atlasRegion,
                                {depth.data(), depth.size() * sizeof(float)}))
        {
          const auto report = [&](const char *name, const Rect &rect) {
            std::uint32_t written = 0;
            for (std::uint32_t y = 0; y < rect.h; ++y)
              for (std::uint32_t x = 0; x < rect.w; ++x)
                if (depth[(rect.y + y) * AtlasSize + rect.x + x] < 0.999f)
                  ++written;
            std::printf("  %-26s escrito %5.1f%%\n", name,
                        100.0 * written / (rect.w * rect.h));
          };
          std::printf("atlas (%dx%d usado, escala %.3f):\n",
                      packer.width(), packer.height(),
                      static_cast<double>(iterativeScaling));
          for (std::size_t index = 0; index < lights.size(); ++index)
          {
            const SceneLight &light = lights[index];
            if (!light.hasTile)
              continue;
            const char *kind = light.type == 0   ? "sol"
                               : light.type == 2 ? "spot"
                                                 : "point";
            const std::uint32_t tiles =
                light.type == 0 ? CascadeCount : (light.type == 2 ? 1u : 6u);
            for (std::uint32_t sub = 0; sub < tiles; ++sub)
            {
              char label[48];
              std::snprintf(label, sizeof(label), "%s%u.%u @%d,%d %dx%d", kind,
                            static_cast<unsigned>(index), sub, light.tile.x,
                            light.tile.y, light.tile.w / static_cast<int>(tiles),
                            light.tile.h);
              report(label, subTile(light.tile, sub, tiles));
            }
          }
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
  device->destroy(tileBuffer);
  device->destroy(scenePipeline);
  device->destroy(linearPipeline);
  device->destroy(projectedPipeline);
  device->destroy(atlasSampler);
  device->destroy(atlas);
  demo::destroyScene(*device, scene);
  demo::destroyHost(host);
  return 0;
}
