#include "Assets/AssetManager.h"

#include "stb_image.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct ObjIndex {
    int v = -1;
    int vt = -1;
    int vn = -1;
};

struct MtlRecord {
    Vec3 diffuse = Vec3::One();
    float alpha = 1.0f;
    std::string diffuseTexture;
};

std::string StemFromPath(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

std::string NormalizeSeparators(std::string s) {
    for (char& c : s) {
        if (c == '\\')
            c = '/';
    }
    return s;
}

std::string NormalizePath(const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal().generic_string();
}

TextureFilter ParseTextureFilter(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "nearest" ? TextureFilter::Nearest : TextureFilter::Linear;
}

TextureWrap ParseTextureWrap(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "clamp" ? TextureWrap::Clamp : TextureWrap::Repeat;
}

void ApplyTextureImportSettings(const std::string& texturePath, TextureDesc& desc) {
    std::filesystem::path cursor = std::filesystem::absolute(std::filesystem::path(texturePath)).parent_path();
    while (!cursor.empty() && cursor != cursor.root_path()) {
        const std::filesystem::path databasePath = cursor / ".myengine" / "AssetDatabase.json";
        if (std::filesystem::is_regular_file(databasePath)) {
            try {
                std::ifstream input(databasePath);
                nlohmann::json database;
                input >> database;
                const std::string normalized = NormalizePath(texturePath);
                for (const auto& record : database.value("assets", nlohmann::json::array())) {
                    const std::string source = record.value("sourcePath", std::string{});
                    const std::string artifact = record.value("artifactPath", std::string{});
                    if ((!source.empty() && NormalizePath(source) == normalized) ||
                        (!artifact.empty() && NormalizePath(artifact) == normalized)) {
                        const std::string settingsJson = record.value("settings", std::string("{}"));
                        const nlohmann::json settings = nlohmann::json::parse(settingsJson);
                        const auto sampler = settings.find("textureSampler");
                        if (sampler != settings.end() && sampler->is_object()) {
                            desc.filter = ParseTextureFilter(sampler->value("filter", std::string("linear")));
                            desc.wrapU = ParseTextureWrap(sampler->value("wrapU", std::string("repeat")));
                            desc.wrapV = ParseTextureWrap(sampler->value("wrapV", std::string("repeat")));
                        }
                        return;
                    }
                }
            } catch (...) {
                return;
            }
            return;
        }
        const auto parent = cursor.parent_path();
        if (parent == cursor)
            break;
        cursor = parent;
    }
}

std::string Trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::vector<std::string> Tokenize(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

ObjIndex ParseFaceToken(const std::string& token) {
    ObjIndex out;
    size_t first = token.find('/');
    if (first == std::string::npos) {
        out.v = std::stoi(token);
        return out;
    }

    size_t second = token.find('/', first + 1);
    const std::string vStr = token.substr(0, first);
    const std::string vtStr =
        (second == std::string::npos) ? token.substr(first + 1) : token.substr(first + 1, second - first - 1);
    const std::string vnStr = (second == std::string::npos) ? std::string() : token.substr(second + 1);

    if (!vStr.empty())
        out.v = std::stoi(vStr);
    if (!vtStr.empty())
        out.vt = std::stoi(vtStr);
    if (!vnStr.empty())
        out.vn = std::stoi(vnStr);
    return out;
}

int ResolveObjIndex(int idx, size_t count) {
    if (idx > 0) {
        const int resolved = idx - 1;
        return (resolved >= 0 && static_cast<size_t>(resolved) < count) ? resolved : -1;
    }
    if (idx < 0) {
        const int resolved = static_cast<int>(count) + idx;
        return (resolved >= 0 && static_cast<size_t>(resolved) < count) ? resolved : -1;
    }
    return -1;
}

Vec3 ComputeFaceNormal(const MeshVertex& a, const MeshVertex& b, const MeshVertex& c) {
    return (b.position - a.position).Cross(c.position - a.position).Normalized();
}

MeshVertex BuildVertex(const std::vector<Vec3>& positions, const std::vector<std::pair<float, float>>& texcoords,
                       const std::vector<Vec3>& normals, const ObjIndex& idx) {
    MeshVertex v{};

    const int posIndex = ResolveObjIndex(idx.v, positions.size());
    if (posIndex >= 0) {
        v.position = positions[static_cast<size_t>(posIndex)];
    }

    const int uvIndex = ResolveObjIndex(idx.vt, texcoords.size());
    if (uvIndex >= 0) {
        v.u = texcoords[static_cast<size_t>(uvIndex)].first;
        v.v = texcoords[static_cast<size_t>(uvIndex)].second;
    }

    const int normalIndex = ResolveObjIndex(idx.vn, normals.size());
    if (normalIndex >= 0) {
        v.normal = normals[static_cast<size_t>(normalIndex)];
    }

    return v;
}

void ParseMtlFile(const std::filesystem::path& mtlPath, std::unordered_map<std::string, MtlRecord>& outRecords,
                  std::vector<std::string>& outOrder) {
    std::ifstream file(mtlPath);
    if (!file.is_open()) {
        Logger::Warn("[AssetManager] Could not open MTL: ", mtlPath.string());
        return;
    }

    std::string line;
    std::string currentName;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto tokens = Tokenize(line);
        if (tokens.empty()) {
            continue;
        }

        const std::string& cmd = tokens[0];
        if (cmd == "newmtl" && tokens.size() >= 2) {
            currentName = tokens[1];
            if (!outRecords.count(currentName)) {
                outRecords[currentName] = {};
                outOrder.push_back(currentName);
            }
            continue;
        }

        if (currentName.empty()) {
            continue;
        }

        auto& rec = outRecords[currentName];
        if (cmd == "Kd" && tokens.size() >= 4) {
            rec.diffuse = {std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])};
        } else if (cmd == "d" && tokens.size() >= 2) {
            rec.alpha = std::clamp(std::stof(tokens[1]), 0.0f, 1.0f);
        } else if (cmd == "Tr" && tokens.size() >= 2) {
            rec.alpha = 1.0f - std::clamp(std::stof(tokens[1]), 0.0f, 1.0f);
        } else if (cmd == "map_Kd" && tokens.size() >= 2) {
            rec.diffuseTexture = tokens.back();
        }
    }
}

} // namespace

