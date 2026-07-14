#pragma once

#include <string>

// Forward-declare SDL types so headers that only need IWindow don't pull SDL.
struct SDL_Window;
struct SDL_Renderer;

// --------------------------------------------------------------------------
// WindowConfig
// --------------------------------------------------------------------------
enum class WindowMode { Windowed, Borderless, Fullscreen };

struct WindowConfig {
    std::string title       = "MyEngine";
    int         width       = 1280;
    int         height      = 720;
    bool        vsync       = true;
    bool        sdlRenderer = false;  // false = bare window for D3D11/Vulkan
    WindowMode  mode        = WindowMode::Windowed;
};

// --------------------------------------------------------------------------
// IWindow  –  platform-agnostic window interface
// --------------------------------------------------------------------------
class IWindow {
public:
    virtual ~IWindow() = default;

    virtual bool Init(const WindowConfig& config) = 0;
    virtual void Shutdown()                        = 0;

    // Present the rendered frame.
    virtual void SwapBuffers() = 0;

    virtual int  GetWidth()  const = 0;
    virtual int  GetHeight() const = 0;
    virtual bool IsOpen()    const = 0;
    virtual bool SetIconFromPixels(const void* rgba8, int width, int height) {
        (void)rgba8; (void)width; (void)height; return false;
    }

    // Raw handles – may be nullptr for non-SDL back-ends.
    virtual SDL_Window*   GetSDLWindow()   const { return nullptr; }
    virtual SDL_Renderer* GetSDLRenderer() const { return nullptr; }

    // Win32 HWND (or nullptr on non-Windows). Used by D3D11.
    virtual void* GetNativeHandle() const { return nullptr; }
};

// --------------------------------------------------------------------------
// SDLWindow  –  SDL3 implementation
// --------------------------------------------------------------------------
class SDLWindow : public IWindow {
public:
    ~SDLWindow() override;

    bool Init(const WindowConfig& config) override;
    void Shutdown()                        override;
    void SwapBuffers()                     override;

    int  GetWidth()  const override { return m_Width; }
    int  GetHeight() const override { return m_Height; }
    bool IsOpen()    const override { return m_Open; }
    bool SetIconFromPixels(const void* rgba8, int width, int height) override;

    SDL_Window*   GetSDLWindow()   const override { return m_Window; }
    SDL_Renderer* GetSDLRenderer() const override { return m_Renderer; }
    void*         GetNativeHandle() const override;

private:
    SDL_Window*   m_Window   = nullptr;
    SDL_Renderer* m_Renderer = nullptr;
    int           m_Width    = 0;
    int           m_Height   = 0;
    bool          m_Open     = false;
};
