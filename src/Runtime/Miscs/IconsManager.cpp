#include "Miscs/IconsManager.h"

#include "Core/Logger.h"
#include "Core/Window.h"
#include "Renderer/RHI/GpuTexture.h"
#include "Renderer/RHI/GpuTextureView.h"
#include "Renderer/RHI/IRHIDevice.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace {
struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct SvgStyle {
    bool fill = false;
    bool stroke = true;
    float strokeWidth = 1.75f;
    IconColor fillColor = IconColor::White();
    IconColor strokeColor = IconColor::White();
};

struct SvgShape {
    std::vector<Vec2f> points;
    SvgStyle style;
    bool closed = false;
};

struct SvgDocument {
    float width = 24.0f;
    float height = 24.0f;
    float minX = 0.0f;
    float minY = 0.0f;
    std::vector<SvgShape> shapes;
};

std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string WithSvgExtension(std::string_view iconName) {
    std::string name(iconName);
    if (name.size() < 4 || Lower(name.substr(name.size() - 4)) != ".svg")
        name += ".svg";
    return name;
}

std::string Attribute(const std::string& tag, const char* name) {
    const std::string key = std::string(name) + "=";
    size_t pos = tag.find(key);
    if (pos == std::string::npos)
        return {};
    pos += key.size();
    if (pos >= tag.size())
        return {};
    const char quote = tag[pos];
    if (quote != '"' && quote != '\'')
        return {};
    const size_t end = tag.find(quote, pos + 1);
    if (end == std::string::npos)
        return {};
    return tag.substr(pos + 1, end - pos - 1);
}

float ParseFloat(const std::string& value, float fallback) {
    if (value.empty())
        return fallback;
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    return end == value.c_str() ? fallback : parsed;
}

IconColor ParseColor(const std::string& value, IconColor currentColor) {
    if (value.empty() || value == "currentColor")
        return currentColor;
    if (value == "none")
        return {0, 0, 0, 0};
    if (value.size() == 7 && value[0] == '#') {
        const auto hex = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9')
                return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f')
                return static_cast<uint8_t>(10 + c - 'a');
            if (c >= 'A' && c <= 'F')
                return static_cast<uint8_t>(10 + c - 'A');
            return 0;
        };
        return {static_cast<uint8_t>((hex(value[1]) << 4) | hex(value[2])),
                static_cast<uint8_t>((hex(value[3]) << 4) | hex(value[4])),
                static_cast<uint8_t>((hex(value[5]) << 4) | hex(value[6])), 255};
    }
    return currentColor;
}

SvgStyle ParseStyle(const std::string& tag, const SvgStyle& inherited, IconColor currentColor) {
    SvgStyle style = inherited;
    const std::string fill = Attribute(tag, "fill");
    const std::string stroke = Attribute(tag, "stroke");
    const std::string strokeWidth = Attribute(tag, "stroke-width");
    if (!fill.empty()) {
        style.fillColor = ParseColor(fill, currentColor);
        style.fill = fill != "none" && style.fillColor.a != 0;
    }
    if (!stroke.empty()) {
        style.strokeColor = ParseColor(stroke, currentColor);
        style.stroke = stroke != "none" && style.strokeColor.a != 0;
    }
    if (!strokeWidth.empty())
        style.strokeWidth = ParseFloat(strokeWidth, style.strokeWidth);
    return style;
}

bool IsPathCommand(char c) {
    switch (c) {
    case 'M':
    case 'm':
    case 'L':
    case 'l':
    case 'H':
    case 'h':
    case 'V':
    case 'v':
    case 'C':
    case 'c':
    case 'A':
    case 'a':
    case 'Z':
    case 'z':
        return true;
    default:
        return false;
    }
}

void SkipSeparators(const std::string& text, size_t& pos) {
    while (pos < text.size()) {
        const char c = text[pos];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',')
            ++pos;
        else
            break;
    }
}

