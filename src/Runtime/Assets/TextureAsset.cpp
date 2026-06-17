#include "Assets/TextureAsset.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

uint16_t PackRgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(
        ((static_cast<uint16_t>(r) >> 3) << 11) |
        ((static_cast<uint16_t>(g) >> 2) << 5) |
        (static_cast<uint16_t>(b) >> 3));
}

std::array<uint8_t, 3> UnpackRgb565(uint16_t color)
{
    const uint8_t r5 = static_cast<uint8_t>((color >> 11) & 31);
    const uint8_t g6 = static_cast<uint8_t>((color >> 5) & 63);
    const uint8_t b5 = static_cast<uint8_t>(color & 31);
    return {
        static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
        static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
        static_cast<uint8_t>((b5 << 3) | (b5 >> 2))
    };
}

std::vector<uint8_t> CompressBc1(const std::vector<uint8_t>& rgba, int width, int height)
{
    const int blocksX = (width + 3) / 4;
    const int blocksY = (height + 3) / 4;
    std::vector<uint8_t> output(static_cast<size_t>(blocksX * blocksY * 8));
    size_t outputOffset = 0;

    for (int blockY = 0; blockY < blocksY; ++blockY) {
        for (int blockX = 0; blockX < blocksX; ++blockX) {
            std::array<std::array<uint8_t, 3>, 16> pixels{};
            uint8_t minR = 255, minG = 255, minB = 255;
            uint8_t maxR = 0, maxG = 0, maxB = 0;
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    const int sourceX = (std::min)(blockX * 4 + x, width - 1);
                    const int sourceY = (std::min)(blockY * 4 + y, height - 1);
                    const size_t source = static_cast<size_t>((sourceY * width + sourceX) * 4);
                    auto& pixel = pixels[static_cast<size_t>(y * 4 + x)];
                    pixel = { rgba[source], rgba[source + 1], rgba[source + 2] };
                    minR = (std::min)(minR, pixel[0]);
                    minG = (std::min)(minG, pixel[1]);
                    minB = (std::min)(minB, pixel[2]);
                    maxR = (std::max)(maxR, pixel[0]);
                    maxG = (std::max)(maxG, pixel[1]);
                    maxB = (std::max)(maxB, pixel[2]);
                }
            }

            uint16_t color0 = PackRgb565(maxR, maxG, maxB);
            uint16_t color1 = PackRgb565(minR, minG, minB);
            if (color0 <= color1) {
                if (color1 < 0xffff) color0 = static_cast<uint16_t>(color1 + 1);
                else color1 = static_cast<uint16_t>(color0 - 1);
            }
            const auto c0 = UnpackRgb565(color0);
            const auto c1 = UnpackRgb565(color1);
            std::array<std::array<uint8_t, 3>, 4> palette = {
                c0, c1,
                std::array<uint8_t, 3>{
                    static_cast<uint8_t>((2 * c0[0] + c1[0]) / 3),
                    static_cast<uint8_t>((2 * c0[1] + c1[1]) / 3),
                    static_cast<uint8_t>((2 * c0[2] + c1[2]) / 3)
                },
                std::array<uint8_t, 3>{
                    static_cast<uint8_t>((c0[0] + 2 * c1[0]) / 3),
                    static_cast<uint8_t>((c0[1] + 2 * c1[1]) / 3),
                    static_cast<uint8_t>((c0[2] + 2 * c1[2]) / 3)
                }
            };

            uint32_t selectors = 0;
            for (size_t pixelIndex = 0; pixelIndex < pixels.size(); ++pixelIndex) {
                uint32_t bestIndex = 0;
                int bestDistance = 0x7fffffff;
                for (uint32_t paletteIndex = 0; paletteIndex < 4; ++paletteIndex) {
                    const int dr = static_cast<int>(pixels[pixelIndex][0]) - palette[paletteIndex][0];
                    const int dg = static_cast<int>(pixels[pixelIndex][1]) - palette[paletteIndex][1];
                    const int db = static_cast<int>(pixels[pixelIndex][2]) - palette[paletteIndex][2];
                    const int distance = dr * dr + dg * dg + db * db;
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestIndex = paletteIndex;
                    }
                }
                selectors |= bestIndex << (pixelIndex * 2);
            }

            output[outputOffset++] = static_cast<uint8_t>(color0 & 0xff);
            output[outputOffset++] = static_cast<uint8_t>(color0 >> 8);
            output[outputOffset++] = static_cast<uint8_t>(color1 & 0xff);
            output[outputOffset++] = static_cast<uint8_t>(color1 >> 8);
            for (int byte = 0; byte < 4; ++byte) {
                output[outputOffset++] = static_cast<uint8_t>(selectors >> (byte * 8));
            }
        }
    }
    return output;
}

} // namespace

void TextureAsset::RebuildDerivedData()
{
    m_Mips.clear();
    if (m_Desc.width <= 0 || m_Desc.height <= 0 ||
        m_PixelData.size() != static_cast<size_t>(m_Desc.width * m_Desc.height * 4)) {
        m_Desc.mipLevels = 1;
        return;
    }

    TextureMipData level;
    level.width = m_Desc.width;
    level.height = m_Desc.height;
    level.rgba8 = m_PixelData;
    while (true) {
        level.bc1 = CompressBc1(level.rgba8, level.width, level.height);
        m_Mips.push_back(level);
        if (level.width == 1 && level.height == 1) break;

        TextureMipData next;
        next.width = (std::max)(1, level.width / 2);
        next.height = (std::max)(1, level.height / 2);
        next.rgba8.resize(static_cast<size_t>(next.width * next.height * 4));
        for (int y = 0; y < next.height; ++y) {
            for (int x = 0; x < next.width; ++x) {
                for (int channel = 0; channel < 4; ++channel) {
                    int sum = 0;
                    int samples = 0;
                    for (int offsetY = 0; offsetY < 2; ++offsetY) {
                        for (int offsetX = 0; offsetX < 2; ++offsetX) {
                            const int sourceX = (std::min)(level.width - 1, x * 2 + offsetX);
                            const int sourceY = (std::min)(level.height - 1, y * 2 + offsetY);
                            sum += level.rgba8[
                                static_cast<size_t>((sourceY * level.width + sourceX) * 4 + channel)];
                            ++samples;
                        }
                    }
                    next.rgba8[static_cast<size_t>((y * next.width + x) * 4 + channel)] =
                        static_cast<uint8_t>(sum / samples);
                }
            }
        }
        level = std::move(next);
    }
    m_Desc.mipLevels = static_cast<int>(m_Mips.size());
}
