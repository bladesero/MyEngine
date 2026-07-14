#include "Assets/ShaderAsset.h"

#include "Core/Logger.h"
#include "Core/RuntimeFileSystem.h"
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {
constexpr char kMagic[8] = {'M', 'Y', 'S', 'H', 'D', 'R', '0', '1'};

uint64_t HashBytes(const void* data, size_t size, uint64_t hash = 14695981039346656037ULL) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}
uint64_t HashText(const std::string& text, uint64_t hash) {
    return HashBytes(text.data(), text.size(), hash);
}

bool SafeRelative(const std::string& value) {
    const std::filesystem::path path(value);
    if (value.empty() || path.is_absolute() || path.has_root_name())
        return false;
    for (const auto& part : path)
        if (part == "..")
            return false;
    return true;
}

template <class T> bool Read(std::istream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(value)));
}
template <class T> void Write(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

std::shared_ptr<ShaderAsset> LoadCooked(const std::string& path, std::istream& input) {
    uint32_t version = 0, mask = 0;
    uint64_t sourceHash = 0;
    if (!Read(input, version) ||
        (version != ShaderAsset::kLegacyCookedFormatVersion && version != ShaderAsset::kCookedFormatVersionWithMetal &&
         version != ShaderAsset::kCookedFormatVersion) ||
        !Read(input, mask) || !Read(input, sourceHash))
        return {};
    if (mask != ShaderAsset::kComputeMask && mask != (ShaderAsset::kVertexMask | ShaderAsset::kPixelMask))
        return {};
    std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, kShaderBackendCount> blobs{};
    const size_t storedBackendCount =
        version == ShaderAsset::kLegacyCookedFormatVersion
            ? 2
            : (version == ShaderAsset::kCookedFormatVersionWithMetal ? 3 : kShaderBackendCount);
    for (size_t backendIndex = 0; backendIndex < storedBackendCount; ++backendIndex) {
        auto& backend = blobs[backendIndex];
        for (size_t stage = 0; stage < kShaderStageCount; ++stage) {
            uint64_t size = 0;
            if (!Read(input, size) || size > (256ull << 20))
                return {};
            const uint32_t bit = 1u << stage;
            if ((mask & bit) == 0 && size != 0)
                return {};
            backend[stage].resize(static_cast<size_t>(size));
            if (size && !input.read(reinterpret_cast<char*>(backend[stage].data()), static_cast<std::streamsize>(size)))
                return {};
            if (version == ShaderAsset::kLegacyCookedFormatVersion && (mask & bit) != 0 && size == 0)
                return {};
        }
    }
    if (input.peek() != std::char_traits<char>::eof())
        return {};
    auto asset = std::make_shared<ShaderAsset>(path);
    asset->SetCooked(mask, sourceHash, std::move(blobs));
    asset->MarkReady();
    return asset;
}
} // namespace

std::filesystem::path ShaderAsset::ResolveSource(ShaderStage stage) const {
    return (std::filesystem::path(GetPath()).parent_path() / GetStage(stage).source).lexically_normal();
}

void ShaderAsset::SetDescription(uint32_t mask, std::array<ShaderStageSource, 3> sources,
                                 std::vector<std::string> defines, uint64_t hash) {
    m_Cooked = false;
    m_StageMask = mask;
    m_Sources = std::move(sources);
    m_Defines = std::move(defines);
    m_SourceHash = hash;
}
void ShaderAsset::SetCooked(
    uint32_t mask, uint64_t hash,
    std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, kShaderBackendCount> blobs) {
    m_Cooked = true;
    m_StageMask = mask;
    m_SourceHash = hash;
    m_Bytecode = std::move(blobs);
}
bool ShaderAsset::ReloadFrom(const Asset& source) {
    const auto* shader = dynamic_cast<const ShaderAsset*>(&source);
    if (!shader)
        return false;
    m_Cooked = shader->m_Cooked;
    m_StageMask = shader->m_StageMask;
    m_SourceHash = shader->m_SourceHash;
    m_Sources = shader->m_Sources;
    m_Defines = shader->m_Defines;
    m_Bytecode = shader->m_Bytecode;
    SetState(AssetState::Ready);
    return true;
}