std::shared_ptr<TextureAsset> LoadTextureAssetFromFile(const std::string& path) {
    int width = 0;
    int height = 0;
    int channels = 0;

    stbi_set_flip_vertically_on_load(true);
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        Logger::Error("[AssetManager] Failed to load texture '", path, "': ", stbi_failure_reason());
        return {};
    }

    std::vector<uint8_t> data(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    stbi_image_free(pixels);

    auto tex = std::make_shared<TextureAsset>(path);
    tex->SetName(StemFromPath(path));

    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = TextureFormat::RGBA8;
    desc.sRGB = true;
    ApplyTextureImportSettings(path, desc);
    tex->SetPixelData(std::move(data), desc);

    Logger::Info("[AssetManager] Imported texture '", tex->GetName(), "' (", width, "x", height, ")");
    return tex;
}

std::shared_ptr<ModelAsset> LoadModelAssetFromObj(const std::string& path) {
    try {
        const std::filesystem::path objPath = std::filesystem::path(path);
        const std::filesystem::path objDir = objPath.parent_path();

        std::ifstream file(objPath);
        if (!file.is_open()) {
            Logger::Error("[AssetManager] Could not open OBJ: ", path);
            return {};
        }

        std::vector<Vec3> positions;
        std::vector<std::pair<float, float>> texcoords;
        std::vector<Vec3> normals;
        positions.reserve(1024);
        texcoords.reserve(1024);
        normals.reserve(1024);

        std::unordered_map<std::string, MtlRecord> materialsByName;
        std::vector<std::string> materialOrder;

        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<SubMesh> subMeshes;
        vertices.reserve(1024);
        indices.reserve(1024);

        std::string currentObject = StemFromPath(path);
        std::string currentMaterialName;
        SubMesh currentSubMesh{};
        bool hasCurrentSubMesh = false;

        auto flushSubMesh = [&]() {
            if (hasCurrentSubMesh && currentSubMesh.indexCount > 0) {
                subMeshes.push_back(currentSubMesh);
            }
            hasCurrentSubMesh = false;
            currentSubMesh = {};
        };

        auto beginSubMesh = [&](const std::string& name, int materialSlot, const std::string& materialName) {
            flushSubMesh();
            currentSubMesh.name = name.empty() ? StemFromPath(path) : name;
            if (!materialName.empty()) {
                currentSubMesh.name += "/" + materialName;
            }
            currentSubMesh.materialSlot = materialSlot;
            currentSubMesh.indexOffset = static_cast<uint32_t>(indices.size());
            currentSubMesh.vertexOffset = 0;
            currentSubMesh.indexCount = 0;
            hasCurrentSubMesh = true;
        };

        auto materialSlotForName = [&](const std::string& materialName) -> int {
            if (materialName.empty()) {
                return 0;
            }
            for (size_t i = 0; i < materialOrder.size(); ++i) {
                if (materialOrder[i] == materialName) {
                    return static_cast<int>(i);
                }
            }
            return 0;
        };

        std::string line;
        while (std::getline(file, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }

            const auto tokens = Tokenize(line);
            if (tokens.empty()) {
                continue;
            }

            const std::string& cmd = tokens[0];
            if (cmd == "v" && tokens.size() >= 4) {
                positions.push_back({std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])});
            } else if (cmd == "vt" && tokens.size() >= 3) {
                texcoords.push_back({std::stof(tokens[1]), std::stof(tokens[2])});
            } else if (cmd == "vn" && tokens.size() >= 4) {
                normals.push_back({std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])});
            } else if (cmd == "mtllib" && tokens.size() >= 2) {
                for (size_t i = 1; i < tokens.size(); ++i) {
                    std::filesystem::path mtlPath = objDir / tokens[i];
                    ParseMtlFile(mtlPath, materialsByName, materialOrder);
                }
            } else if (cmd == "o" || cmd == "g") {
                if (tokens.size() >= 2) {
                    currentObject = tokens[1];
                } else {
                    currentObject = StemFromPath(path);
                }
                flushSubMesh();
            } else if (cmd == "usemtl" && tokens.size() >= 2) {
                currentMaterialName = tokens[1];
                flushSubMesh();
            } else if (cmd == "f" && tokens.size() >= 4) {
                if (!hasCurrentSubMesh) {
                    beginSubMesh(currentObject, materialSlotForName(currentMaterialName), currentMaterialName);
                }

                std::vector<ObjIndex> face;
                face.reserve(tokens.size() - 1);
                for (size_t i = 1; i < tokens.size(); ++i) {
                    face.push_back(ParseFaceToken(tokens[i]));
                }

                for (size_t tri = 1; tri + 1 < face.size(); ++tri) {
                    const ObjIndex triIdx[3] = {face[0], face[tri], face[tri + 1]};

                    MeshVertex triVerts[3];
                    bool hasNormals = true;
                    for (int i = 0; i < 3; ++i) {
                        triVerts[i] = BuildVertex(positions, texcoords, normals, triIdx[i]);
                        hasNormals = hasNormals && (triIdx[i].vn != -1);
                    }

                    if (!hasNormals) {
                        const Vec3 n = ComputeFaceNormal(triVerts[0], triVerts[1], triVerts[2]);
                        triVerts[0].normal = n;
                        triVerts[1].normal = n;
                        triVerts[2].normal = n;
                    }

                    const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(triVerts[0]);
                    vertices.push_back(triVerts[1]);
                    vertices.push_back(triVerts[2]);

                    indices.push_back(baseVertex + 0);
                    indices.push_back(baseVertex + 1);
                    indices.push_back(baseVertex + 2);
                    currentSubMesh.indexCount += 3;
                }
            }
        }

        flushSubMesh();

        if (vertices.empty() || indices.empty()) {
            Logger::Error("[AssetManager] OBJ produced empty mesh: ", path);
            return {};
        }

        std::vector<MaterialHandle> materials;
        materials.reserve(std::max<size_t>(1, materialOrder.size()));

        if (materialOrder.empty()) {
            materials.push_back(AssetManager::Get().GetDefaultMaterial());
        } else {
            for (size_t materialIndex = 0; materialIndex < materialOrder.size(); ++materialIndex) {
                const auto& name = materialOrder[materialIndex];
                const auto it = materialsByName.find(name);
                if (it == materialsByName.end()) {
                    continue;
                }

                auto mat =
                    MaterialAsset::CreateDefaultAtPath(path + "#material-" + std::to_string(materialIndex), name);
                const auto& rec = it->second;
                mat->SetParam("BaseColor",
                              MaterialParam::FromVec4(rec.diffuse.x, rec.diffuse.y, rec.diffuse.z, rec.alpha));

                if (!rec.diffuseTexture.empty()) {
                    const std::filesystem::path texPath = objDir / NormalizeSeparators(rec.diffuseTexture);
                    auto tex = AssetManager::Get().Load<TextureAsset>(texPath.string());
                    if (tex) {
                        mat->SetTexture("BaseColorMap", tex);
                    }
                }

                materials.push_back(AssetManager::Get().Register(std::move(mat)));
            }

            if (materials.empty()) {
                materials.push_back(AssetManager::Get().GetDefaultMaterial());
            }
        }

        auto mesh = std::make_shared<MeshAsset>(path + "#mesh");
        mesh->SetName(StemFromPath(path));
        mesh->SetGeometry(std::move(vertices), std::move(indices), std::move(subMeshes));

        auto model = std::make_shared<ModelAsset>(path);
        model->SetName(StemFromPath(path));
        model->SetMesh(AssetManager::Get().Register(std::move(mesh)));
        model->SetMaterials(std::move(materials));

        Logger::Info("[AssetManager] Imported model '", model->GetName(), "' (", model->GetMesh()->VertexCount(),
                     " verts, ", model->GetMesh()->IndexCount(), " indices, ", model->MaterialCount(), " materials)");
        return model;
    } catch (const std::exception& e) {
        Logger::Error("[AssetManager] OBJ parse exception for '", path, "': ", e.what());
        return {};
    }
}