bool ReadPathNumber(const std::string& text, size_t& pos, float& out) {
    SkipSeparators(text, pos);
    if (pos >= text.size() || IsPathCommand(text[pos]))
        return false;
    const char* begin = text.c_str() + pos;
    char* end = nullptr;
    out = std::strtof(begin, &end);
    if (end == begin)
        return false;
    pos = static_cast<size_t>(end - text.c_str());
    SkipSeparators(text, pos);
    return true;
}

void AddPathShape(std::vector<SvgShape>& shapes, SvgShape& current) {
    if (current.points.size() >= 2)
        shapes.push_back(current);
    current.points.clear();
    current.closed = false;
}

std::vector<SvgShape> ParsePath(const std::string& path, SvgStyle style) {
    std::vector<SvgShape> shapes;
    SvgShape currentShape;
    currentShape.style = style;
    Vec2f cursor{};
    Vec2f subpathStart{};
    char command = 0;
    size_t pos = 0;
    while (pos < path.size()) {
        SkipSeparators(path, pos);
        if (pos >= path.size())
            break;
        if (IsPathCommand(path[pos]))
            command = path[pos++];
        if (!command)
            break;

        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        switch (command) {
        case 'M':
        case 'm': {
            float x = 0.0f, y = 0.0f;
            if (!ReadPathNumber(path, pos, x) || !ReadPathNumber(path, pos, y))
                break;
            AddPathShape(shapes, currentShape);
            cursor = relative ? Vec2f{cursor.x + x, cursor.y + y} : Vec2f{x, y};
            subpathStart = cursor;
            currentShape = {};
            currentShape.style = style;
            currentShape.points.push_back(cursor);
            command = relative ? 'l' : 'L';
            break;
        }
        case 'L':
        case 'l': {
            float x = 0.0f, y = 0.0f;
            if (!ReadPathNumber(path, pos, x) || !ReadPathNumber(path, pos, y))
                break;
            cursor = relative ? Vec2f{cursor.x + x, cursor.y + y} : Vec2f{x, y};
            currentShape.points.push_back(cursor);
            break;
        }
        case 'H':
        case 'h': {
            float x = 0.0f;
            if (!ReadPathNumber(path, pos, x))
                break;
            cursor.x = relative ? cursor.x + x : x;
            currentShape.points.push_back(cursor);
            break;
        }
        case 'V':
        case 'v': {
            float y = 0.0f;
            if (!ReadPathNumber(path, pos, y))
                break;
            cursor.y = relative ? cursor.y + y : y;
            currentShape.points.push_back(cursor);
            break;
        }
        case 'C':
        case 'c': {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0, x = 0, y = 0;
            if (!ReadPathNumber(path, pos, x1) || !ReadPathNumber(path, pos, y1) || !ReadPathNumber(path, pos, x2) ||
                !ReadPathNumber(path, pos, y2) || !ReadPathNumber(path, pos, x) || !ReadPathNumber(path, pos, y))
                break;
            const Vec2f p0 = cursor;
            const Vec2f p1 = relative ? Vec2f{cursor.x + x1, cursor.y + y1} : Vec2f{x1, y1};
            const Vec2f p2 = relative ? Vec2f{cursor.x + x2, cursor.y + y2} : Vec2f{x2, y2};
            const Vec2f p3 = relative ? Vec2f{cursor.x + x, cursor.y + y} : Vec2f{x, y};
            for (int i = 1; i <= 12; ++i) {
                const float t = static_cast<float>(i) / 12.0f;
                const float u = 1.0f - t;
                currentShape.points.push_back(
                    {u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
                     u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y});
            }
            cursor = p3;
            break;
        }
        case 'A':
        case 'a': {
            float rx = 0, ry = 0, rotation = 0, largeArc = 0, sweep = 0, x = 0, y = 0;
            if (!ReadPathNumber(path, pos, rx) || !ReadPathNumber(path, pos, ry) ||
                !ReadPathNumber(path, pos, rotation) || !ReadPathNumber(path, pos, largeArc) ||
                !ReadPathNumber(path, pos, sweep) || !ReadPathNumber(path, pos, x) || !ReadPathNumber(path, pos, y))
                break;
            (void)rx;
            (void)ry;
            (void)rotation;
            (void)largeArc;
            (void)sweep;
            cursor = relative ? Vec2f{cursor.x + x, cursor.y + y} : Vec2f{x, y};
            currentShape.points.push_back(cursor);
            break;
        }
        case 'Z':
        case 'z':
            currentShape.points.push_back(subpathStart);
            currentShape.closed = true;
            AddPathShape(shapes, currentShape);
            break;
        default:
            ++pos;
            break;
        }
    }
    AddPathShape(shapes, currentShape);
    return shapes;
}

