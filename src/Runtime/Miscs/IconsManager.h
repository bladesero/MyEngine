#pragma once

#include "Renderer/RHI/RHITypes.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class IRHIDevice;
class IWindow;
struct GpuTexture;
struct GpuTextureView;

struct IconPixels {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba8;
};

struct IconColor {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    static IconColor White() { return {}; }
    static IconColor Black() { return {0, 0, 0, 255}; }
};

class IconsManager {
public:
    static constexpr const char* kEditorIcon = "engine-editor";
    static constexpr const char* kPlayerIcon = "engine-player";
    static constexpr const char* kCookerIcon = "engine-cooker";

    static IconsManager& Get();

    void SetIconRoot(std::filesystem::path root);
    const std::filesystem::path& GetIconRoot() const { return m_IconRoot; }
    void Clear();

    std::filesystem::path ResolveIconPath(std::string_view iconName) const;
    std::shared_ptr<IconPixels> Rasterize(std::string_view iconName,
                                          int size,
                                          IconColor color = IconColor::White());
    GpuTextureView* GetOrUpload(IRHIDevice& device,
                                std::string_view iconName,
                                int size,
                                IconColor color = IconColor::White());
    bool ApplyWindowIcon(IWindow& window, std::string_view iconName, int size = 64);
    bool WriteIco(std::string_view iconName,
                  const std::filesystem::path& output,
                  const std::vector<int>& sizes,
                  IconColor color = IconColor::White());

private:
    struct UploadedIcon;

    std::filesystem::path FindDefaultIconRoot() const;
    std::string MakePixelKey(std::string_view iconName, int size, IconColor color) const;
    std::string MakeUploadKey(RHIBackend backend, std::string_view iconName,
                              int size, IconColor color) const;

    std::filesystem::path m_IconRoot;
    std::unordered_map<std::string, std::shared_ptr<IconPixels>> m_PixelCache;
    std::unordered_map<std::string, std::shared_ptr<UploadedIcon>> m_UploadCache;
};
