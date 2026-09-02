#include "RenderPathProbe.h"

#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const char *what) {
  ++checks;
  if (!ok)
    ++failures;
  std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
}

std::string glsl(const char *body, bool es) {
  std::string out = es ? "#version 300 es\nprecision highp float;\n"
                         "precision highp int;\nprecision highp sampler2D;\n"
                         "precision highp usampler2D;\n"
                       : "#version 330 core\n";
  out += body;
  return out;
}

gpu::DataView view(const std::string &s) {
  return {s.data(), s.size()};
}

} // namespace

void runRenderPathProbe(gpu::Device &deviceRef, bool es, const char *label) {
  gpu::Device *device = &deviceRef;
  failures = 0;
  checks = 0;
  std::printf("\n================ %s ================\n", label);

  const std::uint32_t atlasSize = 64;

  gpu::TextureDesc atlasDesc;
  atlasDesc.format = gpu::Format::Depth32Float;
  atlasDesc.width = atlasSize;
  atlasDesc.height = atlasSize;
  atlasDesc.usage = gpu::TextureUsageRenderTarget | gpu::TextureUsageSampled |
                    gpu::TextureUsageCopySource;
  atlasDesc.debugName = "shadow atlas";
  const gpu::TextureHandle atlas = device->createTexture(atlasDesc);
  check(atlas.valid(), "criar atlas de profundidade Depth32Float amostravel");
  assert(atlas.valid());

  const float depths[4] = {-0.6f, -0.2f, 0.2f, 0.6f};
  float vertices[4 * 3 * 3];
  for (int rect = 0; rect < 4; ++rect) {
    const float corners[3][2] = {{-1.0f, -1.0f}, {3.0f, -1.0f}, {-1.0f, 3.0f}};
    for (int v = 0; v < 3; ++v) {
      vertices[(rect * 3 + v) * 3 + 0] = corners[v][0];
      vertices[(rect * 3 + v) * 3 + 1] = corners[v][1];
      vertices[(rect * 3 + v) * 3 + 2] = depths[rect];
    }
  }
  gpu::BufferDesc vertexDesc;
  vertexDesc.size = sizeof(vertices);
  vertexDesc.usage = gpu::BufferUsageVertex;
  vertexDesc.initialData = {vertices, sizeof(vertices)};
  vertexDesc.debugName = "shadow atlas triangles";
  const gpu::BufferHandle vertexBuffer = device->createBuffer(vertexDesc);
  check(vertexBuffer.valid(), "criar vertex buffer");

  const std::string depthVS = glsl(
      "layout(location = 0) in vec3 p;\n"
      "void main() { gl_Position = vec4(p, 1.0); }\n", es);
  const std::string depthFS = glsl(
      "out vec4 c;\n"
      "void main() { c = vec4(1.0); }\n", es);

  gpu::PipelineDesc depthPipelineDesc;
  depthPipelineDesc.vertex.source = view(depthVS);
  depthPipelineDesc.fragment.source = view(depthFS);
  depthPipelineDesc.vertexBufferCount = 1;
  depthPipelineDesc.vertexBuffers[0].stride = 12;
  depthPipelineDesc.vertexBuffers[0].attributeCount = 1;
  depthPipelineDesc.vertexBuffers[0].attributes[0].format =
      gpu::VertexFormat::Float32x3;
  depthPipelineDesc.colorTargetCount = 0;
  depthPipelineDesc.depthStencil.format = gpu::Format::Depth32Float;
  depthPipelineDesc.depthStencil.depthTestEnabled = true;
  depthPipelineDesc.depthStencil.depthWriteEnabled = true;
  depthPipelineDesc.depthStencil.depthCompare = gpu::CompareOp::Always;
  depthPipelineDesc.raster.cullMode = gpu::CullMode::None;
  depthPipelineDesc.raster.scissorEnabled = true;
  depthPipelineDesc.debugName = "shadow depth only";
  const gpu::PipelineHandle depthPipeline =
      device->createPipeline(depthPipelineDesc);
  check(depthPipeline.valid(),
        "pipeline depth-only real (colorTargetCount=0)");
  if (!depthPipeline.valid()) {
    gpu::GPUError e;
    while (device->getError(e))
      std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
    assert(false);
    return;
  }

  gpu::RenderPassDesc shadowPass;
  shadowPass.colorCount = 0;
  shadowPass.hasDepthStencil = true;
  shadowPass.depthStencil.target.texture = atlas;
  shadowPass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
  shadowPass.depthStencil.clearDepth = 1.0f;

  device->clearErrors();
  bool passOk = device->beginRenderPass(shadowPass);
  check(passOk, "render pass depth-only (colorCount=0)");
  if (passOk) {
    gpu::Viewport viewport;
    viewport.width = static_cast<float>(atlasSize);
    viewport.height = static_cast<float>(atlasSize);
    bool ok = device->setViewport(viewport);
    ok = device->setPipeline(depthPipeline) && ok;
    ok = device->bindVertexBuffer(0, vertexBuffer, 0) && ok;
    const std::uint32_t half = atlasSize / 2;
    for (std::uint32_t rect = 0; rect < 4; ++rect) {
      gpu::Rect scissor;
      scissor.x = static_cast<std::int32_t>((rect % 2) * half);
      scissor.y = static_cast<std::int32_t>((rect / 2) * half);
      scissor.width = half;
      scissor.height = half;
      ok = device->setScissor(scissor) && ok;
      ok = device->draw(3, 1, rect * 3) && ok;
    }
    check(ok, "viewport + scissor por sub-regiao + 4 draws depth-only");
    device->endRenderPass();
  }
  check(device->pendingErrorCount() == 0, "passada de sombras sem erros");
  {
    gpu::GPUError e;
    while (device->getError(e))
      std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
  }

  static float readback[atlasSize * atlasSize];
  gpu::TextureRegion whole;
  whole.width = atlasSize;
  whole.height = atlasSize;
  const bool canReadDepth = device->capabilities().depthReadback;
  const bool readOk =
      device->readTexture(atlas, whole, {readback, sizeof(readback)});
  if (canReadDepth)
    check(readOk, "readTexture do atlas de profundidade");
  else
  {
    // ES 3.x / WebGL2: glReadPixels so aceita formatos de cor. Tem de ser
    // recusado pela camada, com erro claro, e nao pelo driver.
    gpu::GPUError rejection;
    const bool reported = device->getError(rejection);
    check(!readOk && reported &&
              rejection.code == gpu::GPUErrorCode::UnsupportedFeature,
          "readTexture de profundidade recusado com UnsupportedFeature");
    device->clearErrors();
  }
  if (readOk) {
    bool allPresent = true;
    bool topDownMatches = true;
    for (std::uint32_t rect = 0; rect < 4; ++rect) {
      const std::uint32_t x = (rect % 2) * (atlasSize / 2) + atlasSize / 4;
      const std::uint32_t rowBottomUp = (rect / 2) * (atlasSize / 2) + atlasSize / 4;
      const std::uint32_t rowTopDown = atlasSize - 1 - rowBottomUp;
      const float want = (depths[rect] + 1.0f) * 0.5f;
      const float asRead = readback[rowBottomUp * atlasSize + x];
      const float flipped = readback[rowTopDown * atlasSize + x];
      if (asRead < want - 0.01f || asRead > want + 0.01f)
        topDownMatches = false;
      if (flipped < want - 0.01f || flipped > want + 0.01f)
        allPresent = false;
      std::printf("        rect %u (scissor y=%u): esperado %.2f | "
                  "readTexture na mesma linha=%.2f | na linha espelhada=%.2f\n",
                  rect, (rect / 2) * (atlasSize / 2),
                  static_cast<double>(want), static_cast<double>(asRead),
                  static_cast<double>(flipped));
    }
    check(allPresent || topDownMatches,
          "as 4 sub-regioes receberam as 4 profundidades distintas");
    check(topDownMatches,
          "setScissor e readTexture usam a MESMA origem vertical");
    if (!topDownMatches && allPresent)
      std::printf("        >> setScissor usa origem no topo, readTexture usa "
                  "origem em baixo: convencoes opostas\n");
  }
  {
    gpu::GPUError e;
    while (device->getError(e))
      std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
  }

  std::printf("\n  -- amostragem de texturas no fragment shader --\n");


  gpu::TextureDesc colorDesc;
  colorDesc.format = gpu::Format::RGBA8;
  colorDesc.width = 16;
  colorDesc.height = 16;
  colorDesc.usage =
      gpu::TextureUsageRenderTarget | gpu::TextureUsageCopySource;
  const gpu::TextureHandle colorTarget = device->createTexture(colorDesc);

  gpu::SamplerDesc nearestDesc;
  nearestDesc.minFilter = gpu::Filter::Nearest;
  nearestDesc.magFilter = gpu::Filter::Nearest;
  nearestDesc.mipFilter = gpu::Filter::Nearest;
  nearestDesc.addressU = gpu::AddressMode::ClampToEdge;
  nearestDesc.addressV = gpu::AddressMode::ClampToEdge;
  nearestDesc.debugName = "shadow sampler";
  const gpu::SamplerHandle nearest = device->createSampler(nearestDesc);
  check(colorTarget.valid() && nearest.valid(), "alvo de cor + sampler");

  const std::string quadVS = glsl(
      "layout(location = 0) in vec3 p;\n"
      "out vec2 uv;\n"
      "void main() { uv = p.xy * 0.5 + 0.5; gl_Position = vec4(p.xy, 0.0, 1.0); }\n",
      es);
  const std::string sampleFS = glsl(
      "uniform sampler2D shadowAtlas;\n"
      "in vec2 uv;\n"
      "out vec4 c;\n"
      "void main() { c = vec4(texture(shadowAtlas, uv).r, 0.0, 0.0, 1.0); }\n",
      es);

  gpu::PipelineDesc samplePipelineDesc = depthPipelineDesc;
  samplePipelineDesc.vertex.source = view(quadVS);
  samplePipelineDesc.fragment.source = view(sampleFS);
  samplePipelineDesc.colorTargetCount = 1;
  samplePipelineDesc.colorTargets[0].format = gpu::Format::RGBA8;
  samplePipelineDesc.colorTargets[0].writeMask = gpu::ColorWriteAll;
  samplePipelineDesc.depthStencil.depthTestEnabled = false;
  samplePipelineDesc.depthStencil.depthWriteEnabled = false;
  samplePipelineDesc.raster.scissorEnabled = false;
  samplePipelineDesc.debugName = "sample shadow atlas";
  const gpu::PipelineHandle samplePipeline =
      device->createPipeline(samplePipelineDesc);
  check(samplePipeline.valid(), "compilar shader que amostra sampler2D");
  {
    gpu::GPUError e;
    while (device->getError(e))
      std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
  }

  if (samplePipeline.valid()) {
    gpu::RenderPassDesc colorPass;
    colorPass.colorCount = 1;
    colorPass.colors[0].target.texture = colorTarget;
    colorPass.colors[0].loadOp = gpu::LoadOp::Clear;
    device->clearErrors();
    if (device->beginRenderPass(colorPass)) {
      gpu::Viewport viewport;
      viewport.width = 16.0f;
      viewport.height = 16.0f;
      device->setViewport(viewport);
      device->setPipeline(samplePipeline);
      device->bindVertexBuffer(0, vertexBuffer, 0);
      const bool bound = device->bindTexture(0, atlas, nearest);
      check(bound, "bindTexture(slot 0) do atlas de profundidade");
      device->draw(3);
      device->endRenderPass();
    }
    std::uint8_t pixels[16 * 16 * 4];
    gpu::TextureRegion colorRegion;
    colorRegion.width = 16;
    colorRegion.height = 16;
    if (device->readTexture(colorTarget, colorRegion,
                            {pixels, sizeof(pixels)})) {
      const std::uint8_t topLeft = pixels[(4 * 16 + 4) * 4];
      const std::uint8_t topRight = pixels[(4 * 16 + 12) * 4];
      std::printf("        amostrado com uv esq=%u dir=%u"
                  "  (valores escritos: %u %u %u %u)\n",
                  topLeft, topRight,
                  static_cast<unsigned>((depths[0] + 1.0f) * 127.5f),
                  static_cast<unsigned>((depths[1] + 1.0f) * 127.5f),
                  static_cast<unsigned>((depths[2] + 1.0f) * 127.5f),
                  static_cast<unsigned>((depths[3] + 1.0f) * 127.5f));
      bool matchesSome = false;
      for (int d = 0; d < 4; ++d) {
        const int want = static_cast<int>((depths[d] + 1.0f) * 127.5f);
        if (topLeft > want - 4 && topLeft < want + 4)
          matchesSome = true;
      }
      check(topLeft != topRight,
            "o shader le valores distintos do atlas de profundidade");
      check(matchesSome,
            "o valor amostrado e uma das profundidades escritas");
    }
    {
      gpu::GPUError e;
      while (device->getError(e))
        std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
    }
  }

  std::printf("\n  -- dois samplers em slots diferentes (grid + lista) --\n");

  const std::string twoFS = glsl(
      "uniform sampler2D texA;\n"
      "uniform sampler2D texB;\n"
      "in vec2 uv;\n"
      "out vec4 c;\n"
      "void main() {\n"
      "  c = vec4(texture(texA, uv).r, texture(texB, uv).r, 0.0, 1.0);\n"
      "}\n", es);
  gpu::PipelineDesc twoDesc = samplePipelineDesc;
  twoDesc.fragment.source = view(twoFS);
  twoDesc.debugName = "two samplers";
  const gpu::PipelineHandle twoPipeline = device->createPipeline(twoDesc);
  check(twoPipeline.valid(), "compilar shader com dois sampler2D");

  if (twoPipeline.valid()) {
    std::uint8_t whiteBytes[4 * 4 * 4];
    std::memset(whiteBytes, 0xff, sizeof(whiteBytes));
    gpu::TextureDesc whiteDesc;
    whiteDesc.format = gpu::Format::RGBA8;
    whiteDesc.width = 4;
    whiteDesc.height = 4;
    whiteDesc.usage = gpu::TextureUsageSampled;
    whiteDesc.initialData = {whiteBytes, sizeof(whiteBytes)};
    whiteDesc.debugName = "white";
    const gpu::TextureHandle white = device->createTexture(whiteDesc);

    std::uint8_t blackBytes[4 * 4 * 4];
    std::memset(blackBytes, 0x00, sizeof(blackBytes));
    gpu::TextureDesc blackDesc = whiteDesc;
    blackDesc.initialData = {blackBytes, sizeof(blackBytes)};
    blackDesc.debugName = "black";
    const gpu::TextureHandle black = device->createTexture(blackDesc);

    gpu::RenderPassDesc colorPass;
    colorPass.colorCount = 1;
    colorPass.colors[0].target.texture = colorTarget;
    colorPass.colors[0].loadOp = gpu::LoadOp::Clear;
    device->clearErrors();
    if (device->beginRenderPass(colorPass)) {
      gpu::Viewport viewport;
      viewport.width = 16.0f;
      viewport.height = 16.0f;
      device->setViewport(viewport);
      device->setPipeline(twoPipeline);
      device->bindVertexBuffer(0, vertexBuffer, 0);
      const bool slot0 = device->bindTexture(0, white, nearest);
      const bool slot1 = device->bindTexture(1, black, nearest);
      check(slot0 && slot1, "bindTexture em slot 0 e slot 1");
      device->draw(3);
      device->endRenderPass();
    }
    std::uint8_t pixels[16 * 16 * 4];
    gpu::TextureRegion colorRegion;
    colorRegion.width = 16;
    colorRegion.height = 16;
    if (device->readTexture(colorTarget, colorRegion,
                            {pixels, sizeof(pixels)})) {
      const std::uint8_t red = pixels[(8 * 16 + 8) * 4 + 0];
      const std::uint8_t green = pixels[(8 * 16 + 8) * 4 + 1];
      std::printf("        texA(branca, slot 0) -> %u   "
                  "texB(preta, slot 1) -> %u\n", red, green);
      check(red == 255 && green == 0,
            "cada sampler le a textura do seu proprio slot");
    }
    std::printf("\n  -- o mesmo, mas com layout(binding=) explicito --\n");
    const std::string bindHeader =
        es ? "#version 310 es\nprecision highp float;\n"
             "precision highp sampler2D;\n"
           : "#version 420 core\n";
    const std::string bindVS = bindHeader +
        "layout(location = 0) in vec3 p;\n"
        "out vec2 uv;\n"
        "void main() { uv = p.xy * 0.5 + 0.5;"
        " gl_Position = vec4(p.xy, 0.0, 1.0); }\n";
    const std::string bindFS = bindHeader +
        "layout(binding = 0) uniform sampler2D texA;\n"
        "layout(binding = 1) uniform sampler2D texB;\n"
        "in vec2 uv;\n"
        "out vec4 c;\n"
        "void main() {\n"
        "  c = vec4(texture(texA, uv).r, texture(texB, uv).r, 0.0, 1.0);\n"
        "}\n";
    gpu::PipelineDesc bindDesc = twoDesc;
    bindDesc.vertex.source = view(bindVS);
    bindDesc.fragment.source = view(bindFS);
    bindDesc.debugName = "two samplers explicit binding";
    device->clearErrors();
    const gpu::PipelineHandle bindPipeline = device->createPipeline(bindDesc);
    check(bindPipeline.valid(),
          es ? "compilar com layout(binding=) em GLSL 310 es"
             : "compilar com layout(binding=) em GLSL 420 core");
    {
      gpu::GPUError e;
      while (device->getError(e))
        std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
    }
    if (bindPipeline.valid()) {
      gpu::RenderPassDesc bindPass;
      bindPass.colorCount = 1;
      bindPass.colors[0].target.texture = colorTarget;
      bindPass.colors[0].loadOp = gpu::LoadOp::Clear;
      if (device->beginRenderPass(bindPass)) {
        gpu::Viewport viewport;
        viewport.width = 16.0f;
        viewport.height = 16.0f;
        device->setViewport(viewport);
        device->setPipeline(bindPipeline);
        device->bindVertexBuffer(0, vertexBuffer, 0);
        device->bindTexture(0, white, nearest);
        device->bindTexture(1, black, nearest);
        device->draw(3);
        device->endRenderPass();
      }
      std::uint8_t bindPixels[16 * 16 * 4];
      if (device->readTexture(colorTarget, colorRegion,
                              {bindPixels, sizeof(bindPixels)})) {
        const std::uint8_t red = bindPixels[(8 * 16 + 8) * 4 + 0];
        const std::uint8_t green = bindPixels[(8 * 16 + 8) * 4 + 1];
        std::printf("        texA(branca, slot 0) -> %u   "
                    "texB(preta, slot 1) -> %u\n", red, green);
        check(red == 255 && green == 0,
              "com binding explicito cada sampler le o seu slot");
      }
      device->destroy(bindPipeline);
    }

    device->destroy(white);
    device->destroy(black);
    device->destroy(twoPipeline);
  }

  std::printf("\n  -- grid de luzes: usampler2D + texelFetch --\n");

  std::uint16_t gridBytes[4 * 4 * 4];
  for (std::uint32_t i = 0; i < 4 * 4 * 4; ++i)
    gridBytes[i] = static_cast<std::uint16_t>(i);
  gpu::TextureDesc gridDesc;
  gridDesc.format = gpu::Format::RGBA16Uint;
  gridDesc.width = 4;
  gridDesc.height = 4;
  gridDesc.usage = gpu::TextureUsageSampled;
  gridDesc.initialData = {gridBytes, sizeof(gridBytes)};
  gridDesc.debugName = "light grid";
  const gpu::TextureHandle grid = device->createTexture(gridDesc);
  check(grid.valid(), "criar textura RGBA16Uint para o grid de luzes");

  const std::string gridFS = glsl(
      "uniform usampler2D lightGrid;\n"
      "in vec2 uv;\n"
      "out vec4 c;\n"
      "void main() {\n"
      "  uvec4 cell = texelFetch(lightGrid, ivec2(1, 1), 0);\n"
      "  c = vec4(float(cell.x) / 255.0, float(cell.y) / 255.0, 0.0, 1.0);\n"
      "}\n", es);
  gpu::PipelineDesc gridPipelineDesc = samplePipelineDesc;
  gridPipelineDesc.fragment.source = view(gridFS);
  gridPipelineDesc.debugName = "light grid fetch";
  const gpu::PipelineHandle gridPipeline =
      device->createPipeline(gridPipelineDesc);
  check(gridPipeline.valid(), "compilar shader com usampler2D + texelFetch");
  {
    gpu::GPUError e;
    while (device->getError(e))
      std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
  }

  if (gridPipeline.valid() && grid.valid()) {
    gpu::RenderPassDesc colorPass;
    colorPass.colorCount = 1;
    colorPass.colors[0].target.texture = colorTarget;
    colorPass.colors[0].loadOp = gpu::LoadOp::Clear;
    device->clearErrors();
    if (device->beginRenderPass(colorPass)) {
      gpu::Viewport viewport;
      viewport.width = 16.0f;
      viewport.height = 16.0f;
      device->setViewport(viewport);
      device->setPipeline(gridPipeline);
      device->bindVertexBuffer(0, vertexBuffer, 0);
      device->bindTexture(0, grid, nearest);
      device->draw(3);
      device->endRenderPass();
    }
    std::uint8_t pixels[16 * 16 * 4];
    gpu::TextureRegion colorRegion;
    colorRegion.width = 16;
    colorRegion.height = 16;
    if (device->readTexture(colorTarget, colorRegion,
                            {pixels, sizeof(pixels)})) {
      const std::uint8_t red = pixels[(8 * 16 + 8) * 4 + 0];
      const std::uint8_t green = pixels[(8 * 16 + 8) * 4 + 1];
      std::printf("        texel (1,1) -> x=%u y=%u  (esperado 20 e 21)\n",
                  red, green);
      check(red == 20 && green == 21,
            "texelFetch devolve os indices escritos no grid");
    }
    {
      gpu::GPUError e;
      while (device->getError(e))
        std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
    }
    device->destroy(gridPipeline);
  }

  std::printf("\n  -- sampler de comparacao (PCF por hardware) --\n");

  check(device->capabilities().maxUniformBufferSize >= 16384,
        "maxUniformBufferSize reportado (>= minimo garantido de 16 KB)");

  gpu::SamplerDesc compareDesc;
  compareDesc.minFilter = gpu::Filter::Linear;
  compareDesc.magFilter = gpu::Filter::Linear;
  compareDesc.mipFilter = gpu::Filter::Nearest;
  compareDesc.addressU = gpu::AddressMode::ClampToEdge;
  compareDesc.addressV = gpu::AddressMode::ClampToEdge;
  compareDesc.compareEnabled = true;
  compareDesc.compare = gpu::CompareOp::Less;
  compareDesc.debugName = "shadow compare sampler";
  device->clearErrors();
  const gpu::SamplerHandle compareSampler = device->createSampler(compareDesc);
  check(compareSampler.valid() && device->pendingErrorCount() == 0,
        "criar sampler com funcao de comparacao");

  const std::string shadowFS = glsl(
      "uniform highp sampler2DShadow shadowAtlas;\n"
      "in vec2 uv;\n"
      "out vec4 c;\n"
      "void main() {\n"
      "  float near = texture(shadowAtlas, vec3(uv, 0.05));\n"
      "  float far  = texture(shadowAtlas, vec3(uv, 0.95));\n"
      "  c = vec4(near, far, 0.0, 1.0);\n"
      "}\n", es);
  gpu::PipelineDesc shadowPipelineDesc = samplePipelineDesc;
  shadowPipelineDesc.fragment.source = view(shadowFS);
  shadowPipelineDesc.debugName = "hardware pcf";
  const gpu::PipelineHandle shadowPipeline =
      device->createPipeline(shadowPipelineDesc);
  check(shadowPipeline.valid(), "compilar shader com sampler2DShadow");
  {
    gpu::GPUError e;
    while (device->getError(e))
      std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
  }

  if (shadowPipeline.valid() && compareSampler.valid()) {
    gpu::RenderPassDesc colorPass;
    colorPass.colorCount = 1;
    colorPass.colors[0].target.texture = colorTarget;
    colorPass.colors[0].loadOp = gpu::LoadOp::Clear;
    device->clearErrors();
    if (device->beginRenderPass(colorPass)) {
      gpu::Viewport viewport;
      viewport.width = 16.0f;
      viewport.height = 16.0f;
      device->setViewport(viewport);
      device->setPipeline(shadowPipeline);
      device->bindVertexBuffer(0, vertexBuffer, 0);
      device->bindTexture(0, atlas, compareSampler);
      device->draw(3);
      device->endRenderPass();
    }
    std::uint8_t pixels[16 * 16 * 4];
    gpu::TextureRegion colorRegion;
    colorRegion.width = 16;
    colorRegion.height = 16;
    if (device->readTexture(colorTarget, colorRegion,
                            {pixels, sizeof(pixels)})) {
      const std::uint8_t nearRef = pixels[(4 * 16 + 4) * 4 + 0];
      const std::uint8_t farRef = pixels[(4 * 16 + 4) * 4 + 1];
      std::printf("        ref=0.05 -> %u (esperado 0: nao ocluido)   "
                  "ref=0.95 -> %u (esperado 0: ocluido)\n",
                  nearRef, farRef);
      check(nearRef == 255 && farRef == 0,
            "a comparacao de profundidade e feita pelo hardware");
    }
    device->destroy(shadowPipeline);
  }
  if (compareSampler.valid())
    device->destroy(compareSampler);

  if (canReadDepth)
  {
    static float afterColour[atlasSize * atlasSize];
    const bool ok =
        device->readTexture(atlas, whole, {afterColour, sizeof(afterColour)});
    const float sample = afterColour[16 * atlasSize + 16];
    check(ok && sample > 0.19f && sample < 0.21f,
          "ler profundidade depois de ler cor (framebuffers de copia limpos)");
  }

  std::printf("\n  -- depth bias (glPolygonOffset) --\n");
  gpu::TextureDesc biasAtlasDesc;
  biasAtlasDesc.format = gpu::Format::Depth32Float;
  biasAtlasDesc.width = atlasSize;
  biasAtlasDesc.height = atlasSize;
  biasAtlasDesc.usage = gpu::TextureUsageRenderTarget |
                        gpu::TextureUsageSampled |
                        gpu::TextureUsageCopySource;
  biasAtlasDesc.debugName = "bias probe";
  const gpu::TextureHandle biasAtlas = device->createTexture(biasAtlasDesc);
  const float biasValues[4] = {0.0f, -8.0f, 8.0f, 64.0f};
  for (int probe = 0; probe < 4; ++probe) {
  gpu::PipelineDesc biasedDesc = depthPipelineDesc;
  biasedDesc.raster.depthBiasConstant = biasValues[probe];
  biasedDesc.raster.depthBiasSlope = 0.0f;
  biasedDesc.debugName = "biased depth";
  const gpu::PipelineHandle biasedPipeline =
      device->createPipeline(biasedDesc);
  if (probe == 0)
    check(biasedPipeline.valid(), "pipeline com depth bias constante");
  if (biasedPipeline.valid()) {
    gpu::RenderPassDesc biasPass;
    biasPass.colorCount = 0;
    biasPass.hasDepthStencil = true;
    biasPass.depthStencil.target.texture = biasAtlas;
    biasPass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
    biasPass.depthStencil.clearDepth = 1.0f;
    device->clearErrors();
    if (device->beginRenderPass(biasPass)) {
      gpu::Viewport viewport;
      viewport.width = static_cast<float>(atlasSize);
      viewport.height = static_cast<float>(atlasSize);
      device->setViewport(viewport);
      device->setPipeline(biasedPipeline);
      device->bindVertexBuffer(0, vertexBuffer, 0);
      gpu::Rect scissor;
      scissor.width = atlasSize;
      scissor.height = atlasSize;
      device->setScissor(scissor);
      device->draw(3, 1, 0);
      device->endRenderPass();
    }
    if (probe == 0) {
      gpu::GPUError e;
      while (device->getError(e))
        std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
    }
    static float biased[atlasSize * atlasSize];
    if (device->readTexture(biasAtlas, whole, {biased, sizeof(biased)})) {
      const float unbiasedDepth = (depths[0] + 1.0f) * 0.5f;
      const float got = biased[(atlasSize / 2) * atlasSize + atlasSize / 2];
      std::printf("        units=%+7.1f -> profundidade %.6f "
                  "(sem bias seria %.6f, delta %+.6f)\n",
                  static_cast<double>(biasValues[probe]),
                  static_cast<double>(got),
                  static_cast<double>(unbiasedDepth),
                  static_cast<double>(got - unbiasedDepth));
      if (probe == 3)
        check(got != unbiasedDepth,
              "o depth bias desloca mesmo a profundidade escrita");
    }
    device->destroy(biasedPipeline);
  }
  }

  std::printf("\n  -- render pass para a surface da janela --\n");
  {
    const std::string surfaceVS = glsl(
        "layout(location = 0) in vec3 p;\n"
        "void main() { gl_Position = vec4(p, 1.0); }\n", es);
    const std::string surfaceFS = glsl(
        "out vec4 c;\n"
        "void main() { c = vec4(0.9, 0.3, 0.1, 1.0); }\n", es);
    gpu::PipelineDesc surfaceDesc = samplePipelineDesc;
    surfaceDesc.vertex.source = view(surfaceVS);
    surfaceDesc.fragment.source = view(surfaceFS);
    surfaceDesc.depthStencil.depthTestEnabled = true;
    surfaceDesc.depthStencil.depthWriteEnabled = true;
    surfaceDesc.depthStencil.depthCompare = gpu::CompareOp::LessEqual;
    surfaceDesc.debugName = "surface pass";
    device->clearErrors();
    const gpu::PipelineHandle surfacePipeline =
        device->createPipeline(surfaceDesc);
    check(surfacePipeline.valid(), "pipeline para a surface");

    gpu::RenderPassDesc surfacePass;
    surfacePass.colorCount = 1;
    surfacePass.colors[0].surface = true;
    surfacePass.colors[0].loadOp = gpu::LoadOp::Clear;
    surfacePass.colors[0].clearColor[2] = 0.4f;
    surfacePass.colors[0].clearColor[3] = 1.0f;
    surfacePass.hasDepthStencil = true;
    surfacePass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
    surfacePass.depthStencil.clearDepth = 1.0f;
    device->clearErrors();
    const bool began = device->beginRenderPass(surfacePass);
    check(began, "beginRenderPass na surface COM profundidade");
    if (began && surfacePipeline.valid()) {
      gpu::Viewport viewport;
      viewport.width = 64.0f;
      viewport.height = 64.0f;
      bool ok = device->setViewport(viewport);
      ok = device->setPipeline(surfacePipeline) && ok;
      ok = device->bindVertexBuffer(0, vertexBuffer, 0) && ok;
      ok = device->draw(3) && ok;
      check(ok, "desenhar para a surface");
      device->endRenderPass();
    }
    check(device->pendingErrorCount() == 0, "surface sem erros");
    {
      gpu::GPUError e;
      while (device->getError(e))
        std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
    }

    gpu::RenderPassDesc mixedPass = surfacePass;
    mixedPass.depthStencil.target.texture = atlas;
    device->clearErrors();
    check(!device->beginRenderPass(mixedPass),
          "surface com textura de profundidade continua a ser rejeitada");
    device->clearErrors();

    if (surfacePipeline.valid())
      device->destroy(surfacePipeline);
  }

  std::printf("\n  -- feedback loop e mipmaps --\n");
  {
    // Bindar o proprio alvo da passada como textura amostrada e undefined
    // behaviour em GL/GLES. Tem de ser recusado pela camada.
    gpu::RenderPassDesc loopPass;
    loopPass.colorCount = 1;
    loopPass.colors[0].target.texture = colorTarget;
    loopPass.colors[0].loadOp = gpu::LoadOp::Clear;
    device->clearErrors();
    if (device->beginRenderPass(loopPass))
    {
      const bool rejected = !device->bindTexture(0, colorTarget, nearest);
      gpu::GPUError loopError;
      const bool reported = device->getError(loopError);
      device->endRenderPass();
      check(rejected && reported &&
                loopError.code == gpu::GPUErrorCode::InvalidArgument,
            "bindar o alvo da passada como textura e recusado (feedback loop)");
    }
    else
      check(false, "abrir passada para o teste de feedback loop");
    device->clearErrors();

    // Uma textura que nao e attachment continua a poder ser bindada.
    gpu::RenderPassDesc plainPass = loopPass;
    if (device->beginRenderPass(plainPass))
    {
      const bool ok = device->bindTexture(0, atlas, nearest);
      device->endRenderPass();
      check(ok, "textura que nao e attachment continua a poder ser bindada");
    }
    device->clearErrors();

    gpu::TextureDesc mippedDesc;
    mippedDesc.format = gpu::Format::RGBA8;
    mippedDesc.width = 32;
    mippedDesc.height = 32;
    mippedDesc.mipCount = 6;
    mippedDesc.usage =
        gpu::TextureUsageRenderTarget | gpu::TextureUsageSampled;
    const gpu::TextureHandle mipped = device->createTexture(mippedDesc);
    check(mipped.valid(), "criar textura com 6 niveis de mip");
    check(device->generateMipmaps(mipped),
          "generateMipmaps depois da criacao");
    device->clearErrors();

    // mipCount == 1: nao ha nada para gerar, tem de ser recusado.
    gpu::TextureDesc flatDesc = mippedDesc;
    flatDesc.mipCount = 1;
    const gpu::TextureHandle flat = device->createTexture(flatDesc);
    check(!device->generateMipmaps(flat),
          "generateMipmaps recusado com um so nivel");
    device->clearErrors();

    // Profundidade nao e color-renderable: glGenerateMipmap nao se aplica.
    check(!device->generateMipmaps(atlas),
          "generateMipmaps recusado numa textura de profundidade");
    device->clearErrors();

    if (device->beginRenderPass(plainPass))
    {
      check(!device->generateMipmaps(mipped),
            "generateMipmaps recusado dentro de uma passada");
      device->endRenderPass();
    }
    device->clearErrors();

    if (flat.valid())
      device->destroy(flat);
    if (mipped.valid())
      device->destroy(mipped);
  }

  std::printf("\n  -- MRT: dois alvos de cor numa passada --\n");
  {
    gpu::TextureDesc mrtDesc;
    mrtDesc.format = gpu::Format::RGBA8;
    mrtDesc.width = 16;
    mrtDesc.height = 16;
    mrtDesc.usage = gpu::TextureUsageRenderTarget | gpu::TextureUsageCopySource;
    const gpu::TextureHandle targetA = device->createTexture(mrtDesc);
    const gpu::TextureHandle targetB = device->createTexture(mrtDesc);
    check(targetA.valid() && targetB.valid(), "criar dois alvos de cor");

    // Cada saida escreve uma cor diferente: se os draw buffers estiverem mal
    // mapeados, as duas texturas ficam iguais ou uma fica vazia.
    const std::string mrtFS = glsl(
        "in vec2 uv;\n"
        "layout(location = 0) out vec4 outA;\n"
        "layout(location = 1) out vec4 outB;\n"
        "void main() {\n"
        "  outA = vec4(1.0, 0.0, 0.0, 1.0);\n"
        "  outB = vec4(0.0, 0.0, 1.0, 1.0);\n"
        "}\n",
        es);
    gpu::PipelineDesc mrtPipelineDesc = samplePipelineDesc;
    mrtPipelineDesc.fragment.source = view(mrtFS);
    mrtPipelineDesc.colorTargetCount = 2;
    mrtPipelineDesc.colorTargets[1] = mrtPipelineDesc.colorTargets[0];
    mrtPipelineDesc.debugName = "MRT";
    const gpu::PipelineHandle mrtPipeline =
        device->createPipeline(mrtPipelineDesc);
    check(mrtPipeline.valid(), "compilar shader com duas saidas de cor");
    {
      gpu::GPUError e;
      while (device->getError(e))
        std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
    }

    if (mrtPipeline.valid() && targetA.valid() && targetB.valid())
    {
      gpu::RenderPassDesc mrtPass;
      mrtPass.colorCount = 2;
      mrtPass.colors[0].target.texture = targetA;
      mrtPass.colors[0].loadOp = gpu::LoadOp::Clear;
      mrtPass.colors[1].target.texture = targetB;
      mrtPass.colors[1].loadOp = gpu::LoadOp::Clear;
      device->clearErrors();
      bool ok = device->beginRenderPass(mrtPass);
      check(ok, "beginRenderPass com dois alvos de cor");
      if (ok)
      {
        gpu::Viewport viewport;
        viewport.width = 16.0f;
        viewport.height = 16.0f;
        ok = device->setViewport(viewport) && ok;
        ok = device->setPipeline(mrtPipeline) && ok;
        ok = device->bindVertexBuffer(0, vertexBuffer, 0) && ok;
        ok = device->draw(3) && ok;
        device->endRenderPass();
        check(ok, "desenhar para dois alvos");
        check(device->pendingErrorCount() == 0, "MRT sem erros");
        {
          gpu::GPUError e;
          while (device->getError(e))
            std::printf("        erro: %s\n",
                        e.message ? e.message : "(sem texto)");
        }

        std::uint8_t pixelsA[16 * 16 * 4] = {};
        std::uint8_t pixelsB[16 * 16 * 4] = {};
        gpu::TextureRegion mrtRegion;
        mrtRegion.width = 16;
        mrtRegion.height = 16;
        const bool readA =
            device->readTexture(targetA, mrtRegion, {pixelsA, sizeof(pixelsA)});
        const bool readB =
            device->readTexture(targetB, mrtRegion, {pixelsB, sizeof(pixelsB)});
        const std::uint32_t centre = (8 * 16 + 8) * 4;
        std::printf("        alvo 0 = (%u,%u,%u)  alvo 1 = (%u,%u,%u)\n",
                    pixelsA[centre], pixelsA[centre + 1], pixelsA[centre + 2],
                    pixelsB[centre], pixelsB[centre + 1], pixelsB[centre + 2]);
        check(readA && readB && pixelsA[centre] > 200 &&
                  pixelsA[centre + 2] < 55,
              "location 0 escreveu vermelho no alvo 0");
        check(readA && readB && pixelsB[centre + 2] > 200 &&
                  pixelsB[centre] < 55,
              "location 1 escreveu azul no alvo 1");
      }
      device->clearErrors();
      device->destroy(mrtPipeline);
    }
    if (targetB.valid())
      device->destroy(targetB);
    if (targetA.valid())
      device->destroy(targetA);
  }

  std::printf("\n  -- reflection do pipeline --\n");
  if (samplePipeline.valid())
  {
    gpu::PipelineReflection reflection;
    check(device->reflectPipeline(samplePipeline, reflection),
          "reflectPipeline de um pipeline valido");
    bool foundSampler = false;
    for (std::uint32_t index = 0; index < reflection.resourceCount; ++index)
    {
      const gpu::ShaderResource &resource = reflection.resources[index];
      const char *kindName =
          resource.type == gpu::ShaderResourceType::Sampler ? "sampler"
          : resource.type == gpu::ShaderResourceType::UniformBuffer
              ? "uniform block"
          : resource.type == gpu::ShaderResourceType::StorageBuffer
              ? "storage buffer"
              : "storage texture";
      std::printf("        %-14s %-18s slot %u", kindName, resource.name,
                  resource.slot);
      if (resource.blockSize != 0)
        std::printf("  (%u bytes)", resource.blockSize);
      std::printf("\n");
      if (resource.type == gpu::ShaderResourceType::Sampler &&
          std::strcmp(resource.name, "shadowAtlas") == 0)
        foundSampler = true;
    }
    check(foundSampler,
          "reflection encontra o sampler 'shadowAtlas' pelo nome");
    check(!reflection.truncated, "reflection nao foi truncada");

    gpu::PipelineReflection empty;
    device->clearErrors();
    check(!device->reflectPipeline(gpu::PipelineHandle(), empty) &&
              empty.resourceCount == 0,
          "reflectPipeline recusa um handle invalido");
    device->clearErrors();

    // Um pipeline como o de um material a serio: um uniform block e dois
    // samplers. E este o caso que interessa a quem liga recursos por nome.
    const std::string materialFS = glsl(
        "uniform sampler2D albedo;\n"
        "uniform sampler2D normalMap;\n"
        "layout(std140) uniform Material {\n"
        "  vec4 tint;\n"
        "  vec4 parameters;\n"
        "} material;\n"
        "in vec2 uv;\n"
        "out vec4 c;\n"
        "void main() {\n"
        "  c = texture(albedo, uv) * material.tint +\n"
        "      texture(normalMap, uv) * material.parameters;\n"
        "}\n",
        es);
    gpu::PipelineDesc materialDesc = samplePipelineDesc;
    materialDesc.fragment.source = view(materialFS);
    materialDesc.debugName = "material";
    const gpu::PipelineHandle materialPipeline =
        device->createPipeline(materialDesc);
    check(materialPipeline.valid(), "compilar pipeline com UBO + 2 samplers");
    {
      gpu::GPUError e;
      while (device->getError(e))
        std::printf("        erro: %s\n", e.message ? e.message : "(sem texto)");
    }
    if (materialPipeline.valid())
    {
      gpu::PipelineReflection material;
      check(device->reflectPipeline(materialPipeline, material),
            "reflectPipeline do pipeline de material");
      std::uint32_t samplers = 0;
      std::uint32_t blocks = 0;
      bool distinctUnits = true;
      bool blockHasSize = false;
      std::uint32_t seenUnits = 0;
      for (std::uint32_t index = 0; index < material.resourceCount; ++index)
      {
        const gpu::ShaderResource &resource = material.resources[index];
        std::printf("        %-14s %-18s slot %u",
                    resource.type == gpu::ShaderResourceType::Sampler
                        ? "sampler"
                        : "uniform block",
                    resource.name, resource.slot);
        if (resource.blockSize != 0)
          std::printf("  (%u bytes)", resource.blockSize);
        std::printf("\n");
        if (resource.type == gpu::ShaderResourceType::Sampler)
        {
          ++samplers;
          const std::uint32_t bit = 1u << (resource.slot & 31u);
          if ((seenUnits & bit) != 0)
            distinctUnits = false;
          seenUnits |= bit;
        }
        else if (resource.type == gpu::ShaderResourceType::UniformBuffer)
        {
          ++blocks;
          // std140 com dois vec4 sao 32 bytes.
          blockHasSize = resource.blockSize >= 32;
        }
      }
      check(samplers == 2, "reflection lista os dois samplers");
      check(distinctUnits,
            "cada sampler tem a sua unidade (nao colidem no slot 0)");
      check(blocks == 1, "reflection lista o uniform block");
      check(blockHasSize, "o uniform block reporta o tamanho em bytes");
      device->destroy(materialPipeline);
    }
    device->clearErrors();
  }

  if (biasAtlas.valid())
    device->destroy(biasAtlas);
  if (grid.valid())
    device->destroy(grid);
  device->destroy(samplePipeline);
  device->destroy(nearest);
  device->destroy(colorTarget);
  device->destroy(depthPipeline);
  device->destroy(vertexBuffer);
  device->destroy(atlas);

  std::printf("\n  render path %s: %d/%d verificacoes passaram\n", label,
              checks - failures, checks);
  assert(failures == 0);
}