std::shared_ptr<ShaderAsset> LoadShaderAssetFromFile(const std::string& path) {
    try {
        auto input = RuntimeFileSystem::Get().OpenRead(path);
        if (!input || !*input)
            return {};
        char magic[8]{};
        if (!input->read(magic, sizeof(magic)))
            return {};
        if (std::memcmp(magic, kMagic, sizeof(kMagic)) == 0)
            return LoadCooked(path, *input);
        input->clear();
        input->seekg(0);
        std::string jsonText((std::istreambuf_iterator<char>(*input)), std::istreambuf_iterator<char>());
        for (const char* stageName : {"\"vertex\"", "\"pixel\"", "\"compute\""}) {
            const size_t first = jsonText.find(stageName);
            if (first != std::string::npos &&
                jsonText.find(stageName, first + std::strlen(stageName)) != std::string::npos)
                return {};
        }
        nlohmann::json data = nlohmann::json::parse(jsonText);
        if (data.value("type", std::string()) != "Shader" ||
            data.value("version", 0u) != ShaderAsset::kDescriptionVersion)
            return {};
        const auto stages = data.find("stages");
        if (stages == data.end() || !stages->is_object())
            return {};
        for (auto it = stages->begin(); it != stages->end(); ++it)
            if (it.key() != "vertex" && it.key() != "pixel" && it.key() != "compute")
                return {};
        std::array<ShaderStageSource, 3> sources{};
        uint32_t mask = 0;
        const std::pair<const char*, ShaderStage> names[] = {
            {"vertex", ShaderStage::Vertex}, {"pixel", ShaderStage::Pixel}, {"compute", ShaderStage::Compute}};
        uint64_t hash = 14695981039346656037ULL;
        for (const auto& [name, stage] : names) {
            const auto it = stages->find(name);
            if (it == stages->end())
                continue;
            if (!it->is_object())
                return {};
            auto& dst = sources[static_cast<size_t>(stage)];
            dst.source = it->value("source", std::string());
            dst.entry = it->value("entry", std::string());
            if (!SafeRelative(dst.source) || dst.entry.empty())
                return {};
            mask |= 1u << static_cast<uint32_t>(stage);
            hash = HashText(name, hash);
            hash = HashText(dst.source, hash);
            hash = HashText(dst.entry, hash);
        }
        if (mask != ShaderAsset::kComputeMask && mask != (ShaderAsset::kVertexMask | ShaderAsset::kPixelMask))
            return {};
        std::vector<std::string> defines;
        if (data.contains("defines")) {
            if (!data["defines"].is_array())
                return {};
            for (const auto& item : data["defines"]) {
                if (!item.is_string())
                    return {};
                defines.push_back(item.get<std::string>());
                hash = HashText(defines.back(), hash);
            }
        }
        auto asset = std::make_shared<ShaderAsset>(path);
        asset->SetDescription(mask, std::move(sources), std::move(defines), hash);
        asset->MarkReady();
        return asset;
    } catch (const std::exception& e) {
        Logger::Error("[ShaderAsset] ", path, ": ", e.what());
        return {};
    }
}

bool SaveCookedShaderAsset(const ShaderAsset& shader, const std::filesystem::path& path, std::string* error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error)
            *error = "cannot write cooked shader: " + path.string();
        return false;
    }
    output.write(kMagic, sizeof(kMagic));
    Write(output, ShaderAsset::kCookedFormatVersion);
    Write(output, shader.GetStageMask());
    Write(output, shader.GetSourceHash());
    for (size_t backend = 0; backend < kShaderBackendCount; ++backend) {
        for (size_t stage = 0; stage < kShaderStageCount; ++stage) {
            const auto& blob = shader.GetBytecode(static_cast<ShaderBackend>(backend), static_cast<ShaderStage>(stage));
            const uint64_t size = blob.size();
            Write(output, size);
            if (size)
                output.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(size));
        }
    }
    if (!output) {
        if (error)
            *error = "failed writing cooked shader: " + path.string();
        return false;
    }
    return true;
}
