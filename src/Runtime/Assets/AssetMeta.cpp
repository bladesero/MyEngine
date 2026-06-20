#include "Assets/AssetMeta.h"

#include "Core/Logger.h"

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace {

std::string GenerateUuid()
{
    std::array<uint8_t, 16> bytes{};
    std::random_device random;
    for (uint8_t& value : bytes) value = static_cast<uint8_t>(random());
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3f) | 0x80);

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        output << std::setw(2) << static_cast<unsigned>(bytes[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) output << '-';
    }
    return output.str();
}

} // namespace

std::string AssetMeta::MetaPathFor(const std::string& sourcePath)
{
    return sourcePath + ".meta";
}

AssetMeta AssetMeta::LoadOrCreate(const std::string& sourcePath)
{
    if (auto existing = Load(sourcePath)) return *existing;
    AssetMeta meta = Create(sourcePath);
    Save(meta);
    return meta;
}

std::optional<AssetMeta> AssetMeta::Load(const std::string& sourcePath,
                                         std::string* error)
{
    const std::filesystem::path normalized =
        std::filesystem::absolute(sourcePath).lexically_normal();
    const std::string metaPath = MetaPathFor(normalized.string());

    try {
        std::ifstream input(metaPath);
        if (input.is_open()) {
            const nlohmann::json json = nlohmann::json::parse(input);
            AssetMeta meta;
            meta.uuid = json.value("uuid", std::string{});
            meta.sourcePath = normalized.string();
            meta.importerVersion = json.value("importerVersion", 1u);
            if (!meta.uuid.empty()) return meta;
        }
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
    if (error) *error = "metadata is missing or invalid: " + metaPath;
    return std::nullopt;
}

AssetMeta AssetMeta::Create(const std::string& sourcePath)
{
    const std::filesystem::path normalized =
        std::filesystem::absolute(sourcePath).lexically_normal();
    AssetMeta meta;
    meta.uuid = GenerateUuid();
    meta.sourcePath = normalized.string();
    meta.importerVersion = 1;
    return meta;
}

bool AssetMeta::Save(const AssetMeta& meta)
{
    if (meta.uuid.empty() || meta.sourcePath.empty()) return false;
    try {
        nlohmann::json json;
        json["uuid"] = meta.uuid;
        json["source"] = std::filesystem::path(meta.sourcePath).filename().string();
        json["importerVersion"] = meta.importerVersion;
        const std::string path = MetaPathFor(meta.sourcePath);
        const std::string temporary = path + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << json.dump(2);
            output.flush();
            if (!output.good()) return false;
        }
        std::error_code error;
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        return !error;
    } catch (const std::exception& e) {
        Logger::Error("[AssetMeta] Failed to save metadata: ", e.what());
        return false;
    }
}
