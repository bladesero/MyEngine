#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

class Sha256 {
public:
    using Digest = std::array<uint8_t, 32>;
    void Update(const void* data, size_t size);
    Digest Final();
    static std::string ToHex(const Digest& digest);
    static bool FromHex(const std::string& text, Digest& digest);
    static std::string HashFile(const std::filesystem::path& path, std::string* error = nullptr);

private:
    void Transform(const uint8_t block[64]);
    std::array<uint32_t, 8> m_State{
        {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u}};
    std::array<uint8_t, 64> m_Buffer{};
    uint64_t m_TotalBytes = 0;
    size_t m_BufferSize = 0;
    bool m_Finalized = false;
};