SvgDocument ParseSvg(const std::string& svg, IconColor color) {
    SvgDocument document;
    const size_t svgStart = svg.find("<svg");
    if (svgStart != std::string::npos) {
        const size_t svgEnd = svg.find('>', svgStart);
        const std::string tag =
            svgEnd == std::string::npos ? std::string{} : svg.substr(svgStart, svgEnd - svgStart + 1);
        const std::string viewBox = Attribute(tag, "viewBox");
        if (!viewBox.empty()) {
            std::istringstream input(viewBox);
            input >> document.minX >> document.minY >> document.width >> document.height;
        } else {
            document.width = ParseFloat(Attribute(tag, "width"), document.width);
            document.height = ParseFloat(Attribute(tag, "height"), document.height);
        }
    }

    SvgStyle rootStyle;
    rootStyle.fill = false;
    rootStyle.stroke = true;
    rootStyle.strokeColor = color;
    rootStyle.fillColor = color;

    size_t pos = 0;
    while ((pos = svg.find('<', pos)) != std::string::npos) {
        const size_t end = svg.find('>', pos);
        if (end == std::string::npos)
            break;
        const std::string tag = svg.substr(pos, end - pos + 1);
        pos = end + 1;
        if (tag.rfind("<path", 0) == 0) {
            const std::string d = Attribute(tag, "d");
            if (d.empty())
                continue;
            auto shapes = ParsePath(d, ParseStyle(tag, rootStyle, color));
            document.shapes.insert(document.shapes.end(), shapes.begin(), shapes.end());
        } else if (tag.rfind("<rect", 0) == 0) {
            SvgShape shape;
            shape.style = ParseStyle(tag, rootStyle, color);
            shape.closed = true;
            const float x = ParseFloat(Attribute(tag, "x"), 0.0f);
            const float y = ParseFloat(Attribute(tag, "y"), 0.0f);
            const float w = ParseFloat(Attribute(tag, "width"), 0.0f);
            const float h = ParseFloat(Attribute(tag, "height"), 0.0f);
            shape.points = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, {x, y}};
            document.shapes.push_back(std::move(shape));
        } else if (tag.rfind("<circle", 0) == 0) {
            SvgShape shape;
            shape.style = ParseStyle(tag, rootStyle, color);
            shape.closed = true;
            const float cx = ParseFloat(Attribute(tag, "cx"), 0.0f);
            const float cy = ParseFloat(Attribute(tag, "cy"), 0.0f);
            const float r = ParseFloat(Attribute(tag, "r"), 0.0f);
            for (int i = 0; i <= 32; ++i) {
                const float a = static_cast<float>(i) / 32.0f * 6.28318530718f;
                shape.points.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
            }
            document.shapes.push_back(std::move(shape));
        }
    }
    return document;
}

