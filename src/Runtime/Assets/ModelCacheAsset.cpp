#include "Assets/ModelCacheAsset.h"

#include "Assets/AssetManager.h"
#include "Core/Logger.h"
#include "Core/RuntimeFileSystem.h"

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <type_traits>

namespace {

constexpr uint32_t kModelCacheMagic = 0x4d454d43; // CMEM / cached MyEngine model
constexpr uint32_t kModelCacheVersion = 4;
constexpr uint32_t kMaxStringBytes = 1u << 20;
constexpr uint32_t kMaxArrayItems = 64u << 20;

using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct ModelCacheLoadStats {
    uint32_t textures = 0;
    uint32_t deferredTextures = 0;
    uint64_t textureBytes = 0;
    double textureRebuildMs = 0.0;
};

enum class TextureCacheStorage : uint8_t {
    Embedded = 0,
    ExternalPayload = 1,
};

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

bool WriteString(std::ostream& out, const std::string& value) {
    if (value.size() > kMaxStringBytes)
        return false;
    const uint32_t size = static_cast<uint32_t>(value.size());
    if (!WritePod(out, size))
        return false;
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(out);
}

bool ReadString(std::istream& in, std::string& value) {
    uint32_t size = 0;
    if (!ReadPod(in, size) || size > kMaxStringBytes)
        return false;
    value.assign(size, '\0');
    if (size == 0)
        return true;
    in.read(value.data(), static_cast<std::streamsize>(size));
    return static_cast<bool>(in);
}

template <typename T> bool WriteVector(std::ostream& out, const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (values.size() > kMaxArrayItems)
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
    if (!ReadPod(in, size) || size > kMaxArrayItems)
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
    if (mips.size() > kMaxArrayItems || !WritePod(out, static_cast<uint32_t>(mips.size()))) {
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

bool ReadMipChain(std::istream& in, std::vector<TextureMipData>& mips, uint32_t cacheVersion) {
    uint32_t mipCount = 0;
    if (!ReadPod(in, mipCount) || mipCount > kMaxArrayItems)
        return false;
    mips.resize(mipCount);
    for (TextureMipData& mip : mips) {
        if (!ReadPod(in, mip.width) || !ReadPod(in, mip.height) || !ReadVector(in, mip.rgba8) ||
            !ReadVector(in, mip.bc1)) {
            return false;
        }
        if (cacheVersion >= 4 && !ReadVector(in, mip.bc3))
            return false;
    }
    return true;
}

bool WriteEmbeddedTexturePayload(std::ostream& out, const TextureAsset& texture) {
    return WriteVector(out, texture.GetPixelData()) && WriteMipChain(out, texture.GetMips());
}

bool WriteTexture(std::ostream& out, const TextureAsset& texture, const std::filesystem::path& modelCacheDirectory,
                  const std::filesystem::path& payloadDirectory, uint32_t& texturePayloadIndex) {
    TextureAsset cookedTexture(texture.GetPath());
    cookedTexture.SetName(texture.GetName());
    cookedTexture.SetPixelDataWithMips(texture.GetPixelData(), texture.GetDesc(), texture.GetMips());
    cookedTexture.GenerateCompressedMips();

    TextureDesc cookedDesc = texture.GetDesc();
    std::vector<TextureMipData> cookedMips;
    cookedMips.reserve(cookedTexture.GetMips().size());
    for (const TextureMipData& mip : cookedTexture.GetMips()) {
        TextureMipData cookedMip;
        cookedMip.width = mip.width;
        cookedMip.height = mip.height;
        cookedMip.bc3 = mip.bc3;
        cookedMips.push_back(std::move(cookedMip));
    }
    cookedDesc.format = TextureFormat::BC3;
    cookedDesc.generateCompressedMips = false;
    cookedDesc.mipLevels = static_cast<int>(cookedMips.size());

    TextureAsset payloadTexture(texture.GetPath());
    payloadTexture.SetName(texture.GetName());
    payloadTexture.SetPixelDataWithMips({}, cookedDesc, std::move(cookedMips));

    if (!WriteString(out, payloadTexture.GetPath()) || !WriteString(out, payloadTexture.GetName()) ||
        !WriteTextureDesc(out, payloadTexture.GetDesc())) {
        return false;
    }

    const std::filesystem::path payloadPath =
        payloadDirectory / ("texture-" + std::to_string(texturePayloadIndex++) + ".texturebin");
    if (SaveTexturePayloadToFile(payloadTexture, payloadPath.string())) {
        const TextureCacheStorage storage = TextureCacheStorage::ExternalPayload;
        std::error_code ec;
        std::filesystem::path relative = std::filesystem::relative(payloadPath, modelCacheDirectory, ec);
        if (ec || relative.empty())
            relative = payloadPath.filename();
        return WritePod(out, storage) && WriteString(out, relative.generic_string());
    }

    const TextureCacheStorage storage = TextureCacheStorage::Embedded;
    return WritePod(out, storage) && WriteEmbeddedTexturePayload(out, payloadTexture);
}

bool ReadEmbeddedTexturePayload(std::istream& in, const TextureDesc& desc, std::shared_ptr<TextureAsset>& texture,
                                ModelCacheLoadStats* stats, uint32_t cacheVersion) {
    std::vector<uint8_t> pixels;
    if (!ReadVector(in, pixels))
        return false;
    const uint64_t pixelBytes = static_cast<uint64_t>(pixels.size());
    std::vector<TextureMipData> mips;
    if (cacheVersion >= 2 && !ReadMipChain(in, mips, cacheVersion))
        return false;

    const auto rebuildStart = Clock::now();
    if (cacheVersion >= 2) {
        texture->SetPixelDataWithMips(std::move(pixels), desc, std::move(mips));
    } else {
        texture->SetPixelData(std::move(pixels), desc);
    }
    if (stats) {
        ++stats->textures;
        stats->textureBytes += pixelBytes;
        stats->textureRebuildMs += ElapsedMs(rebuildStart);
    }
    return true;
}

bool ReadExternalTexturePayload(std::istream& in, const std::filesystem::path& modelCacheDirectory,
                                const TextureDesc& desc, std::shared_ptr<TextureAsset>& texture,
                                ModelCacheLoadStats* stats) {
    std::string relativePayloadPath;
    if (!ReadString(in, relativePayloadPath) || relativePayloadPath.empty())
        return false;
    const std::filesystem::path payloadPath =
        std::filesystem::absolute(modelCacheDirectory / std::filesystem::path(relativePayloadPath)).lexically_normal();
    if (!RuntimeFileSystem::Get().Exists(payloadPath.string()))
        return false;
    texture->SetDeferredPayload(payloadPath.string(), desc);
    if (stats) {
        ++stats->textures;
        ++stats->deferredTextures;
    }
    return true;
}

bool ReadTexture(std::istream& in, const std::string& fallbackPath, const std::filesystem::path& modelCacheDirectory,
                 std::shared_ptr<TextureAsset>& texture, ModelCacheLoadStats* stats, uint32_t cacheVersion) {
    std::string originalPath;
    std::string name;
    if (!ReadString(in, originalPath) || !ReadString(in, name))
        return false;
    TextureDesc desc;
    if (!ReadTextureDesc(in, desc))
        return false;

    texture = std::make_shared<TextureAsset>(originalPath.empty() ? fallbackPath : originalPath);
    texture->SetName(name);

    if (cacheVersion >= 3) {
        TextureCacheStorage storage = TextureCacheStorage::Embedded;
        if (!ReadPod(in, storage))
            return false;
        switch (storage) {
        case TextureCacheStorage::Embedded:
            return ReadEmbeddedTexturePayload(in, desc, texture, stats, cacheVersion);
        case TextureCacheStorage::ExternalPayload:
            return ReadExternalTexturePayload(in, modelCacheDirectory, desc, texture, stats);
        default:
            return false;
        }
    }

    return ReadEmbeddedTexturePayload(in, desc, texture, stats, cacheVersion);
}

bool WriteMaterial(std::ostream& out, const MaterialAsset& material, const std::filesystem::path& modelCacheDirectory,
                   const std::filesystem::path& payloadDirectory, uint32_t& texturePayloadIndex) {
    if (!WriteString(out, material.GetPath()) || !WriteString(out, material.GetName()) ||
        !WritePod(out, material.GetBlendMode()) || !WritePod(out, material.IsTwoSided()) ||
        !WritePod(out, material.IsWireframe()) || !WritePod(out, material.GetAlphaThreshold())) {
        return false;
    }

    const auto& params = material.GetParams();
    if (params.size() > kMaxArrayItems || !WritePod(out, static_cast<uint32_t>(params.size()))) {
        return false;
    }
    for (const auto& [name, param] : params) {
        if (!WriteString(out, name) || !WritePod(out, param.type) || !WritePod(out, param.data)) {
            return false;
        }
    }

    const auto& textures = material.GetTextures();
    if (textures.size() > kMaxArrayItems || !WritePod(out, static_cast<uint32_t>(textures.size()))) {
        return false;
    }
    for (const auto& [slot, texture] : textures) {
        if (!WriteString(out, slot))
            return false;
        const bool hasTexture = texture.IsValid();
        if (!WritePod(out, hasTexture))
            return false;
        if (hasTexture &&
            !WriteTexture(out, *texture.Get(), modelCacheDirectory, payloadDirectory, texturePayloadIndex)) {
            return false;
        }
    }
    return true;
}

bool ReadMaterial(std::istream& in, const std::string& modelPath, const std::filesystem::path& modelCacheDirectory,
                  uint32_t materialIndex, MaterialHandle& material, ModelCacheLoadStats* stats, uint32_t cacheVersion) {
    std::string originalPath;
    std::string name;
    BlendMode blendMode = BlendMode::Opaque;
    bool twoSided = false;
    bool wireframe = false;
    float alphaThreshold = 0.5f;
    if (!ReadString(in, originalPath) || !ReadString(in, name) || !ReadPod(in, blendMode) || !ReadPod(in, twoSided) ||
        !ReadPod(in, wireframe) || !ReadPod(in, alphaThreshold)) {
        return false;
    }

    const std::string materialPath = modelPath + "#material-" + std::to_string(materialIndex);
    auto materialAsset = std::make_shared<MaterialAsset>(materialPath);
    materialAsset->SetName(name);
    materialAsset->SetBlendMode(blendMode);
    materialAsset->SetTwoSided(twoSided);
    materialAsset->SetWireframe(wireframe);
    materialAsset->SetAlphaThreshold(alphaThreshold);

    uint32_t paramCount = 0;
    if (!ReadPod(in, paramCount) || paramCount > kMaxArrayItems)
        return false;
    for (uint32_t i = 0; i < paramCount; ++i) {
        std::string paramName;
        MaterialParam param;
        if (!ReadString(in, paramName) || !ReadPod(in, param.type) || !ReadPod(in, param.data)) {
            return false;
        }
        materialAsset->SetParam(paramName, param);
    }

    uint32_t textureCount = 0;
    if (!ReadPod(in, textureCount) || textureCount > kMaxArrayItems)
        return false;
    for (uint32_t i = 0; i < textureCount; ++i) {
        std::string slot;
        bool hasTexture = false;
        if (!ReadString(in, slot) || !ReadPod(in, hasTexture))
            return false;
        if (!hasTexture)
            continue;
        std::shared_ptr<TextureAsset> texture;
        const std::string texturePath = modelPath + "#texture-" + std::to_string(materialIndex) + "-" + slot;
        if (!ReadTexture(in, texturePath, modelCacheDirectory, texture, stats, cacheVersion)) {
            return false;
        }
        materialAsset->SetTexture(slot, AssetManager::Get().Register(std::move(texture)));
    }
    materialAsset->MarkReady();
    material = AssetManager::Get().Register(std::move(materialAsset));
    return material.IsValid();
}

bool WriteModelNode(std::ostream& out, const ModelNode& node) {
    const bool hasMesh = node.mesh.IsValid();
    if (!WriteString(out, node.name) || !WritePod(out, node.localTransform) || !WritePod(out, hasMesh) ||
        !WritePod(out, node.materialSlot)) {
        return false;
    }
    return WriteVector(out, node.children);
}

bool ReadModelNode(std::istream& in, ModelNode& node, MeshHandle mesh) {
    bool hasMesh = false;
    if (!ReadString(in, node.name) || !ReadPod(in, node.localTransform) || !ReadPod(in, hasMesh) ||
        !ReadPod(in, node.materialSlot) || !ReadVector(in, node.children)) {
        return false;
    }
    if (hasMesh)
        node.mesh = mesh;
    return true;
}

bool WriteBones(std::ostream& out, const std::vector<Bone>& bones) {
    if (bones.size() > kMaxArrayItems || !WritePod(out, static_cast<uint32_t>(bones.size()))) {
        return false;
    }
    for (const Bone& bone : bones) {
        if (!WriteString(out, bone.name) || !WritePod(out, bone.parent) || !WritePod(out, bone.inverseBind) ||
            !WritePod(out, bone.bindTranslation) || !WritePod(out, bone.bindRotation) ||
            !WritePod(out, bone.bindScale)) {
            return false;
        }
    }
    return true;
}

bool ReadBones(std::istream& in, std::vector<Bone>& bones) {
    uint32_t count = 0;
    if (!ReadPod(in, count) || count > kMaxArrayItems)
        return false;
    bones.resize(count);
    for (Bone& bone : bones) {
        if (!ReadString(in, bone.name) || !ReadPod(in, bone.parent) || !ReadPod(in, bone.inverseBind) ||
            !ReadPod(in, bone.bindTranslation) || !ReadPod(in, bone.bindRotation) || !ReadPod(in, bone.bindScale)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool SaveModelCacheAssetToFile(const ModelAsset& model, const std::string& path) {
    const MeshAsset* mesh = model.GetMeshPtr();
    if (!mesh)
        return false;
    try {
        const std::filesystem::path outputPath(path);
        std::filesystem::create_directories(outputPath.parent_path());
        const std::filesystem::path outputDirectory = outputPath.parent_path();
        const std::filesystem::path payloadDirectory = outputDirectory / (outputPath.filename().string() + "_textures");
        std::error_code cleanupError;
        std::filesystem::remove_all(payloadDirectory, cleanupError);
        std::filesystem::create_directories(payloadDirectory);
        const std::string temporary = path + ".tmp";
        uint32_t texturePayloadIndex = 0;
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            if (!WritePod(out, kModelCacheMagic) || !WritePod(out, kModelCacheVersion) ||
                !WriteString(out, model.GetPath()) || !WriteString(out, model.GetName()) ||
                !WriteVector(out, mesh->GetVertices()) || !WriteVector(out, mesh->GetIndices())) {
                return false;
            }
            const auto& subMeshes = mesh->GetSubMeshes();
            if (subMeshes.size() > kMaxArrayItems || !WritePod(out, static_cast<uint32_t>(subMeshes.size()))) {
                return false;
            }
            for (const SubMesh& subMesh : subMeshes) {
                if (!WritePod(out, subMesh.indexOffset) || !WritePod(out, subMesh.indexCount) ||
                    !WritePod(out, subMesh.vertexOffset) || !WritePod(out, subMesh.materialSlot) ||
                    !WriteString(out, subMesh.name)) {
                    return false;
                }
            }

            const auto& materials = model.GetMaterials();
            if (materials.size() > kMaxArrayItems || !WritePod(out, static_cast<uint32_t>(materials.size()))) {
                return false;
            }
            for (const MaterialHandle& material : materials) {
                const bool hasMaterial = material.IsValid();
                if (!WritePod(out, hasMaterial))
                    return false;
                if (hasMaterial &&
                    !WriteMaterial(out, *material.Get(), outputDirectory, payloadDirectory, texturePayloadIndex)) {
                    return false;
                }
            }

            const auto& nodes = model.GetNodes();
            if (nodes.size() > kMaxArrayItems || !WritePod(out, static_cast<uint32_t>(nodes.size())) ||
                !WritePod(out, model.GetRootIndex())) {
                return false;
            }
            for (const ModelNode& node : nodes) {
                if (!WriteModelNode(out, node))
                    return false;
            }

            if (!WriteBones(out, model.GetBones()) || !WriteVector(out, model.GetSkinWeights())) {
                return false;
            }
            const auto& clips = model.GetAnimations();
            if (clips.size() > kMaxArrayItems || !WritePod(out, static_cast<uint32_t>(clips.size()))) {
                return false;
            }
            for (const AnimationClip& clip : clips) {
                if (!WriteString(out, clip.name) || !WritePod(out, clip.duration) || !WritePod(out, clip.looping)) {
                    return false;
                }
                const auto& tracks = clip.tracks;
                if (tracks.size() > kMaxArrayItems || !WritePod(out, static_cast<uint32_t>(tracks.size()))) {
                    return false;
                }
                for (const BoneTrack& track : tracks) {
                    if (!WritePod(out, track.boneIndex) || !WriteVector(out, track.keys)) {
                        return false;
                    }
                }
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

std::shared_ptr<ModelAsset> LoadModelCacheAssetFromFile(const std::string& path) {
    try {
        const auto loadStart = Clock::now();
        const uint64_t fileBytes = RuntimeFileSystem::Get().FileSize(path);
        const std::filesystem::path modelCacheDirectory =
            std::filesystem::absolute(std::filesystem::path(path).parent_path()).lexically_normal();
        auto input = RuntimeFileSystem::Get().OpenRead(path);
        if (!input)
            return {};
        std::istream& in = *input;
        uint32_t magic = 0;
        uint32_t version = 0;
        if (!ReadPod(in, magic) || !ReadPod(in, version) || magic != kModelCacheMagic || version == 0 ||
            version > kModelCacheVersion) {
            return {};
        }

        std::string originalPath;
        std::string name;
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
        if (!ReadString(in, originalPath) || !ReadString(in, name) || !ReadVector(in, vertices) ||
            !ReadVector(in, indices)) {
            return {};
        }

        uint32_t subMeshCount = 0;
        if (!ReadPod(in, subMeshCount) || subMeshCount > kMaxArrayItems)
            return {};
        std::vector<SubMesh> subMeshes(subMeshCount);
        for (SubMesh& subMesh : subMeshes) {
            if (!ReadPod(in, subMesh.indexOffset) || !ReadPod(in, subMesh.indexCount) ||
                !ReadPod(in, subMesh.vertexOffset) || !ReadPod(in, subMesh.materialSlot) ||
                !ReadString(in, subMesh.name)) {
                return {};
            }
        }

        auto mesh = std::make_shared<MeshAsset>(path + "#mesh");
        mesh->SetName(name);
        mesh->SetGeometry(std::move(vertices), std::move(indices), std::move(subMeshes));
        MeshHandle meshHandle = AssetManager::Get().Register(std::move(mesh));
        if (!meshHandle)
            return {};

        std::vector<MaterialHandle> materials;
        ModelCacheLoadStats stats;
        uint32_t materialCount = 0;
        if (!ReadPod(in, materialCount) || materialCount > kMaxArrayItems)
            return {};
        materials.reserve(materialCount);
        for (uint32_t i = 0; i < materialCount; ++i) {
            bool hasMaterial = false;
            if (!ReadPod(in, hasMaterial))
                return {};
            if (!hasMaterial) {
                materials.push_back({});
                continue;
            }
            MaterialHandle material;
            if (!ReadMaterial(in, path, modelCacheDirectory, i, material, &stats, version)) {
                return {};
            }
            materials.push_back(std::move(material));
        }

        uint32_t nodeCount = 0;
        int rootIndex = 0;
        if (!ReadPod(in, nodeCount) || nodeCount > kMaxArrayItems || !ReadPod(in, rootIndex)) {
            return {};
        }
        std::vector<ModelNode> nodes(nodeCount);
        for (ModelNode& node : nodes) {
            if (!ReadModelNode(in, node, meshHandle))
                return {};
        }

        std::vector<Bone> bones;
        std::vector<SkinWeight> skinWeights;
        if (!ReadBones(in, bones) || !ReadVector(in, skinWeights))
            return {};

        uint32_t clipCount = 0;
        if (!ReadPod(in, clipCount) || clipCount > kMaxArrayItems)
            return {};
        std::vector<AnimationClip> clips;
        clips.reserve(clipCount);
        for (uint32_t i = 0; i < clipCount; ++i) {
            AnimationClip clip;
            if (!ReadString(in, clip.name) || !ReadPod(in, clip.duration) || !ReadPod(in, clip.looping)) {
                return {};
            }
            uint32_t trackCount = 0;
            if (!ReadPod(in, trackCount) || trackCount > kMaxArrayItems)
                return {};
            clip.tracks.resize(trackCount);
            for (BoneTrack& track : clip.tracks) {
                if (!ReadPod(in, track.boneIndex) || !ReadVector(in, track.keys)) {
                    return {};
                }
            }
            clips.push_back(std::move(clip));
        }

        auto model = std::make_shared<ModelAsset>(path);
        model->SetName(name);
        model->SetMesh(meshHandle);
        model->SetMaterials(std::move(materials));
        model->SetNodes(std::move(nodes), rootIndex);
        model->SetSkin(std::move(bones), std::move(skinWeights));
        model->SetAnimations(std::move(clips));
        Logger::Info("[AssetCache] model cache hit: ", path, " version=", version,
                     " fileMB=", fileBytes / (1024.0 * 1024.0), " loadMs=", ElapsedMs(loadStart),
                     " textures=", stats.textures, " deferredTextures=", stats.deferredTextures,
                     " textureMB=", stats.textureBytes / (1024.0 * 1024.0),
                     " textureRebuildMs=", stats.textureRebuildMs, " vertices=", model->GetMesh()->VertexCount(),
                     " materials=", model->MaterialCount());
        return model;
    } catch (...) {
        return {};
    }
}
