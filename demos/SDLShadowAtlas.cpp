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
  const std::uint32_t CascadeTile = 512;
  const std::uint32_t SpotTile = 512;
  const std::uint32_t PointTile = 256;
  const std::uint32_t MaxLightsPerObject = 3;
  const std::uint32_t TileCount = CascadeCount + 1 + 6;

  struct Rect
  {
    std::uint32_t x = 0, y = 0, w = 0, h = 0;
  };

  Rect cascadeRect(std::uint32_t index)
  {
    return {index * CascadeTile, 0, CascadeTile, CascadeTile};
  }
  Rect spotRect() { return {0, CascadeTile, SpotTile, SpotTile}; }
  Rect pointRect(std::uint32_t face)
  {
    return {face * PointTile, CascadeTile + SpotTile, PointTile, PointTile};
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
    GpuLight lights[MaxLightsPerObject];
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
           "  GpuLight lights[3];\n"
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
           "  vec3 accumulated = vec3(0.05, 0.055, 0.07);\n"
           "  int count = int(lightCount.x);\n"
           "  for (int index = 0; index < 3; ++index) {\n"
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
                          float farSplit)
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
    radius *= static_cast<float>(CascadeTile) /
              static_cast<float>(CascadeTile - 2);

    const demo::Vec3 forward = demo::normalize(lightDirection);
    const demo::Vec3 reference =
        std::fabs(forward.y) > 0.95f ? demo::Vec3{0.0f, 0.0f, 1.0f}
                                     : demo::Vec3{0.0f, 1.0f, 0.0f};
    const demo::Vec3 right = demo::normalize(demo::cross(forward, reference));
    const demo::Vec3 up = demo::cross(right, forward);
    const float unit = radius * 2.0f / static_cast<float>(CascadeTile);
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

} // namespace

