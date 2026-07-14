#include "Window.h"
#include "Logger.h"
#include "Platform.h"

#include <SDL3/SDL.h>

#ifdef MYENGINE_PLATFORM_WINDOWS
#include <windows.h>
#endif

// --------------------------------------------------------------------------
// SDLWindow
// --------------------------------------------------------------------------

SDLWindow::~SDLWindow() {
    Shutdown();
}

bool SDLWindow::Init(const WindowConfig& config) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        Logger::Error("SDL_Init failed: ", SDL_GetError());
        return false;
    }

    m_Width = config.width;
    m_Height = config.height;

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    if (config.mode == WindowMode::Borderless)
        flags = static_cast<SDL_WindowFlags>(flags | SDL_WINDOW_BORDERLESS);
    else if (config.mode == WindowMode::Fullscreen)
        flags = SDL_WINDOW_FULLSCREEN;

    m_Window = SDL_CreateWindow(config.title.c_str(), m_Width, m_Height, flags);
    if (!m_Window) {
        Logger::Error("SDL_CreateWindow failed: ", SDL_GetError());
        SDL_Quit();
        return false;
    }

    // Only create SDL renderer when requested (not needed for D3D11/Vulkan).
    if (config.sdlRenderer) {
        m_Renderer = SDL_CreateRenderer(m_Window, nullptr);
        if (!m_Renderer) {
            Logger::Error("SDL_CreateRenderer failed: ", SDL_GetError());
            SDL_DestroyWindow(m_Window);
            m_Window = nullptr;
            SDL_Quit();
            return false;
        }
        if (config.vsync) {
            SDL_SetRenderVSync(m_Renderer, 1);
        }
    }

    m_Open = true;
    Logger::Info("SDLWindow created: ", config.title, " (", m_Width, "x", m_Height, ")");
    return true;
}

void SDLWindow::Shutdown() {
    if (m_Renderer) {
        SDL_DestroyRenderer(m_Renderer);
        m_Renderer = nullptr;
    }
    if (m_Window) {
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
    }
    if (m_Open) {
        SDL_Quit();
        m_Open = false;
        Logger::Info("SDLWindow destroyed");
    }
}

void SDLWindow::SwapBuffers() {
    SDL_RenderPresent(m_Renderer);
}

bool SDLWindow::SetIconFromPixels(const void* rgba8, int width, int height) {
    if (!m_Window || !rgba8 || width <= 0 || height <= 0)
        return false;
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, const_cast<void*>(rgba8), width * 4);
    if (!surface) {
        Logger::Warn("SDL_CreateSurfaceFrom(icon) failed: ", SDL_GetError());
        return false;
    }
    const bool ok = SDL_SetWindowIcon(m_Window, surface);
    SDL_DestroySurface(surface);
    if (!ok)
        Logger::Warn("SDL_SetWindowIcon failed: ", SDL_GetError());
    return ok;
}

void* SDLWindow::GetNativeHandle() const {
#ifdef MYENGINE_PLATFORM_WINDOWS
    if (!m_Window)
        return nullptr;
    return SDL_GetPointerProperty(SDL_GetWindowProperties(m_Window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(MYENGINE_PLATFORM_MACOS)
    if (!m_Window)
        return nullptr;
    return SDL_GetPointerProperty(SDL_GetWindowProperties(m_Window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#else
    return nullptr;
#endif
}
