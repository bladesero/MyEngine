#include "Assets/TextureAsset.h"
#include "Core/RuntimeFileSystem.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <type_traits>

namespace {

constexpr uint32_t kTexturePayloadMagic = 0x5845544d; // MTEX
constexpr uint32_t kTexturePayloadVersion = 2;
constexpr uint32_t kMaxTextureArrayItems = 64u << 20;
const std::vector<uint8_t> kEmptyCompressedMip;

template <typename T> bool WritePod(std::ostream& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(out);
}

template <typename T> bool ReadPod(std::istream& in, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

template <typename T> bool WriteVector(std::ostream& out, const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (values.size() > kMaxTextureArrayItems)
        return false;
    const uint32_t size = static_cast<uint32_t>(values.size());
    if (!WritePod(out, size))
        return false;
    if (values.empty())
        return true;
    out.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
    return static_cast<bool>(out);
}

template <typename T> bool ReadVector(std::istream& in, std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    uint32_t size = 0;
    if (!ReadPod(in, size) || size > kMaxTextureArrayItems)
        return false;
    values.resize(size);
    if (values.empty())
        return true;
    in.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
    return static_cast<bool>(in);
}

bool WriteTextureDesc(std::ostream& out, const TextureDesc& desc) {
    return WritePod(out, desc.width) && WritePod(out, desc.height) && WritePod(out, desc.mipLevels) &&
           WritePod(out, desc.format) && WritePod(out, desc.filter) && WritePod(out, desc.wrapU) &&
           WritePod(out, desc.wrapV) && WritePod(out, desc.sRGB) && WritePod(out, desc.generateCompressedMips);
}

bool ReadTextureDesc(std::istream& in, TextureDesc& desc) {
    return ReadPod(in, desc.width) && ReadPod(in, desc.height) && ReadPod(in, desc.mipLevels) &&
           ReadPod(in, desc.format) && ReadPod(in, desc.filter) && ReadPod(in, desc.wrapU) && ReadPod(in, desc.wrapV) &&
           ReadPod(in, desc.sRGB) && ReadPod(in, desc.generateCompressedMips);
}

bool WriteMipChain(std::ostream& out, const std::vector<TextureMipData>& mips) {
    if (mips.size() > kMaxTextureArrayItems || !WritePod(out, static_cast<uint32_t>(mips.size()))) {
        return false;
    }
    for (const TextureMipData& mip : mips) {
        if (!WritePod(out, mip.width) || !WritePod(out, mip.height) || !WriteVector(out, mip.rgba8) ||
            !WriteVector(out, mip.bc1) || !WriteVector(out, mip.bc3)) {
            return false;
        }
    }
    return true;
}

bool ReadMipChain(std::istream& in, std::vector<TextureMipData>& mips, uint32_t version) {
    uint32_t mipCount = 0;
    if (!ReadPod(in, mipCount) || mipCount > kMaxTextureArrayItems)
        return false;
    mips.resize(mipCount);
    for (TextureMipData& mip : mips) {
        if (!ReadPod(in, mip.width) || !ReadPod(in, mip.height) || !ReadVector(in, mip.rgba8) ||
            !ReadVector(in, mip.bc1)) {
            return false;
        }
        if (version >= 2 && !ReadVector(in, mip.bc3))
            return false;
    }
    return true;
}

uint16_t PackRgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((static_cast<uint16_t>(r) >> 3) << 11) | ((static_cast<uint16_t>(g) >> 2) << 5) |
                                 (static_cast<uint16_t>(b) >> 3));
}

std::array<uint8_t, 3> UnpackRgb565(uint16_t color) {
    const uint8_t r5 = static_cast<uint8_t>((color >> 11) & 31);
    const uint8_t g6 = static_cast<uint8_t>((color >> 5) & 63);
    const uint8_t b5 = static_cast<uint8_t>(color & 31);
    return {static_cast<uint8_t>((r5 << 3) | (r5 >> 2)), static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
            static_cast<uint8_t>((b5 << 3) | (b5 >> 2))};
}

std::vector<uint8_t> CompressBc1(const std::vector<uint8_t>& rgba, int width, int height) {
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
                    pixel = {rgba[source], rgba[source + 1], rgba[source + 2]};
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
                if (color1 < 0xffff)
                    color0 = static_cast<uint16_t>(color1 + 1);
                else
                    color1 = static_cast<uint16_t>(color0 - 1);
            }
            const auto c0 = UnpackRgb565(color0);
            const auto c1 = UnpackRgb565(color1);
            std::array<std::array<uint8_t, 3>, 4> palette = {
                c0, c1,
                std::array<uint8_t, 3>{static_cast<uint8_t>((2 * c0[0] + c1[0]) / 3),
                                       static_cast<uint8_t>((2 * c0[1] + c1[1]) / 3),
                                       static_cast<uint8_t>((2 * c0[2] + c1[2]) / 3)},
                std::array<uint8_t, 3>{static_cast<uint8_t>((c0[0] + 2 * c1[0]) / 3),
                                       static_cast<uint8_t>((c0[1] + 2 * c1[1]) / 3),
                                       static_cast<uint8_t>((c0[2] + 2 * c1[2]) / 3)}};

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