int main(int argc, char **argv)
{
  const demo::Options options =
      demo::parseOptions(argc, argv, "shadow_atlas.ppm");
  const std::uint32_t captureWidth = 640;
  const std::uint32_t captureHeight = 400;
  demo::Host *host = demo::createHost("Shadow atlas: dir + spot + point", 1152,
                                      720, options.preferES, options.capture);
  if (!host)
    return 1;
  gpu::Device *device = demo::device(*host);
  const bool es = demo::isES(*host);
  const gpu::GPUCapabilities &capabilities = device->capabilities();
  std::printf("backend: %s | atlas %ux%u | max textura %u\n",
              es ? "OpenGL ES" : "OpenGL desktop", AtlasSize, AtlasSize,
              capabilities.maxTextureDimension2D);

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
  linearPipelineDesc.raster.cullMode = gpu::CullMode::None;
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
  tileBufferDesc.size = tileStride * TileCount;
  tileBufferDesc.usage = gpu::BufferUsageUniform;
  tileBufferDesc.debugName = "atlas tile uniforms";
  const gpu::BufferHandle tileBuffer = device->createBuffer(tileBufferDesc);

  gpu::BufferDesc frameBufferDesc;
  frameBufferDesc.size = sizeof(FrameUniforms);
  frameBufferDesc.usage = gpu::BufferUsageUniform;
  frameBufferDesc.debugName = "atlas frame uniforms";
  const gpu::BufferHandle frameBuffer = device->createBuffer(frameBufferDesc);

  gpu::BufferDesc objectBufferDesc;
  objectBufferDesc.size = objectStride * scene.objectCount;
  objectBufferDesc.usage = gpu::BufferUsageUniform;
  objectBufferDesc.debugName = "atlas object uniforms";
  const gpu::BufferHandle objectBuffer = device->createBuffer(objectBufferDesc);

  gpu::TextureHandle depthTarget;
  gpu::TextureHandle captureTarget;

  if (demo::reportErrors(*device, "setup"))
  {
    demo::destroyHost(host);
    return 1;
  }

  demo::Camera camera;
  camera.eye = {0.0f, 9.0f, 16.0f};
  camera.target = {0.0f, 1.5f, 0.0f};
  camera.farPlane = 80.0f;

  const float shadowFar = 50.0f;
  std::uint32_t width = 1152;
  std::uint32_t height = 720;
  float time = 0.0f;
  std::uint32_t frameIndex = 0;
  std::vector<std::uint8_t> tileData(tileStride * TileCount);
  std::vector<std::uint8_t> objectData(objectStride * scene.objectCount);

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

    const demo::Vec3 sunDirection = demo::normalize(
        {std::cos(time * 0.18f) * 0.55f, -1.0f, std::sin(time * 0.18f) * 0.55f});
    const demo::Vec3 spotPosition{std::cos(time * 0.5f) * 7.0f, 8.5f,
                                  std::sin(time * 0.5f) * 7.0f};
    const demo::Vec3 spotTarget{0.0f, 0.0f, 0.0f};
    const float spotOuter = 0.55f;
    const float spotRange = 34.0f;
    const demo::Vec3 pointPosition{std::cos(time * 0.8f + 2.0f) * 3.0f, 2.2f,
                                   std::sin(time * 0.8f + 2.0f) * 3.0f};
    const float pointRange = 16.0f;

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

    demo::Mat4 cascadeMatrices[CascadeCount];
    for (std::uint32_t index = 0; index < CascadeCount; ++index)
      cascadeMatrices[index] = buildCascade(camera, aspect, sunDirection,
                                            splits[index], splits[index + 1]);

    const demo::Mat4 spotMatrix = demo::multiply(
        demo::perspective(spotOuter * 2.0f, 1.0f, 0.5f, spotRange),
        demo::lookAt(spotPosition, spotTarget, {0.0f, 1.0f, 0.0f}));
    const demo::Mat4 pointProjection =
        demo::perspective(1.5707963f, 1.0f, 0.2f, pointRange);

    std::memset(tileData.data(), 0, tileData.size());
    for (std::uint32_t index = 0; index < CascadeCount; ++index)
    {
      ShadowTileUniforms tile;
      std::memset(&tile, 0, sizeof(tile));
      writeMatrix(tile.viewProjection, cascadeMatrices[index]);
      std::memcpy(tileData.data() + index * tileStride, &tile, sizeof(tile));
    }
    {
      ShadowTileUniforms tile;
      std::memset(&tile, 0, sizeof(tile));
      writeMatrix(tile.viewProjection, spotMatrix);
      std::memcpy(tileData.data() + CascadeCount * tileStride, &tile,
                  sizeof(tile));
    }
    for (std::uint32_t face = 0; face < 6; ++face)
    {
      ShadowTileUniforms tile;
      std::memset(&tile, 0, sizeof(tile));
      writeMatrix(tile.viewProjection,
                  demo::multiply(pointProjection,
                                 cubeFaceView(pointPosition, face)));
      tile.lightPosition[0] = pointPosition.x;
      tile.lightPosition[1] = pointPosition.y;
      tile.lightPosition[2] = pointPosition.z;
      tile.lightPosition[3] = 1.0f / pointRange;
      std::memcpy(tileData.data() + (CascadeCount + 1 + face) * tileStride,
                  &tile, sizeof(tile));
    }
    device->updateBuffer(tileBuffer, 0, {tileData.data(), tileData.size()});

    gpu::RenderPassDesc shadowPass;
    shadowPass.colorCount = 0;
    shadowPass.hasDepthStencil = true;
    shadowPass.depthStencil.target.texture = atlas;
    shadowPass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
    shadowPass.depthStencil.clearDepth = 1.0f;
    if (device->beginRenderPass(shadowPass))
    {
      gpu::Viewport viewport;
      viewport.width = static_cast<float>(AtlasSize);
      viewport.height = static_cast<float>(AtlasSize);
      device->setViewport(viewport);

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
      };

      device->setPipeline(projectedPipeline);
      device->bindVertexBuffer(0, scene.vertices, 0);
      for (std::uint32_t index = 0; index < CascadeCount; ++index)
        drawTile(cascadeRect(index), index);
      drawTile(spotRect(), CascadeCount);

      device->setPipeline(linearPipeline);
      device->bindVertexBuffer(0, scene.vertices, 0);
      for (std::uint32_t face = 0; face < 6; ++face)
        drawTile(pointRect(face), CascadeCount + 1 + face);

      device->endRenderPass();
    }

    FrameUniforms frame;
    std::memset(&frame, 0, sizeof(frame));
    writeMatrix(frame.viewProjection,
                demo::multiply(camera.projection(aspect), camera.view()));
    for (std::uint32_t index = 0; index < CascadeCount; ++index)
    {
      writeMatrix(frame.cascadeMatrix[index], cascadeMatrices[index]);
      writeRect(frame.cascadeRects[index], cascadeRect(index));
      frame.cascadeSplits[index] = splits[index + 1];
    }
    frame.atlasParameters[0] = 1.0f / static_cast<float>(AtlasSize);
    frame.atlasParameters[1] = 0.07f;
    frame.atlasParameters[2] = 0.25f;
    frame.atlasParameters[3] = shadowFar;
    device->updateBuffer(frameBuffer, 0, {&frame, sizeof(frame)});

    std::memset(objectData.data(), 0, objectData.size());
    for (std::uint32_t index = 0; index < scene.objectCount; ++index)
    {
      ObjectUniforms uniforms;
      std::memset(&uniforms, 0, sizeof(uniforms));

      GpuLight &sun = uniforms.lights[0];
      sun.position[3] = 0.0f;
      sun.direction[0] = sunDirection.x;
      sun.direction[1] = sunDirection.y;
      sun.direction[2] = sunDirection.z;
      sun.colour[0] = 1.0f;
      sun.colour[1] = 0.97f;
      sun.colour[2] = 0.90f;
      sun.colour[3] = 1.7f;
      sun.parameters[3] = 1.0f;

      GpuLight &spot = uniforms.lights[1];
      spot.position[0] = spotPosition.x;
      spot.position[1] = spotPosition.y;
      spot.position[2] = spotPosition.z;
      spot.position[3] = 2.0f;
      const demo::Vec3 spotDirection =
          demo::normalize(demo::subtract(spotTarget, spotPosition));
      spot.direction[0] = spotDirection.x;
      spot.direction[1] = spotDirection.y;
      spot.direction[2] = spotDirection.z;
      spot.direction[3] = spotRange;
      spot.colour[0] = 0.45f;
      spot.colour[1] = 0.75f;
      spot.colour[2] = 1.0f;
      spot.colour[3] = 26.0f;
      spot.parameters[0] =
          1.0f / std::max(std::cos(spotOuter * 0.7f) - std::cos(spotOuter),
                          0.0001f);
      spot.parameters[1] = std::cos(spotOuter);
      spot.parameters[2] = 1.0f / spotRange;
      spot.parameters[3] = 1.0f;
      writeRect(spot.shadowRect, spotRect());
      writeMatrix(spot.shadowMatrix, spotMatrix);

      GpuLight &point = uniforms.lights[2];
      point.position[0] = pointPosition.x;
      point.position[1] = pointPosition.y;
      point.position[2] = pointPosition.z;
      point.position[3] = 1.0f;
      point.direction[3] = pointRange;
      point.colour[0] = 1.0f;
      point.colour[1] = 0.55f;
      point.colour[2] = 0.25f;
      point.colour[3] = 22.0f;
      point.parameters[2] = 1.0f / pointRange;
      point.parameters[3] = 1.0f;
      writeRect(point.shadowRect, pointRect(0));

      uniforms.lightCount[0] = 3.0f;
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
    scenePass.colors[0].clearColor[0] = 0.05f;
    scenePass.colors[0].clearColor[1] = 0.06f;
    scenePass.colors[0].clearColor[2] = 0.09f;
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
        std::printf("passes: 1 de sombras (%u tiles) + 1 forward (%u objetos)\n",
                    TileCount, scene.objectCount);
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
        std::vector<float> depth(AtlasSize * AtlasSize);
        gpu::TextureRegion atlasRegion;
        atlasRegion.width = AtlasSize;
        atlasRegion.height = AtlasSize;
        if (device->readTexture(atlas, atlasRegion,
                                {depth.data(), depth.size() * sizeof(float)}))
        {
          const auto report = [&](const char *name, const Rect &rect) {
            float lowest = 1.0f;
            float highest = 0.0f;
            std::uint32_t written = 0;
            for (std::uint32_t y = 0; y < rect.h; ++y)
              for (std::uint32_t x = 0; x < rect.w; ++x)
              {
                const float value =
                    depth[(rect.y + y) * AtlasSize + rect.x + x];
                lowest = std::min(lowest, value);
                highest = std::max(highest, value);
                if (value < 0.999f)
                  ++written;
              }
            std::printf("  %-14s rect %4u,%4u %4ux%4u  min %.3f max %.3f  "
                        "escrito %5.1f%%\n", name, rect.x, rect.y, rect.w,
                        rect.h, static_cast<double>(lowest),
                        static_cast<double>(highest),
                        100.0 * written / (rect.w * rect.h));
          };
          std::printf("conteudo do atlas:\n");
          for (std::uint32_t index = 0; index < CascadeCount; ++index)
          {
            char label[24];
            std::snprintf(label, sizeof(label), "cascata %u", index);
            report(label, cascadeRect(index));
          }
          report("spot", spotRect());
          for (std::uint32_t face = 0; face < 6; ++face)
          {
            char label[24];
            std::snprintf(label, sizeof(label), "point face %u", face);
            report(label, pointRect(face));
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
