#ifndef GPU_SHADOW_DEMO_COMMON_H
#define GPU_SHADOW_DEMO_COMMON_H

#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"

#include <cstdint>
#include <string>
#include <vector>

namespace demo
{

  struct Vec3
  {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
  };

  Vec3 add(const Vec3 &a, const Vec3 &b);
  Vec3 subtract(const Vec3 &a, const Vec3 &b);
  Vec3 scale(const Vec3 &v, float s);
  Vec3 cross(const Vec3 &a, const Vec3 &b);
  float dot(const Vec3 &a, const Vec3 &b);
  float length(const Vec3 &v);
  Vec3 normalize(const Vec3 &v);

  struct Mat4
  {
    float m[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  };

  Mat4 multiply(const Mat4 &a, const Mat4 &b);
  Mat4 perspective(float fovYRadians, float aspect, float nearPlane,
                   float farPlane);
  Mat4 orthographic(float left, float right, float bottom, float top,
                    float nearPlane, float farPlane);
  Mat4 lookAt(const Vec3 &eye, const Vec3 &target, const Vec3 &up);
  Vec3 transformPoint(const Mat4 &matrix, const Vec3 &point);

  void frustumCorners(const Vec3 &eye, const Vec3 &forward, const Vec3 &up,
                      float fovYRadians, float aspect, float nearPlane,
                      float farPlane, Vec3 *outCorners);

  struct Camera
  {
    Vec3 eye{0.0f, 6.0f, 12.0f};
    Vec3 target{0.0f, 1.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fovYRadians = 1.0472f;
    float nearPlane = 0.5f;
    float farPlane = 60.0f;

    Mat4 view() const;
    Mat4 projection(float aspect) const;
    Vec3 forward() const;
  };

  struct SceneObject
  {
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    Vec3 centre;
    float radius = 0.0f;
  };

  struct Scene
  {
    gpu::BufferHandle vertices;
    std::uint32_t vertexCount = 0;
    SceneObject objects[8];
    std::uint32_t objectCount = 0;
  };

  Scene createScene(gpu::Device &device);
  void destroyScene(gpu::Device &device, Scene &scene);

  std::string preamble(bool es);
  gpu::DataView view(const std::string &source);

  bool reportErrors(gpu::Device &device, const char *stage);

  struct PackedRect
  {
    int id = 0;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool packed = false;
  };

  class RectPacker
  {
  public:
    void clear();
    void addRect(int id, int width, int height);
    bool pack(int maxSide);

    const std::vector<PackedRect> &rects() const { return mRects; }
    int width() const { return mWidth; }
    int height() const { return mHeight; }

  private:
    std::vector<PackedRect> mRects;
    int mWidth = 0;
    int mHeight = 0;
  };

  struct Options
  {
    bool preferES = false;
    bool forceGL = false;
    int onlyLight = -1;
    bool capture = false;
    std::uint32_t frames = 0;
    std::string capturePath;
  };

  Options parseOptions(int argc, char **argv, const char *defaultCapture);
  bool writePPM(const char *path, const std::uint8_t *rgba, std::uint32_t width,
                std::uint32_t height);
  bool describeCapture(const std::uint8_t *rgba, std::uint32_t width,
                       std::uint32_t height);

  struct Host;

  Host *createHost(const char *title, std::uint32_t width,
                   std::uint32_t height, bool preferES, bool hidden = false);
  void destroyHost(Host *host);
  gpu::Device *device(Host &host);
  bool isES(const Host &host);
  bool pumpEvents(Host &host);
  void drawableSize(Host &host, std::uint32_t &width, std::uint32_t &height);

} // namespace demo

#endif
