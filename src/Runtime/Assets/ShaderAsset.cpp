#include "Assets/ShaderAsset.h"

#include "Core/Logger.h"
#include "Core/RuntimeFileSystem.h"
#include "Core/TransactionalFileWriter.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_set>

namespace {
constexpr char kMagic[8] = {'M', 'Y', 'S', 'H', 'D', 'R', '0', '1'};
constexpr size_t kPassCount = static_cast<size_t>(ShaderPass::Count);

uint64_t HashBytes(const void* data, size_t size, uint64_t hash = 14695981039346656037ULL) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}
uint64_t HashText(const std::string& text, uint64_t hash = 14695981039346656037ULL) {
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

bool ReadEnumText(const nlohmann::json& data, const char* key, ShaderShadingModel& value) {
    const std::string text = data.value(key, std::string("Lit"));
    if (text == "Lit")
        value = ShaderShadingModel::Lit;
    else if (text == "Unlit")
        value = ShaderShadingModel::Unlit;
    else
        return false;
    return true;
}

bool ReadEnumText(const nlohmann::json& data, const char* key, ShaderSurfaceType& value) {
    const std::string text = data.value(key, std::string("Opaque"));
    if (text == "Opaque")
        value = ShaderSurfaceType::Opaque;
    else if (text == "Masked")
        value = ShaderSurfaceType::Masked;
    else if (text == "Transparent")
        value = ShaderSurfaceType::Transparent;
    else
        return false;
    return true;
}

bool ParseStages(const nlohmann::json& stages, std::array<ShaderStageSource, kShaderStageCount>& sources,
                 uint32_t& mask) {
    if (!stages.is_object())
        return false;
    for (auto it = stages.begin(); it != stages.end(); ++it)
        if (it.key() != "vertex" && it.key() != "pixel" && it.key() != "compute")
            return false;
    const std::pair<const char*, ShaderStage> names[] = {
        {"vertex", ShaderStage::Vertex}, {"pixel", ShaderStage::Pixel}, {"compute", ShaderStage::Compute}};
    mask = 0;
    for (const auto& [name, stage] : names) {
        const auto it = stages.find(name);
        if (it == stages.end())
            continue;
        if (!it->is_object())
            return false;
        auto& destination = sources[static_cast<size_t>(stage)];
        destination.source = it->value("source", std::string{});
        destination.entry = it->value("entry", std::string{});
        if (!SafeRelative(destination.source) || destination.entry.empty())
            return false;
        mask |= 1u << static_cast<uint32_t>(stage);
    }
    return mask == ShaderAsset::kComputeMask || mask == (ShaderAsset::kVertexMask | ShaderAsset::kPixelMask);
}

nlohmann::json SerializeCookedReflection(const ShaderAsset& shader) {
    nlohmann::json entries = nlohmann::json::array();
    for (size_t backend = 0; backend < kShaderBackendCount; ++backend) {
        for (size_t pass = 0; pass < kPassCount; ++pass) {
            for (size_t stage = 0; stage < kShaderStageCount; ++stage) {
                const auto& reflection =
                    shader.GetReflection(static_cast<ShaderBackend>(backend), static_cast<ShaderPass>(pass),
                                         static_cast<ShaderStage>(stage));
                if (reflection.bindings.empty() && reflection.threadGroupSize[0] == 1 &&
                    reflection.threadGroupSize[1] == 1 && reflection.threadGroupSize[2] == 1)
                    continue;
                nlohmann::json bindings = nlohmann::json::array();
                for (const auto& binding : reflection.bindings) {
                    bindings.push_back({{"name", binding.name},
                                        {"type", static_cast<uint8_t>(binding.type)},
                                        {"bindPoint", binding.bindPoint},
                                        {"bindSpace", binding.bindSpace},
                                        {"bindCount", binding.bindCount},
                                        {"byteSize", binding.byteSize}});
                }
                entries.push_back(
                    {{"backend", backend},
                     {"pass", pass},
                     {"stage", stage},
                     {"threadGroupSize",
                      {reflection.threadGroupSize[0], reflection.threadGroupSize[1], reflection.threadGroupSize[2]}},
                     {"bindings", std::move(bindings)}});
            }
        }
    }
    return entries;
}

bool ParseCookedReflection(const nlohmann::json& entries, CookedShaderReflectionTable& reflection) {
    if (!entries.is_array())
        return false;
    for (const auto& entry : entries) {
        if (!entry.is_object())
            return false;
        const size_t backend = entry.value("backend", kShaderBackendCount);
        const size_t pass = entry.value("pass", kPassCount);
        const size_t stage = entry.value("stage", kShaderStageCount);
        if (backend >= kShaderBackendCount || pass >= kPassCount || stage >= kShaderStageCount)
            return false;
        auto& destination = reflection[backend][pass][stage];
        const auto threads = entry.value("threadGroupSize", nlohmann::json::array({1, 1, 1}));
        if (!threads.is_array() || threads.size() != 3)
            return false;
        for (size_t axis = 0; axis < 3; ++axis)
            destination.threadGroupSize[axis] = threads[axis].get<uint32_t>();
        const auto bindings = entry.value("bindings", nlohmann::json::array());
        if (!bindings.is_array())
            return false;
        for (const auto& item : bindings) {
            const uint32_t type = item.value("type", UINT32_MAX);
            if (!item.is_object() || type > static_cast<uint32_t>(CookedShaderBindingType::StorageTexture))
                return false;
            CookedShaderBinding binding;
            binding.name = item.value("name", std::string{});
            binding.type = static_cast<CookedShaderBindingType>(type);
            binding.bindPoint = item.value("bindPoint", 0u);
            binding.bindSpace = item.value("bindSpace", 0u);
            binding.bindCount = item.value("bindCount", 1u);
            binding.byteSize = item.value("byteSize", 0u);
            if (binding.name.empty() || binding.bindCount == 0)
                return false;
            destination.bindings.push_back(std::move(binding));
        }
    }
    return true;
}

std::shared_ptr<ShaderAsset> LoadLegacyCooked(const std::string& path, std::istream& input, uint32_t version) {
    uint32_t mask = 0;
    uint64_t sourceHash = 0;
    if (!Read(input, mask) || !Read(input, sourceHash) ||
        (mask != ShaderAsset::kComputeMask && mask != (ShaderAsset::kVertexMask | ShaderAsset::kPixelMask)))
        return {};
    std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, kShaderBackendCount> blobs{};
    const size_t storedBackendCount =
        version == ShaderAsset::kLegacyCookedFormatVersion
            ? 2
            : (version == ShaderAsset::kCookedFormatVersionWithMetal ? 3 : kShaderBackendCount);
    for (size_t backend = 0; backend < storedBackendCount; ++backend) {
        for (size_t stage = 0; stage < kShaderStageCount; ++stage) {
            uint64_t size = 0;
            if (!Read(input, size) || size > (256ull << 20))
                return {};
            blobs[backend][stage].resize(static_cast<size_t>(size));
            if (size &&
                !input.read(reinterpret_cast<char*>(blobs[backend][stage].data()), static_cast<std::streamsize>(size)))
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

std::shared_ptr<ShaderAsset> LoadCooked(const std::string& path, std::istream& input) {
    uint32_t version = 0;
    if (!Read(input, version) || version < ShaderAsset::kLegacyCookedFormatVersion ||
        version > ShaderAsset::kCookedFormatVersion)
        return {};
    if (version < ShaderAsset::kCookedFormatVersionWithPasses)
        return LoadLegacyCooked(path, input, version);

    uint32_t passMask = 0;
    uint64_t sourceHash = 0;
    uint8_t sourceMode = 0, domain = 0, shadingModel = 0, surfaceType = 0;
    uint64_t metadataSize = 0;
    if (!Read(input, passMask) || !Read(input, sourceHash) || !Read(input, sourceMode) || !Read(input, domain) ||
        !Read(input, shadingModel) || !Read(input, surfaceType) || !Read(input, metadataSize) ||
        metadataSize > (16ull << 20))
        return {};
    if (sourceMode > static_cast<uint8_t>(ShaderSourceMode::Graph) ||
        domain > static_cast<uint8_t>(ShaderDomain::Compute) ||
        shadingModel > static_cast<uint8_t>(ShaderShadingModel::Unlit) ||
        surfaceType > static_cast<uint8_t>(ShaderSurfaceType::Transparent) || passMask == 0 ||
        (passMask & ~((1u << static_cast<uint32_t>(ShaderPass::Count)) - 1u)) != 0)
        return {};
    std::string metadata(static_cast<size_t>(metadataSize), '\0');
    if (metadataSize && !input.read(metadata.data(), static_cast<std::streamsize>(metadataSize)))
        return {};
    std::vector<ShaderPropertyDesc> properties;
    CookedShaderReflectionTable reflection{};
    try {
        const nlohmann::json json = metadata.empty() ? nlohmann::json::object() : nlohmann::json::parse(metadata);
        if (!ParseShaderProperties(json.value("properties", nlohmann::json::array()), properties))
            return {};
        if (version >= ShaderAsset::kCookedFormatVersionWithReflection &&
            (json.value("abiVersion", 0u) != ShaderAsset::kCookedShaderAbiVersion ||
             !ParseCookedReflection(json.value("reflection", nlohmann::json::array()), reflection)))
            return {};
    } catch (...) {
        return {};
    }

    std::array<std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, kPassCount>, kShaderBackendCount>
        blobs{};
    for (size_t backend = 0; backend < kShaderBackendCount; ++backend) {
        for (size_t pass = 0; pass < kPassCount; ++pass) {
            for (size_t stage = 0; stage < kShaderStageCount; ++stage) {
                uint64_t size = 0;
                if (!Read(input, size) || size > (256ull << 20))
                    return {};
                blobs[backend][pass][stage].resize(static_cast<size_t>(size));
                if (size && !input.read(reinterpret_cast<char*>(blobs[backend][pass][stage].data()),
                                        static_cast<std::streamsize>(size)))
                    return {};
            }
        }
    }
    if (input.peek() != std::char_traits<char>::eof())
        return {};
    auto asset = std::make_shared<ShaderAsset>(path);
    asset->SetCookedPasses(
        passMask, sourceHash, static_cast<ShaderSourceMode>(sourceMode), static_cast<ShaderDomain>(domain),
        static_cast<ShaderShadingModel>(shadingModel), static_cast<ShaderSurfaceType>(surfaceType),
        std::move(properties), std::move(blobs), std::move(reflection),
        version >= ShaderAsset::kCookedFormatVersionWithReflection ? ShaderAsset::kCookedShaderAbiVersion : 0u);
    asset->MarkReady();
    return asset;
}
} // namespace

const ShaderPropertyDesc* ShaderAsset::FindPropertyById(const std::string& id) const {
    const auto found =
        std::find_if(m_Properties.begin(), m_Properties.end(), [&](const auto& property) { return property.id == id; });
    return found == m_Properties.end() ? nullptr : &*found;
}

const ShaderPropertyDesc* ShaderAsset::FindPropertyByName(const std::string& name) const {
    const auto found = std::find_if(m_Properties.begin(), m_Properties.end(),
                                    [&](const auto& property) { return property.name == name; });
    return found == m_Properties.end() ? nullptr : &*found;
}

std::filesystem::path ShaderAsset::ResolveSource(ShaderStage stage) const {
    return ResolveSource(ShaderPass::Default, stage);
}

std::filesystem::path ShaderAsset::ResolveSource(ShaderPass pass, ShaderStage stage) const {
    return (std::filesystem::path(GetPath()).parent_path() / GetPassStage(pass, stage).source).lexically_normal();
}

void ShaderAsset::SetDescription(uint32_t mask, std::array<ShaderStageSource, 3> sources,
                                 std::vector<std::string> defines, uint64_t hash) {
    m_Cooked = false;
    m_SourceMode = ShaderSourceMode::Code;
    m_Domain = mask == kComputeMask ? ShaderDomain::Compute : ShaderDomain::Graphics;
    m_StageMask = mask;
    m_PassMask = 1u << static_cast<uint32_t>(ShaderPass::Default);
    m_PassSources[static_cast<size_t>(ShaderPass::Default)] = std::move(sources);
    m_Defines = std::move(defines);
    m_SourceHash = hash;
}

void ShaderAsset::SetCodeDescription(
    uint32_t passMask,
    std::array<std::array<ShaderStageSource, kShaderStageCount>, static_cast<size_t>(ShaderPass::Count)> passSources,
    std::vector<std::string> defines, std::vector<ShaderPropertyDesc> properties, ShaderDomain domain, uint64_t hash) {
    m_Cooked = false;
    m_SourceMode = ShaderSourceMode::Code;
    m_Domain = domain;
    m_PassMask = passMask;
    m_StageMask = domain == ShaderDomain::Compute ? kComputeMask : (kVertexMask | kPixelMask);
    m_PassSources = std::move(passSources);
    m_Defines = std::move(defines);
    m_Properties = std::move(properties);
    m_SourceHash = hash;
}

void ShaderAsset::SetGraphDescription(ShaderGraph graph, std::vector<ShaderPropertyDesc> properties,
                                      ShaderShadingModel shadingModel, ShaderSurfaceType surfaceType, uint64_t hash,
                                      std::vector<ShaderGraphDiagnostic> diagnostics) {
    m_Cooked = false;
    m_SourceMode = ShaderSourceMode::Graph;
    m_Domain = ShaderDomain::Surface;
    m_ShadingModel = shadingModel;
    m_SurfaceType = surfaceType;
    m_StageMask = kVertexMask | kPixelMask;
    m_PassMask = 0;
    if (shadingModel == ShaderShadingModel::Lit && surfaceType != ShaderSurfaceType::Transparent)
        m_PassMask |= 1u << static_cast<uint32_t>(ShaderPass::GBuffer);
    else
        m_PassMask |= 1u << static_cast<uint32_t>(ShaderPass::Forward);
    if (surfaceType != ShaderSurfaceType::Transparent)
        m_PassMask |= 1u << static_cast<uint32_t>(ShaderPass::Shadow);
    m_Graph = std::move(graph);
    m_Properties = std::move(properties);
    m_Diagnostics = std::move(diagnostics);
    m_SourceHash = hash;
}

void ShaderAsset::SetCooked(
    uint32_t mask, uint64_t hash,
    std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, kShaderBackendCount> blobs) {
    m_Cooked = true;
    m_SourceMode = ShaderSourceMode::Code;
    m_Domain = mask == kComputeMask ? ShaderDomain::Compute : ShaderDomain::Graphics;
    m_StageMask = mask;
    m_PassMask = 1u << static_cast<uint32_t>(ShaderPass::Default);
    m_SourceHash = hash;
    for (size_t backend = 0; backend < kShaderBackendCount; ++backend)
        m_Bytecode[backend][static_cast<size_t>(ShaderPass::Default)] = std::move(blobs[backend]);
}

void ShaderAsset::SetCookedPasses(
    uint32_t passMask, uint64_t hash, ShaderSourceMode sourceMode, ShaderDomain domain, ShaderShadingModel shadingModel,
    ShaderSurfaceType surfaceType, std::vector<ShaderPropertyDesc> properties,
    std::array<std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, kPassCount>, kShaderBackendCount>
        bytecode,
    CookedShaderReflectionTable reflection, uint32_t abiVersion) {
    m_Cooked = true;
    m_SourceMode = sourceMode;
    m_Domain = domain;
    m_ShadingModel = shadingModel;
    m_SurfaceType = surfaceType;
    m_StageMask = domain == ShaderDomain::Compute ? kComputeMask : (kVertexMask | kPixelMask);
    m_PassMask = passMask;
    m_SourceHash = hash;
    m_Properties = std::move(properties);
    m_Bytecode = std::move(bytecode);
    m_Reflection = std::move(reflection);
    m_CookedShaderAbiVersion = abiVersion;
}

bool ShaderAsset::ReloadFrom(const Asset& source) {
    const auto* shader = dynamic_cast<const ShaderAsset*>(&source);
    if (!shader)
        return false;
    m_Cooked = shader->m_Cooked;
    m_SourceMode = shader->m_SourceMode;
    m_Domain = shader->m_Domain;
    m_ShadingModel = shader->m_ShadingModel;
    m_SurfaceType = shader->m_SurfaceType;
    m_StageMask = shader->m_StageMask;
    m_PassMask = shader->m_PassMask;
    m_SourceHash = shader->m_SourceHash;
    m_CookedShaderAbiVersion = shader->m_CookedShaderAbiVersion;
    m_PassSources = shader->m_PassSources;
    m_Defines = shader->m_Defines;
    m_Properties = shader->m_Properties;
    m_Graph = shader->m_Graph;
    m_Diagnostics = shader->m_Diagnostics;
    m_Bytecode = shader->m_Bytecode;
    m_Reflection = shader->m_Reflection;
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
        const std::string jsonText((std::istreambuf_iterator<char>(*input)), std::istreambuf_iterator<char>());
        const nlohmann::json data = nlohmann::json::parse(jsonText);
        if (data.value("type", std::string{}) != "Shader")
            return {};
        const uint32_t version = data.value("version", 0u);
        if (version != ShaderAsset::kLegacyDescriptionVersion && version != ShaderAsset::kDescriptionVersion)
            return {};
        auto asset = std::make_shared<ShaderAsset>(path);
        const uint64_t sourceHash = HashText(data.dump());
        if (version == ShaderAsset::kLegacyDescriptionVersion) {
            std::array<ShaderStageSource, kShaderStageCount> sources{};
            uint32_t mask = 0;
            if (!data.contains("stages") || !ParseStages(data["stages"], sources, mask))
                return {};
            std::vector<std::string> defines;
            if (data.contains("defines")) {
                if (!data["defines"].is_array())
                    return {};
                for (const auto& define : data["defines"])
                    if (define.is_string())
                        defines.push_back(define.get<std::string>());
                    else
                        return {};
            }
            asset->SetDescription(mask, std::move(sources), std::move(defines), sourceHash);
        } else {
            const std::string mode = data.value("mode", std::string("Code"));
            std::vector<ShaderPropertyDesc> properties;
            std::vector<ShaderGraphDiagnostic> diagnostics;
            if (!ParseShaderProperties(data.value("properties", nlohmann::json::array()), properties, &diagnostics))
                return {};
            if (mode == "Graph") {
                if (data.value("domain", std::string("Surface")) != "Surface")
                    return {};
                ShaderGraph graph;
                ShaderShadingModel shadingModel;
                ShaderSurfaceType surfaceType;
                const char* surfaceKey = data.contains("surfaceType") ? "surfaceType" : "surface";
                if (!data.contains("graph") || !ParseShaderGraph(data["graph"], graph, &diagnostics) ||
                    !ReadEnumText(data, "shadingModel", shadingModel) || !ReadEnumText(data, surfaceKey, surfaceType))
                    return {};
                asset->SetGraphDescription(std::move(graph), std::move(properties), shadingModel, surfaceType,
                                           sourceHash, std::move(diagnostics));
            } else if (mode == "Code") {
                std::array<std::array<ShaderStageSource, kShaderStageCount>, kPassCount> passSources{};
                uint32_t passMask = 0, stageMask = 0;
                if (data.contains("stages")) {
                    if (!ParseStages(data["stages"], passSources[static_cast<size_t>(ShaderPass::Default)], stageMask))
                        return {};
                    passMask |= 1u << static_cast<uint32_t>(ShaderPass::Default);
                }
                if (data.contains("passes")) {
                    if (!data["passes"].is_object())
                        return {};
                    const std::pair<const char*, ShaderPass> passNames[] = {{"GBuffer", ShaderPass::GBuffer},
                                                                            {"Forward", ShaderPass::Forward},
                                                                            {"Shadow", ShaderPass::Shadow}};
                    for (const auto& [name, pass] : passNames) {
                        const auto found = data["passes"].find(name);
                        if (found == data["passes"].end())
                            continue;
                        uint32_t mask = 0;
                        const auto& stages = found->contains("stages") ? (*found)["stages"] : *found;
                        if (!ParseStages(stages, passSources[static_cast<size_t>(pass)], mask) ||
                            mask != (ShaderAsset::kVertexMask | ShaderAsset::kPixelMask))
                            return {};
                        passMask |= 1u << static_cast<uint32_t>(pass);
                    }
                }
                if (passMask == 0)
                    return {};
                std::vector<std::string> defines;
                for (const auto& define : data.value("defines", nlohmann::json::array()))
                    if (define.is_string())
                        defines.push_back(define.get<std::string>());
                    else
                        return {};
                const std::string domainText = data.value("domain", std::string("Graphics"));
                const ShaderDomain domain = domainText == "Compute"   ? ShaderDomain::Compute
                                            : domainText == "Surface" ? ShaderDomain::Surface
                                                                      : ShaderDomain::Graphics;
                asset->SetCodeDescription(passMask, std::move(passSources), std::move(defines), std::move(properties),
                                          domain, sourceHash);
            } else {
                return {};
            }
        }
        asset->MarkReady();
        return asset;
    } catch (const std::exception& error) {
        Logger::Error("[ShaderAsset] ", path, ": ", error.what());
        return {};
    }
}

bool SaveCookedShaderAsset(const ShaderAsset& shader, const std::filesystem::path& path, std::string* error) {
    std::ostringstream output(std::ios::binary | std::ios::out);
    output.write(kMagic, sizeof(kMagic));
    Write(output, ShaderAsset::kCookedFormatVersion);
    Write(output, shader.GetPassMask());
    Write(output, shader.GetSourceHash());
    Write(output, static_cast<uint8_t>(shader.GetSourceMode()));
    Write(output, static_cast<uint8_t>(shader.GetDomain()));
    Write(output, static_cast<uint8_t>(shader.GetShadingModel()));
    Write(output, static_cast<uint8_t>(shader.GetSurfaceType()));
    const std::string metadata = nlohmann::json({{"properties", SerializeShaderProperties(shader.GetProperties())},
                                                 {"abiVersion", ShaderAsset::kCookedShaderAbiVersion},
                                                 {"reflection", SerializeCookedReflection(shader)}})
                                     .dump();
    const uint64_t metadataSize = metadata.size();
    Write(output, metadataSize);
    output.write(metadata.data(), static_cast<std::streamsize>(metadata.size()));
    for (size_t backend = 0; backend < kShaderBackendCount; ++backend) {
        for (size_t pass = 0; pass < kPassCount; ++pass) {
            for (size_t stage = 0; stage < kShaderStageCount; ++stage) {
                const auto& blob = shader.GetBytecode(static_cast<ShaderBackend>(backend),
                                                      static_cast<ShaderPass>(pass), static_cast<ShaderStage>(stage));
                const uint64_t size = blob.size();
                Write(output, size);
                if (size)
                    output.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(size));
            }
        }
    }
    if (!output) {
        if (error)
            *error = "failed writing cooked shader: " + path.string();
        return false;
    }
    const std::string bytes = output.str();
    TransactionalWriteOptions options;
    options.keepBackup = false;
    if (!TransactionalFileWriter::WriteText(path, bytes, options, error)) {
        if (error && error->empty())
            *error = "cannot atomically write cooked shader: " + path.string();
        return false;
    }
    return true;
}