void BlendPixel(IconPixels& pixels, int x, int y, IconColor color) {
    if (x < 0 || y < 0 || x >= pixels.width || y >= pixels.height || color.a == 0)
        return;
    uint8_t* dst = pixels.rgba8.data() + (static_cast<size_t>(y) * pixels.width + x) * 4;
    const float srcA = static_cast<float>(color.a) / 255.0f;
    const float dstA = static_cast<float>(dst[3]) / 255.0f;
    const float outA = srcA + dstA * (1.0f - srcA);
    if (outA <= 0.0f)
        return;
    dst[0] = static_cast<uint8_t>((color.r * srcA + dst[0] * dstA * (1.0f - srcA)) / outA);
    dst[1] = static_cast<uint8_t>((color.g * srcA + dst[1] * dstA * (1.0f - srcA)) / outA);
    dst[2] = static_cast<uint8_t>((color.b * srcA + dst[2] * dstA * (1.0f - srcA)) / outA);
    dst[3] = static_cast<uint8_t>((std::min)(255.0f, outA * 255.0f));
}

void DrawLine(IconPixels& pixels, Vec2f a, Vec2f b, float width, IconColor color) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.001f)
        return;
    const int steps = (std::max)(1, static_cast<int>(std::ceil(length * 2.0f)));
    const int radius = (std::max)(1, static_cast<int>(std::ceil(width * 0.5f)));
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int cx = static_cast<int>(std::round(a.x + dx * t));
        const int cy = static_cast<int>(std::round(a.y + dy * t));
        for (int oy = -radius; oy <= radius; ++oy) {
            for (int ox = -radius; ox <= radius; ++ox) {
                if (ox * ox + oy * oy <= radius * radius)
                    BlendPixel(pixels, cx + ox, cy + oy, color);
            }
        }
    }
}

bool PointInPolygon(float x, float y, const std::vector<Vec2f>& points) {
    bool inside = false;
    for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
        const Vec2f& pi = points[i];
        const Vec2f& pj = points[j];
        const bool intersect =
            ((pi.y > y) != (pj.y > y)) &&
            (x < (pj.x - pi.x) * (y - pi.y) / ((pj.y - pi.y) == 0.0f ? 1e-6f : (pj.y - pi.y)) + pi.x);
        if (intersect)
            inside = !inside;
    }
    return inside;
}

void FillPolygon(IconPixels& pixels, const std::vector<Vec2f>& points, IconColor color) {
    if (points.size() < 3)
        return;
    float minX = points[0].x, maxX = points[0].x, minY = points[0].y, maxY = points[0].y;
    for (const Vec2f& p : points) {
        minX = (std::min)(minX, p.x);
        maxX = (std::max)(maxX, p.x);
        minY = (std::min)(minY, p.y);
        maxY = (std::max)(maxY, p.y);
    }
    const int ix0 = (std::max)(0, static_cast<int>(std::floor(minX)));
    const int iy0 = (std::max)(0, static_cast<int>(std::floor(minY)));
    const int ix1 = (std::min)(pixels.width - 1, static_cast<int>(std::ceil(maxX)));
    const int iy1 = (std::min)(pixels.height - 1, static_cast<int>(std::ceil(maxY)));
    for (int y = iy0; y <= iy1; ++y) {
        for (int x = ix0; x <= ix1; ++x) {
            if (PointInPolygon(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, points)) {
                BlendPixel(pixels, x, y, color);
            }
        }
    }
}

std::shared_ptr<IconPixels> RenderSvg(const SvgDocument& svg, int size) {
    if (size <= 0 || svg.width <= 0.0f || svg.height <= 0.0f)
        return nullptr;
    auto pixels = std::make_shared<IconPixels>();
    pixels->width = size;
    pixels->height = size;
    pixels->rgba8.assign(static_cast<size_t>(size) * size * 4, 0);

    const float scale = static_cast<float>(size) / (std::max)(svg.width, svg.height);
    const float offsetX = (static_cast<float>(size) - svg.width * scale) * 0.5f - svg.minX * scale;
    const float offsetY = (static_cast<float>(size) - svg.height * scale) * 0.5f - svg.minY * scale;
    for (const SvgShape& shape : svg.shapes) {
        std::vector<Vec2f> points;
        points.reserve(shape.points.size());
        for (const Vec2f& point : shape.points) {
            points.push_back({point.x * scale + offsetX, point.y * scale + offsetY});
        }
        if (shape.style.fill && shape.closed)
            FillPolygon(*pixels, points, shape.style.fillColor);
        if (shape.style.stroke && points.size() >= 2) {
            for (size_t i = 1; i < points.size(); ++i)
                DrawLine(*pixels, points[i - 1], points[i], (std::max)(1.0f, shape.style.strokeWidth * scale),
                         shape.style.strokeColor);
        }
    }
    return pixels;
}

