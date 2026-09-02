#include "gpu/GPUSDLWindow.h"

#include <SDL2/SDL.h>

#include <cstdio>

#if defined(GPU_HAS_OPENGL) || defined(GPU_HAS_OPENGLES)
#include "backends/gl/GLSurface.h"
#endif

namespace gpu
{

  struct SDLWindow::Impl
  {
    SDL_Window *window = nullptr;
    Backend backend = Backend::Null;
    Format format = Format::RGBA8;
    bool vsync = true;
    bool ownsVideo = false;
    char error[256] = {};

    void setError(const char *prefix)
    {
      const char *detail = SDL_GetError();
      std::snprintf(error, sizeof(error), "%s%s%s", prefix,
                    detail && detail[0] ? ": " : "", detail ? detail : "");
    }

    void setMessage(const char *message)
    {
      std::snprintf(error, sizeof(error), "%s", message);
    }

#if defined(GPU_HAS_OPENGL) || defined(GPU_HAS_OPENGLES)
    SDL_GLContext context = nullptr;
    GLSurface glSurface;

    static bool glMakeCurrent(void *userData)
    {
      Impl *impl = static_cast<Impl *>(userData);
      return SDL_GL_MakeCurrent(impl->window, impl->context) == 0;
    }

    static void *glGetProcAddress(void *, const char *name)
    {
      return SDL_GL_GetProcAddress(name);
    }

    static void glPresent(void *userData)
    {
      SDL_GL_SwapWindow(static_cast<Impl *>(userData)->window);
    }

    static void glDrawableSize(void *userData, std::uint32_t &width, std::uint32_t &height)
    {
      int drawableWidth = 0;
      int drawableHeight = 0;
      SDL_GL_GetDrawableSize(static_cast<Impl *>(userData)->window, &drawableWidth,
                             &drawableHeight);
      width = drawableWidth > 0 ? static_cast<std::uint32_t>(drawableWidth) : 0;
      height = drawableHeight > 0 ? static_cast<std::uint32_t>(drawableHeight) : 0;
    }
#endif
  };

  namespace
  {

    bool usesGLContext(Backend backend)
    {
      return backend == Backend::OpenGL || backend == Backend::OpenGLES;
    }

#if defined(GPU_HAS_OPENGL) || defined(GPU_HAS_OPENGLES)
    void applyGLAttributes(const SDLWindowDesc &desc)
    {
      SDL_GL_ResetAttributes();
      const bool es = desc.backend == Backend::OpenGLES;
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                          es ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,
                          desc.contextMajor > 0 ? desc.contextMajor : 3);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,
                          desc.contextMajor > 0 ? desc.contextMinor : (es ? 0 : 3));
      SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
      SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, desc.depthBits);
      SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, desc.stencilBits);
      SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE,
                          desc.format == Format::RGBA8Srgb ? 1 : 0);
      if (desc.multisampleSamples > 0)
      {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, desc.multisampleSamples);
      }
      if (desc.debugContext)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }
#endif

  } // namespace

  SDLWindow::SDLWindow() : mImpl(new Impl()) {}

  SDLWindow::~SDLWindow()
  {
    destroy();
    delete mImpl;
  }

  bool SDLWindow::supportsBackend(Backend backend)
  {
    switch (backend)
    {
    case Backend::Null:
      return true;
    case Backend::OpenGL:
#if defined(GPU_HAS_OPENGL)
      return true;
#else
      return false;
#endif
    case Backend::OpenGLES:
#if defined(GPU_HAS_OPENGLES)
      return true;
#else
      return false;
#endif
    default:
      return false;
    }
  }

  bool SDLWindow::create(const SDLWindowDesc &desc)
  {
    destroy();

    if (!supportsBackend(desc.backend))
    {
      mImpl->setMessage("SDLWindow: backend not available in this build");
      return false;
    }

    if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
    {
      if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
      {
        mImpl->setError("SDL_InitSubSystem(SDL_INIT_VIDEO) failed");
        return false;
      }
      mImpl->ownsVideo = true;
    }

    std::uint32_t flags = desc.flags;
    if (usesGLContext(desc.backend))
      flags |= SDL_WINDOW_OPENGL;

#if defined(GPU_HAS_OPENGL) || defined(GPU_HAS_OPENGLES)
    if (usesGLContext(desc.backend))
      applyGLAttributes(desc);
#endif

    mImpl->window = SDL_CreateWindow(desc.title, desc.x, desc.y, static_cast<int>(desc.width),
                                     static_cast<int>(desc.height), flags);
    if (!mImpl->window)
    {
      mImpl->setError("SDL_CreateWindow failed");
      destroy();
      return false;
    }

    mImpl->backend = desc.backend;
    mImpl->format = desc.format;
    mImpl->vsync = desc.vsync;

#if defined(GPU_HAS_OPENGL) || defined(GPU_HAS_OPENGLES)
    if (usesGLContext(desc.backend))
    {
      mImpl->context = SDL_GL_CreateContext(mImpl->window);
      if (!mImpl->context)
      {
        mImpl->setError("SDL_GL_CreateContext failed");
        destroy();
        return false;
      }
      SDL_GL_MakeCurrent(mImpl->window, mImpl->context);
      SDL_GL_SetSwapInterval(desc.vsync ? 1 : 0);
      mImpl->glSurface.userData = mImpl;
      mImpl->glSurface.makeCurrent = &Impl::glMakeCurrent;
      mImpl->glSurface.getProcAddress = &Impl::glGetProcAddress;
      mImpl->glSurface.present = &Impl::glPresent;
      mImpl->glSurface.drawableSize = &Impl::glDrawableSize;
    }
#endif

    mImpl->error[0] = '\0';
    return true;
  }

  void SDLWindow::destroy()
  {
#if defined(GPU_HAS_OPENGL) || defined(GPU_HAS_OPENGLES)
    if (mImpl->context)
    {
      SDL_GL_DeleteContext(mImpl->context);
      mImpl->context = nullptr;
    }
    mImpl->glSurface = GLSurface();
#endif
    if (mImpl->window)
    {
      SDL_DestroyWindow(mImpl->window);
      mImpl->window = nullptr;
    }
    if (mImpl->ownsVideo)
    {
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
      mImpl->ownsVideo = false;
    }
    mImpl->backend = Backend::Null;
  }

  bool SDLWindow::valid() const
  {
    return mImpl->window != nullptr;
  }

  SDL_Window *SDLWindow::handle() const
  {
    return mImpl->window;
  }

  Backend SDLWindow::backend() const
  {
    return mImpl->backend;
  }

  void SDLWindow::drawableSize(std::uint32_t &width, std::uint32_t &height) const
  {
    width = 0;
    height = 0;
    if (!mImpl->window)
      return;
#if defined(GPU_HAS_OPENGL) || defined(GPU_HAS_OPENGLES)
    if (usesGLContext(mImpl->backend))
    {
      Impl::glDrawableSize(mImpl, width, height);
      return;
    }
#endif
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(mImpl->window, &windowWidth, &windowHeight);
    width = windowWidth > 0 ? static_cast<std::uint32_t>(windowWidth) : 0;
    height = windowHeight > 0 ? static_cast<std::uint32_t>(windowHeight) : 0;
  }

  SurfaceDesc SDLWindow::surfaceDesc() const
  {
    SurfaceDesc surface;
    surface.format = mImpl->format;
    drawableSize(surface.width, surface.height);
#if defined(GPU_HAS_OPENGL) || defined(GPU_HAS_OPENGLES)
    if (usesGLContext(mImpl->backend))
      surface.nativeHandle = &mImpl->glSurface;
#endif
    return surface;
  }

  bool SDLWindow::setVSync(bool enabled)
  {
    mImpl->vsync = enabled;
#if defined(GPU_HAS_OPENGL) || defined(GPU_HAS_OPENGLES)
    if (usesGLContext(mImpl->backend) && mImpl->context)
    {
      if (SDL_GL_SetSwapInterval(enabled ? 1 : 0) != 0)
      {
        mImpl->setError("SDL_GL_SetSwapInterval failed");
        return false;
      }
      return true;
    }
#endif
    mImpl->setMessage("SDLWindow: presentation synchronisation is owned by the device backend");
    return false;
  }

  bool SDLWindow::vsync() const
  {
    return mImpl->vsync;
  }

  const char *SDLWindow::lastError() const
  {
    return mImpl->error;
  }

} // namespace gpu
