#include "ShadowDemoCommon.h"

#if defined(GPU_DEMO_HAS_OPENGL)
#include "SDLGLSurface.h"
#endif
#if defined(GPU_DEMO_HAS_OPENGLES)
#include "SDLGLESSurface.h"
#endif

#define STB_RECT_PACK_IMPLEMENTATION
#include "vendor/stb_rect_pack.h"

#include <SDL2/SDL.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstdlib>
#include <vector>

namespace demo
{

  Vec3 add(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
  Vec3 subtract(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
  Vec3 scale(const Vec3 &v, float s) { return {v.x * s, v.y * s, v.z * s}; }
  float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
  float length(const Vec3 &v) { return std::sqrt(dot(v, v)); }

  Vec3 cross(const Vec3 &a, const Vec3 &b)
  {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  }

  Vec3 normalize(const Vec3 &v)
  {
    const float len = length(v);
    return len > 0.0f ? scale(v, 1.0f / len) : v;
  }

  Mat4 multiply(const Mat4 &a, const Mat4 &b)
  {
    Mat4 result;
    for (int column = 0; column < 4; ++column)
      for (int row = 0; row < 4; ++row)
      {
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k)
          sum += a.m[k * 4 + row] * b.m[column * 4 + k];
        result.m[column * 4 + row] = sum;
      }
    return result;
  }

  Mat4 perspective(float fovYRadians, float aspect, float nearPlane,
                   float farPlane)
  {
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    Mat4 result;
    for (int i = 0; i < 16; ++i)
      result.m[i] = 0.0f;
    result.m[0] = f / aspect;
    result.m[5] = f;
    result.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    result.m[11] = -1.0f;
    result.m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    return result;
  }

  Mat4 orthographic(float left, float right, float bottom, float top,
                    float nearPlane, float farPlane)
  {
    Mat4 result;
    result.m[0] = 2.0f / (right - left);
    result.m[5] = 2.0f / (top - bottom);
    result.m[10] = -2.0f / (farPlane - nearPlane);
    result.m[12] = -(right + left) / (right - left);
    result.m[13] = -(top + bottom) / (top - bottom);
    result.m[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    result.m[15] = 1.0f;
    return result;
  }

  Mat4 lookAt(const Vec3 &eye, const Vec3 &target, const Vec3 &up)
  {
    const Vec3 f = normalize(subtract(target, eye));
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 result;
    result.m[0] = s.x;  result.m[4] = s.y;  result.m[8] = s.z;
    result.m[1] = u.x;  result.m[5] = u.y;  result.m[9] = u.z;
    result.m[2] = -f.x; result.m[6] = -f.y; result.m[10] = -f.z;
    result.m[12] = -dot(s, eye);
    result.m[13] = -dot(u, eye);
    result.m[14] = dot(f, eye);
    result.m[15] = 1.0f;
    return result;
  }

  Vec3 transformPoint(const Mat4 &matrix, const Vec3 &point)
  {
    const float x = matrix.m[0] * point.x + matrix.m[4] * point.y +
                    matrix.m[8] * point.z + matrix.m[12];
    const float y = matrix.m[1] * point.x + matrix.m[5] * point.y +
                    matrix.m[9] * point.z + matrix.m[13];
    const float z = matrix.m[2] * point.x + matrix.m[6] * point.y +
                    matrix.m[10] * point.z + matrix.m[14];
    return {x, y, z};
  }

  void frustumCorners(const Vec3 &eye, const Vec3 &forward, const Vec3 &up,
                      float fovYRadians, float aspect, float nearPlane,
                      float farPlane, Vec3 *outCorners)
  {
    const Vec3 f = normalize(forward);
    const Vec3 r = normalize(cross(f, up));
    const Vec3 u = cross(r, f);
    const float planes[2] = {nearPlane, farPlane};
    int index = 0;
    for (int p = 0; p < 2; ++p)
    {
      const float halfHeight = std::tan(fovYRadians * 0.5f) * planes[p];
      const float halfWidth = halfHeight * aspect;
      const Vec3 centre = add(eye, scale(f, planes[p]));
      for (int corner = 0; corner < 4; ++corner)
      {
        const float sx = (corner & 1) ? 1.0f : -1.0f;
        const float sy = (corner & 2) ? 1.0f : -1.0f;
        outCorners[index++] =
            add(centre, add(scale(r, sx * halfWidth), scale(u, sy * halfHeight)));
      }
    }
  }

  Mat4 Camera::view() const { return lookAt(eye, target, up); }

  Mat4 Camera::projection(float aspect) const
  {
    return perspective(fovYRadians, aspect, nearPlane, farPlane);
  }

  Vec3 Camera::forward() const { return normalize(subtract(target, eye)); }

  namespace
  {

    void pushQuad(std::vector<float> &out, const Vec3 &origin, const Vec3 &edgeA,
                  const Vec3 &edgeB, const Vec3 &normal)
    {
      const Vec3 a = origin;
      const Vec3 b = add(origin, edgeA);
      const Vec3 c = add(add(origin, edgeA), edgeB);
      const Vec3 d = add(origin, edgeB);
      const Vec3 corners[6] = {a, b, c, a, c, d};
      for (const Vec3 &corner : corners)
      {
        out.push_back(corner.x);
        out.push_back(corner.y);
        out.push_back(corner.z);
        out.push_back(normal.x);
        out.push_back(normal.y);
        out.push_back(normal.z);
      }
    }

    void pushBox(std::vector<float> &out, const Vec3 &centre, const Vec3 &half)
    {
      const Vec3 low = subtract(centre, half);
      const Vec3 sx{half.x * 2.0f, 0.0f, 0.0f};
      const Vec3 sy{0.0f, half.y * 2.0f, 0.0f};
      const Vec3 sz{0.0f, 0.0f, half.z * 2.0f};
      pushQuad(out, add(low, sz), sx, sy, {0.0f, 0.0f, 1.0f});
      pushQuad(out, low, sy, sx, {0.0f, 0.0f, -1.0f});
      pushQuad(out, add(low, sx), sy, sz, {1.0f, 0.0f, 0.0f});
      pushQuad(out, low, sz, sy, {-1.0f, 0.0f, 0.0f});
      pushQuad(out, add(low, sy), sz, sx, {0.0f, 1.0f, 0.0f});
      pushQuad(out, low, sx, sz, {0.0f, -1.0f, 0.0f});
    }

  } // namespace

  Scene createScene(gpu::Device &device)
  {
    std::vector<float> data;
    Scene scene;

    const auto beginObject = [&](const Vec3 &centre, float radius) {
      SceneObject &object = scene.objects[scene.objectCount++];
      object.firstVertex = static_cast<std::uint32_t>(data.size() / 6);
      object.centre = centre;
      object.radius = radius;
    };
    const auto endObject = [&]() {
      SceneObject &object = scene.objects[scene.objectCount - 1];
      object.vertexCount =
          static_cast<std::uint32_t>(data.size() / 6) - object.firstVertex;
    };

    beginObject({0.0f, 0.0f, 0.0f}, 28.3f);
    pushQuad(data, {-20.0f, 0.0f, -20.0f}, {0.0f, 0.0f, 40.0f},
             {40.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    endObject();

    const Vec3 centres[4] = {{-3.0f, 1.5f, -1.0f},
                             {2.5f, 1.0f, 1.5f},
                             {0.0f, 2.5f, -4.0f},
                             {5.0f, 0.6f, -3.0f}};
    const Vec3 halves[4] = {{1.0f, 1.5f, 1.0f},
                            {1.2f, 1.0f, 1.2f},
                            {0.8f, 2.5f, 0.8f},
                            {0.6f, 0.6f, 2.5f}};
    for (int index = 0; index < 4; ++index)
    {
      beginObject(centres[index], length(halves[index]));
      pushBox(data, centres[index], halves[index]);
      endObject();
    }

    std::uint32_t mismatched = 0;
    for (std::size_t base = 0; base + 17 < data.size(); base += 18)
    {
      const Vec3 a{data[base + 0], data[base + 1], data[base + 2]};
      const Vec3 b{data[base + 6], data[base + 7], data[base + 8]};
      const Vec3 c{data[base + 12], data[base + 13], data[base + 14]};
      const Vec3 stored{data[base + 3], data[base + 4], data[base + 5]};
      const Vec3 geometric = normalize(cross(subtract(b, a), subtract(c, b)));
      if (dot(geometric, stored) < 0.9f)
        ++mismatched;
    }
    std::printf("cena: %u triangulos, %u com winding errado\n",
                static_cast<std::uint32_t>(data.size() / 18), mismatched);

    gpu::BufferDesc desc;
    desc.size = data.size() * sizeof(float);
    desc.usage = gpu::BufferUsageVertex;
    desc.initialData = {data.data(), desc.size};
    desc.debugName = "shadow demo scene";
    scene.vertices = device.createBuffer(desc);
    scene.vertexCount = static_cast<std::uint32_t>(data.size() / 6);
    return scene;
  }

  void destroyScene(gpu::Device &device, Scene &scene)
  {
    if (scene.vertices.valid())
      device.destroy(scene.vertices);
    scene = Scene{};
  }

  std::string preamble(bool es)
  {
    if (es)
      return "#version 300 es\nprecision highp float;\nprecision highp int;\n"
             "precision highp sampler2D;\nprecision highp sampler2DShadow;\n"
             "precision highp sampler2DArrayShadow;\n"
             "precision highp samplerCubeShadow;\n";
    return "#version 330 core\n";
  }

  gpu::DataView view(const std::string &source)
  {
    return {source.data(), source.size()};
  }

  bool reportErrors(gpu::Device &device, const char *stage)
  {
    bool found = false;
    gpu::GPUError error;
    while (device.getError(error))
    {
      std::fprintf(stderr, "[%s] GPU error %u op %u: %s\n", stage,
                   static_cast<unsigned>(error.code),
                   static_cast<unsigned>(error.operation),
                   error.message ? error.message : "no diagnostic");
      found = true;
    }
    return found;
  }

  void RectPacker::clear() { mRects.clear(); }

  void RectPacker::addRect(int id, int width, int height)
  {
    PackedRect rect;
    rect.id = id;
    rect.w = width;
    rect.h = height;
    mRects.push_back(rect);
    mWidth = std::max(mWidth, width);
    mHeight = std::max(mHeight, height);
  }

  bool RectPacker::pack(int maxSide)
  {
    std::vector<stbrp_rect> raw(mRects.size());
    for (std::size_t index = 0; index < mRects.size(); ++index)
    {
      raw[index] = stbrp_rect{};
      raw[index].id = mRects[index].id;
      raw[index].w = static_cast<stbrp_coord>(mRects[index].w);
      raw[index].h = static_cast<stbrp_coord>(mRects[index].h);
    }

    std::vector<stbrp_node> nodes;
    while (mWidth <= maxSide && mHeight <= maxSide)
    {
      if (static_cast<int>(nodes.size()) < mWidth)
        nodes.resize(static_cast<std::size_t>(mWidth));
      stbrp_context context = {};
      stbrp_init_target(&context, mWidth, mHeight, nodes.data(),
                        static_cast<int>(nodes.size()));
      if (stbrp_pack_rects(&context, raw.data(),
                           static_cast<int>(raw.size())))
      {
        for (std::size_t index = 0; index < mRects.size(); ++index)
        {
          mRects[index].x = raw[index].x;
          mRects[index].y = raw[index].y;
          mRects[index].packed = raw[index].was_packed != 0;
        }
        return true;
      }
      for (std::size_t index = 0; index < raw.size(); ++index)
      {
        raw[index].x = 0;
        raw[index].y = 0;
        raw[index].was_packed = 0;
      }
      if (mHeight < mWidth)
        mHeight *= 2;
      else
        mWidth *= 2;
    }
    mWidth = 0;
    mHeight = 0;
    return false;
  }

  Options parseOptions(int argc, char **argv, const char *defaultCapture)
  {
    Options options;
    options.capturePath = defaultCapture;
    for (int index = 1; index < argc; ++index)
    {
      const std::string argument = argv[index];
      if (argument == "gles" || argument == "--es" ||
          argument == "--gles")
        options.preferES = true;
      else if (argument == "--gl")
        options.forceGL = true;
      else if (argument == "--only" && index + 1 < argc)
        options.onlyLight = std::atoi(argv[++index]);
      else if (argument == "--capture")
        options.capture = true;
      else if (argument == "--frames" && index + 1 < argc)
        options.frames = static_cast<std::uint32_t>(std::atoi(argv[++index]));
      else if (argument == "--out" && index + 1 < argc)
        options.capturePath = argv[++index];
    }
    if (options.capture && options.frames == 0)
      options.frames = 3;
    return options;
  }

  bool writePPM(const char *path, const std::uint8_t *rgba, std::uint32_t width,
                std::uint32_t height)
  {
    std::FILE *file = std::fopen(path, "wb");
    if (!file)
      return false;
    std::fprintf(file, "P6\n%u %u\n255\n", width, height);
    for (std::uint32_t row = 0; row < height; ++row)
    {
      const std::uint8_t *line = rgba + (height - 1 - row) * width * 4;
      for (std::uint32_t column = 0; column < width; ++column)
        std::fwrite(line + column * 4, 1, 3, file);
    }
    std::fclose(file);
    return true;
  }

  bool describeCapture(const std::uint8_t *rgba, std::uint32_t width,
                       std::uint32_t height)
  {
    const std::uint32_t pixels = width * height;
    std::vector<std::uint8_t> luminance(pixels);
    std::uint64_t total = 0;
    for (std::uint32_t index = 0; index < pixels; ++index)
    {
      const std::uint32_t value =
          (rgba[index * 4] * 3 + rgba[index * 4 + 1] * 6 +
           rgba[index * 4 + 2]) / 10;
      luminance[index] = static_cast<std::uint8_t>(value);
      total += value;
    }
    std::vector<std::uint8_t> sorted = luminance;
    std::sort(sorted.begin(), sorted.end());
    const std::uint8_t p05 = sorted[pixels / 20];
    const std::uint8_t p50 = sorted[pixels / 2];
    const std::uint8_t p95 = sorted[pixels - 1 - pixels / 20];
    std::printf("  luminancia p05=%u p50=%u p95=%u (media %.1f)\n", p05, p50,
                p95, static_cast<double>(total) / pixels);

    std::uint32_t edges = 0;
    for (std::uint32_t row = 1; row < height; ++row)
      for (std::uint32_t column = 1; column < width; ++column)
      {
        const int here = luminance[row * width + column];
        const int left = luminance[row * width + column - 1];
        const int above = luminance[(row - 1) * width + column];
        if (std::abs(here - left) > 18 || std::abs(here - above) > 18)
          ++edges;
      }
    const double edgeRatio = static_cast<double>(edges) / pixels;
    const bool hasContrast = (p95 - p05) > 45 && edgeRatio > 0.005;
    std::printf("  contraste p95-p05=%d | fronteiras nitidas %.2f%%\n",
                p95 - p05, edgeRatio * 100.0);
    std::printf("  %s\n", hasContrast
                               ? "zonas iluminadas E zonas em sombra, com "
                                 "fronteiras definidas"
                               : "SEM contraste util: nao ha sombra visivel");
    return hasContrast;
  }

  struct Host
  {
#if defined(GPU_DEMO_HAS_OPENGL)
    SDLGLSurface gl;
#endif
#if defined(GPU_DEMO_HAS_OPENGLES)
    SDLGLESSurface gles;
#endif
    gpu::Device *deviceHandle = nullptr;
    bool es = false;
    bool running = true;
  };

  Host *createHost(const char *title, std::uint32_t width,
                   std::uint32_t height, bool preferES, bool hidden)
  {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
      std::fprintf(stderr, "SDL_Init falhou: %s\n", SDL_GetError());
      return nullptr;
    }
    Host *host = new Host();
    gpu::DeviceDesc desc;
    desc.profile = gpu::RendererProfile::Portable;
    desc.surface.width = width;
    desc.surface.height = height;
    gpu::GPUError creation;

#if defined(GPU_DEMO_HAS_OPENGLES)
#if defined(GPU_DEMO_HAS_OPENGL)
    const bool useES = preferES;
#else
    const bool useES = true;
#endif
    if (useES && !createSDLGLESSurface(host->gles, title, width, height))
      std::fprintf(stderr, "surface GLES falhou: %s\n", SDL_GetError());
    else if (useES)
    {
      if (hidden)
        SDL_HideWindow(host->gles.window);
      host->es = true;
      desc.backend = gpu::Backend::OpenGLES;
      desc.surface.nativeHandle = &host->gles.gpuSurface;
      host->deviceHandle = gpu::createDevice(desc, &creation);
      if (host->deviceHandle)
        return host;
      std::fprintf(stderr, "device GLES falhou: %s\n",
                   creation.message ? creation.message : "sem diagnostico");
      destroySDLGLESSurface(host->gles);
      host->es = false;
    }
#else
    (void)preferES;
#endif

#if defined(GPU_DEMO_HAS_OPENGL)
    if (createSDLGLSurface(host->gl, title, width, height))
    {
      if (hidden)
        SDL_HideWindow(host->gl.window);
      desc.backend = gpu::Backend::OpenGL;
      desc.surface.nativeHandle = &host->gl.gpuSurface;
      host->deviceHandle = gpu::createDevice(desc, &creation);
      if (host->deviceHandle)
        return host;
      destroySDLGLSurface(host->gl);
    }
#endif

    std::fprintf(stderr, "sem device utilizavel: %s\n",
                 creation.message ? creation.message
                                  : "nenhuma surface foi criada");
    delete host;
    SDL_Quit();
    return nullptr;
  }

  void destroyHost(Host *host)
  {
    if (!host)
      return;
    if (host->deviceHandle)
      gpu::destroyDevice(host->deviceHandle);
#if defined(GPU_DEMO_HAS_OPENGLES)
    if (host->es)
      destroySDLGLESSurface(host->gles);
#endif
#if defined(GPU_DEMO_HAS_OPENGL)
    if (!host->es)
      destroySDLGLSurface(host->gl);
#endif
    delete host;
    SDL_Quit();
  }

  gpu::Device *device(Host &host) { return host.deviceHandle; }
  bool isES(const Host &host) { return host.es; }

  bool pumpEvents(Host &host)
  {
#if defined(__EMSCRIPTEN__)
    emscripten_sleep(0);
#endif
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_QUIT)
        host.running = false;
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
        host.running = false;
    }
    return host.running;
  }

  void drawableSize(Host &host, std::uint32_t &width, std::uint32_t &height)
  {
    int w = 0;
    int h = 0;
#if defined(GPU_DEMO_HAS_OPENGLES)
    if (host.es)
      SDL_GL_GetDrawableSize(host.gles.window, &w, &h);
#endif
#if defined(GPU_DEMO_HAS_OPENGL)
    if (!host.es)
      SDL_GL_GetDrawableSize(host.gl.window, &w, &h);
#endif
    width = w > 0 ? static_cast<std::uint32_t>(w) : 1u;
    height = h > 0 ? static_cast<std::uint32_t>(h) : 1u;
  }

} // namespace demo
