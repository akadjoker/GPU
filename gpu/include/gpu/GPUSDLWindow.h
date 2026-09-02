#ifndef GPU_SDL_WINDOW_H
#define GPU_SDL_WINDOW_H

#include "GPUBackend.h"

#include <cstdint>

struct SDL_Window;

namespace gpu
{

  /** @brief Parameters used by SDLWindow::create. */
  struct SDLWindowDesc
  {
    /** @brief Window title. */
    const char *title = "gpu";
    /** @brief SDL window position on the X axis; defaults to centered. */
    int x = 0x2FFF0000;
    /** @brief SDL window position on the Y axis; defaults to centered. */
    int y = 0x2FFF0000;
    /** @brief Client width in window units. */
    std::uint32_t width = 1280;
    /** @brief Client height in window units. */
    std::uint32_t height = 720;
    /** @brief Additional SDL_WINDOW_* flags; the backend flag is added automatically. */
    std::uint32_t flags = 0;
    /** @brief Backend the window must be able to present for. */
    Backend backend = Backend::OpenGL;
    /** @brief Color format reported through surfaceDesc(). */
    Format format = Format::RGBA8;
    /** @brief Synchronise presentation with the display refresh. */
    bool vsync = true;
    /** @brief Request a debug-capable context when the backend supports one. */
    bool debugContext = false;
    /** @brief Context major version; 0 selects the backend default. */
    int contextMajor = 0;
    /** @brief Context minor version; 0 selects the backend default. */
    int contextMinor = 0;
    /** @brief Depth buffer bits requested for the default framebuffer. */
    int depthBits = 24;
    /** @brief Stencil buffer bits requested for the default framebuffer. */
    int stencilBits = 8;
    /** @brief Multisample count for the default framebuffer; 0 disables it. */
    int multisampleSamples = 0;
  };

  /**
   * @brief SDL2 window that owns the presentation surface of one backend.
   *
   * The window hides every backend-specific SDL call: it creates the GL
   * context the selected backend needs and hands it to createDevice through
   * surfaceDesc(). Applications only deal with the SDL_Window for input and
   * window management. Vulkan is not wired in yet - this commit of gpu has
   * no Vulkan backend to hand a surface to.
   */
  class SDLWindow
  {
  public:
    SDLWindow();
    ~SDLWindow();

    SDLWindow(const SDLWindow &) = delete;
    SDLWindow &operator=(const SDLWindow &) = delete;

    /** @brief Create the window and the backend surface; initialises SDL video when needed. */
    bool create(const SDLWindowDesc &desc);
    /** @brief Destroy the surface and the window. */
    void destroy();

    /** @brief True while a window exists. */
    bool valid() const;
    /** @brief Underlying SDL window, or `nullptr`. */
    SDL_Window *handle() const;
    /** @brief Backend the window was created for. */
    Backend backend() const;
    /** @brief Surface parameters to pass to createDevice; refreshed with the drawable size. */
    SurfaceDesc surfaceDesc() const;
    /** @brief Current drawable size in pixels. */
    void drawableSize(std::uint32_t &width, std::uint32_t &height) const;

    /** @brief Change the presentation synchronisation; false when the backend does not expose it. */
    bool setVSync(bool enabled);
    /** @brief Last requested synchronisation state. */
    bool vsync() const;

    /** @brief Description of the last failure, or an empty string. */
    const char *lastError() const;

    /** @brief True when this build can create windows for the backend. */
    static bool supportsBackend(Backend backend);

  private:
    struct Impl;
    Impl *mImpl;
  };

} // namespace gpu

#endif