void WriteU16(std::ofstream& out, uint16_t v) {
    out.put(static_cast<char>(v & 0xff));
    out.put(static_cast<char>((v >> 8) & 0xff));
}

void WriteU32(std::ofstream& out, uint32_t v) {
    WriteU16(out, static_cast<uint16_t>(v & 0xffff));
    WriteU16(out, static_cast<uint16_t>((v >> 16) & 0xffff));
}

std::vector<uint8_t> MakeIcoDib(const IconPixels& pixels) {
    const int width = pixels.width;
    const int height = pixels.height;
    const int maskStride = ((width + 31) / 32) * 4;
    std::vector<uint8_t> data;
    data.reserve(40 + static_cast<size_t>(width) * height * 4 + static_cast<size_t>(maskStride) * height);
    auto append16 = [&data](uint16_t value) {
        data.push_back(static_cast<uint8_t>(value & 0xff));
        data.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    };
    auto append32 = [&data, &append16](uint32_t value) {
        append16(static_cast<uint16_t>(value & 0xffff));
        append16(static_cast<uint16_t>((value >> 16) & 0xffff));
    };
    append32(40);
    append32(static_cast<uint32_t>(width));
    append32(static_cast<uint32_t>(height * 2));
    append16(1);
    append16(32);
    append32(0);
    append32(static_cast<uint32_t>(width * height * 4 + maskStride * height));
    append32(0);
    append32(0);
    append32(0);
    append32(0);
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t* src = pixels.rgba8.data() + (static_cast<size_t>(y) * width + x) * 4;
            data.push_back(src[2]);
            data.push_back(src[1]);
            data.push_back(src[0]);
            data.push_back(src[3]);
        }
    }
    data.resize(data.size() + static_cast<size_t>(maskStride) * height, 0);
    return data;
}

} // namespace

struct IconsManager::UploadedIcon {
    std::shared_ptr<GpuTexture> texture;
    std::shared_ptr<GpuTextureView> view;
};

IconsManager& IconsManager::Get() {
    static IconsManager manager;
    return manager;
}

void IconsManager::SetIconRoot(std::filesystem::path root) {
    m_IconRoot = std::move(root);
}

void IconsManager::Clear() {
    m_PixelCache.clear();
    m_UploadCache.clear();
}

fs::path IconsManager::FindDefaultIconRoot() const {
    if (const char* basePath = SDL_GetBasePath()) {
        fs::path candidate = fs::path(basePath) / "EngineContent" / "Editor" / "Icons";
        std::error_code ec;
        if (fs::is_directory(candidate, ec) && !ec)
            return candidate.lexically_normal();
    }
    fs::path candidate = fs::current_path() / "EngineContent" / "Editor" / "Icons";
    return candidate.lexically_normal();
}

fs::path IconsManager::ResolveIconPath(std::string_view iconName) const {
    const fs::path root = m_IconRoot.empty() ? FindDefaultIconRoot() : m_IconRoot;
    const fs::path path = root / WithSvgExtension(iconName);
    std::error_code ec;
    if (fs::is_regular_file(path, ec) && !ec)
        return path.lexically_normal();
    return {};
}

std::string IconsManager::MakePixelKey(std::string_view iconName, int size, IconColor color) const {
    return Lower(std::string(iconName)) + "|" + std::to_string(size) + "|" + std::to_string(color.r) + "," +
           std::to_string(color.g) + "," + std::to_string(color.b) + "," + std::to_string(color.a);
}

std::string IconsManager::MakeUploadKey(RHIBackend backend, std::string_view iconName, int size,
                                        IconColor color) const {
    return std::to_string(static_cast<int>(backend)) + "|" + MakePixelKey(iconName, size, color);
}