std::vector<uint8_t> CompressBc3(const std::vector<uint8_t>& rgba, int width, int height) {
    const int blocksX = (width + 3) / 4;
    const int blocksY = (height + 3) / 4;
    std::vector<uint8_t> output(static_cast<size_t>(blocksX * blocksY * 16));
    size_t outputOffset = 0;
    const std::vector<uint8_t> bc1 = CompressBc1(rgba, width, height);
    size_t bc1Offset = 0;

    for (int blockY = 0; blockY < blocksY; ++blockY) {
        for (int blockX = 0; blockX < blocksX; ++blockX) {
            std::array<uint8_t, 16> alphas{};
            uint8_t minA = 255;
            uint8_t maxA = 0;
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    const int sourceX = (std::min)(blockX * 4 + x, width - 1);
                    const int sourceY = (std::min)(blockY * 4 + y, height - 1);
                    const size_t source = static_cast<size_t>((sourceY * width + sourceX) * 4 + 3);
                    const uint8_t alpha = rgba[source];
                    alphas[static_cast<size_t>(y * 4 + x)] = alpha;
                    minA = (std::min)(minA, alpha);
                    maxA = (std::max)(maxA, alpha);
                }
            }

            output[outputOffset++] = maxA;
            output[outputOffset++] = minA;
            std::array<uint8_t, 8> palette{};
            palette[0] = maxA;
            palette[1] = minA;
            if (maxA > minA) {
                for (int i = 1; i <= 6; ++i) {
                    palette[static_cast<size_t>(i + 1)] = static_cast<uint8_t>(((7 - i) * maxA + i * minA) / 7);
                }
            } else {
                for (int i = 1; i <= 4; ++i) {
                    palette[static_cast<size_t>(i + 1)] = static_cast<uint8_t>(((5 - i) * maxA + i * minA) / 5);
                }
                palette[6] = 0;
                palette[7] = 255;
            }

            uint64_t selectors = 0;
            for (size_t pixelIndex = 0; pixelIndex < alphas.size(); ++pixelIndex) {
                uint32_t bestIndex = 0;
                int bestDistance = 0x7fffffff;
                for (uint32_t paletteIndex = 0; paletteIndex < palette.size(); ++paletteIndex) {
                    const int distance =
                        std::abs(static_cast<int>(alphas[pixelIndex]) - static_cast<int>(palette[paletteIndex]));
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestIndex = paletteIndex;
                    }
                }
                selectors |= static_cast<uint64_t>(bestIndex) << (pixelIndex * 3);
            }
            for (int byte = 0; byte < 6; ++byte) {
                output[outputOffset++] = static_cast<uint8_t>(selectors >> (byte * 8));
            }
            std::copy_n(bc1.data() + bc1Offset, 8, output.data() + outputOffset);
            bc1Offset += 8;
            outputOffset += 8;
        }
    }
    return output;
}

} // namespace

const std::vector<uint8_t>& TextureAsset::GetCompressedMip(size_t level) const {
    return level < m_Mips.size() ? m_Mips[level].bc1 : kEmptyCompressedMip;
}

const std::vector<uint8_t>& TextureAsset::GetCompressedBc3Mip(size_t level) const {
    return level < m_Mips.size() ? m_Mips[level].bc3 : kEmptyCompressedMip;
}

bool SaveTexturePayloadToFile(const TextureAsset& texture, const std::string& path) {
    try {
        const std::filesystem::path outputPath(path);
        std::filesystem::create_directories(outputPath.parent_path());
        const std::string temporary = path + ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            if (!WritePod(out, kTexturePayloadMagic) || !WritePod(out, kTexturePayloadVersion) ||
                !WriteTextureDesc(out, texture.GetDesc()) || !WriteVector(out, texture.GetPixelData()) ||
                !WriteMipChain(out, texture.GetMips())) {
                return false;
            }
            out.flush();
            if (!out)
                return false;
        }
        std::error_code ec;
        std::filesystem::remove(outputPath, ec);
        ec.clear();
        std::filesystem::rename(temporary, outputPath, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool TextureAsset::EnsurePayloadLoaded() {
    if (!m_Mips.empty())
        return true;
    if (m_DeferredPayloadPath.empty())
        return false;

    try {
        auto input = RuntimeFileSystem::Get().OpenRead(m_DeferredPayloadPath);
        if (!input)
            return false;
        std::istream& in = *input;
        uint32_t magic = 0;
        uint32_t version = 0;
        TextureDesc desc;
        std::vector<uint8_t> pixels;
        std::vector<TextureMipData> mips;
        if (!ReadPod(in, magic) || !ReadPod(in, version) || magic != kTexturePayloadMagic || version == 0 ||
            version > kTexturePayloadVersion || !ReadTextureDesc(in, desc) || !ReadVector(in, pixels) ||
            !ReadMipChain(in, mips, version)) {
            return false;
        }
        SetPixelDataWithMips(std::move(pixels), desc, std::move(mips));
        return true;
    } catch (...) {
        return false;
    }
}

void TextureAsset::RebuildDerivedData() {
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
        m_Mips.push_back(level);
        if (level.width == 1 && level.height == 1)
            break;

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
                            sum += level.rgba8[static_cast<size_t>((sourceY * level.width + sourceX) * 4 + channel)];
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
    if (m_Desc.generateCompressedMips)
        GenerateCompressedMips();
}

void TextureAsset::GenerateCompressedMips() {
    for (TextureMipData& level : m_Mips) {
        if (level.bc1.empty() && !level.rgba8.empty() && level.width > 0 && level.height > 0) {
            level.bc1 = CompressBc1(level.rgba8, level.width, level.height);
        }
        if (level.bc3.empty() && !level.rgba8.empty() && level.width > 0 && level.height > 0) {
            level.bc3 = CompressBc3(level.rgba8, level.width, level.height);
        }
    }
}