std::shared_ptr<IconPixels> IconsManager::Rasterize(std::string_view iconName, int size, IconColor color) {
    if (size <= 0)
        return nullptr;
    const std::string key = MakePixelKey(iconName, size, color);
    if (auto it = m_PixelCache.find(key); it != m_PixelCache.end())
        return it->second;
    const fs::path path = ResolveIconPath(iconName);
    if (path.empty()) {
        Logger::Warn("[Icons] Missing icon: ", std::string(iconName));
        return nullptr;
    }
    const std::string svg = ReadFile(path);
    if (svg.empty()) {
        Logger::Warn("[Icons] Failed to read SVG: ", path.string());
        return nullptr;
    }
    auto pixels = RenderSvg(ParseSvg(svg, color), size);
    if (!pixels || pixels->rgba8.empty()) {
        Logger::Warn("[Icons] Failed to rasterize SVG: ", path.string());
        return nullptr;
    }
    m_PixelCache[key] = pixels;
    return pixels;
}

GpuTextureView* IconsManager::GetOrUpload(IRHIDevice& device, std::string_view iconName, int size, IconColor color) {
    const std::string key = MakeUploadKey(device.GetBackend(), iconName, size, color);
    if (auto it = m_UploadCache.find(key); it != m_UploadCache.end())
        return it->second && it->second->view ? it->second->view.get() : nullptr;

    auto pixels = Rasterize(iconName, size, color);
    if (!pixels)
        return nullptr;
    auto texture = device.UploadTexture2D(pixels->rgba8.data(), pixels->width, pixels->height);
    if (!texture)
        return nullptr;
    RHITextureViewDesc viewDesc;
    viewDesc.usage = RHIResourceUsage::ShaderResource;
    auto view = device.CreateTextureView(texture, viewDesc);
    if (!view)
        return nullptr;
    auto uploaded = std::make_shared<UploadedIcon>();
    uploaded->texture = std::move(texture);
    uploaded->view = std::move(view);
    GpuTextureView* result = uploaded->view.get();
    m_UploadCache[key] = std::move(uploaded);
    return result;
}

bool IconsManager::ApplyWindowIcon(IWindow& window, std::string_view iconName, int size) {
    auto pixels = Rasterize(iconName, size, IconColor::White());
    return pixels && window.SetIconFromPixels(pixels->rgba8.data(), pixels->width, pixels->height);
}

bool IconsManager::WriteIco(std::string_view iconName, const fs::path& output, const std::vector<int>& sizes,
                            IconColor color) {
    if (sizes.empty())
        return false;
    struct Entry {
        int size = 0;
        std::vector<uint8_t> dib;
    };
    std::vector<Entry> entries;
    for (int size : sizes) {
        auto pixels = Rasterize(iconName, size, color);
        if (!pixels)
            return false;
        entries.push_back({size, MakeIcoDib(*pixels)});
    }
    std::error_code ec;
    fs::create_directories(output.parent_path(), ec);
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    WriteU16(out, 0);
    WriteU16(out, 1);
    WriteU16(out, static_cast<uint16_t>(entries.size()));
    uint32_t offset = 6 + static_cast<uint32_t>(entries.size()) * 16;
    for (const Entry& entry : entries) {
        out.put(static_cast<char>(entry.size >= 256 ? 0 : entry.size));
        out.put(static_cast<char>(entry.size >= 256 ? 0 : entry.size));
        out.put(0);
        out.put(0);
        WriteU16(out, 1);
        WriteU16(out, 32);
        WriteU32(out, static_cast<uint32_t>(entry.dib.size()));
        WriteU32(out, offset);
        offset += static_cast<uint32_t>(entry.dib.size());
    }
    for (const Entry& entry : entries)
        out.write(reinterpret_cast<const char*>(entry.dib.data()), static_cast<std::streamsize>(entry.dib.size()));
    return out.good();
}
