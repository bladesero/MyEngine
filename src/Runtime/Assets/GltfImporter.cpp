#include "Assets/AssetManager.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <unordered_map>

namespace {

const cgltf_accessor* FindAttribute(
    const cgltf_primitive& primitive, cgltf_attribute_type type, int index = 0)
{
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& attribute = primitive.attributes[i];
        if (attribute.type == type && attribute.index == index) return attribute.data;
    }
    return nullptr;
}

Vec3 ReadAccessorVec3(const cgltf_accessor* accessor, cgltf_size index, const Vec3& fallback)
{
    if (!accessor) return fallback;
    cgltf_float values[4] = {};
    if (!cgltf_accessor_read_float(accessor, index, values, 3)) return fallback;
    return { values[0], values[1], values[2] };
}

Quat ReadAccessorQuat(const cgltf_accessor* accessor, cgltf_size index)
{
    if (!accessor) return Quat::Identity();
    cgltf_float values[4] = {};
    if (!cgltf_accessor_read_float(accessor, index, values, 4)) return Quat::Identity();
    return Quat{ values[0], values[1], values[2], values[3] }.Normalized();
}

Mat4 MatrixFromColumnMajor(const cgltf_float values[16])
{
    Mat4 result;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.m[row][column] = values[column * 4 + row];
        }
    }
    return result;
}

void GenerateTangents(std::vector<MeshVertex>& vertices,
                      const std::vector<uint32_t>& indices)
{
    std::vector<Vec3> accumulated(vertices.size(), Vec3::Zero());
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) continue;
        const MeshVertex& a = vertices[ia];
        const MeshVertex& b = vertices[ib];
        const MeshVertex& c = vertices[ic];
        const Vec3 e1 = b.position - a.position;
        const Vec3 e2 = c.position - a.position;
        const float du1 = b.u - a.u, dv1 = b.v - a.v;
        const float du2 = c.u - a.u, dv2 = c.v - a.v;
        const float determinant = du1 * dv2 - du2 * dv1;
        if (std::fabs(determinant) < 1e-8f) continue;
        const Vec3 tangent = (e1 * dv2 - e2 * dv1) / determinant;
        accumulated[ia] += tangent;
        accumulated[ib] += tangent;
        accumulated[ic] += tangent;
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
        Vec3 tangent = accumulated[i] -
            vertices[i].normal * accumulated[i].Dot(vertices[i].normal);
        vertices[i].tangent = tangent.LengthSq() > 1e-8f
            ? tangent.Normalized() : Vec3::Right();
    }
}

std::vector<uint8_t> DecodeBase64(const char* text)
{
    static const int8_t table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
        -1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };
    std::vector<uint8_t> output;
    int value = 0, bits = -8;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (*p >= 128) continue;
        const int decoded = table[*p];
        if (decoded == -2) break;
        if (decoded < 0) continue;
        value = (value << 6) | decoded;
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<uint8_t>((value >> bits) & 0xff));
            bits -= 8;
        }
    }
    return output;
}

bool GetImageBytes(const cgltf_image& image, const std::filesystem::path& sourcePath,
                   std::vector<uint8_t>& bytes)
{
    if (image.buffer_view && image.buffer_view->buffer &&
        image.buffer_view->buffer->data) {
        const uint8_t* begin =
            static_cast<const uint8_t*>(image.buffer_view->buffer->data) +
            image.buffer_view->offset;
        bytes.assign(begin, begin + image.buffer_view->size);
        return true;
    }
    if (!image.uri) return false;
    const std::string uri(image.uri);
    const size_t comma = uri.find(',');
    if (uri.rfind("data:", 0) == 0 && comma != std::string::npos) {
        bytes = DecodeBase64(uri.c_str() + comma + 1);
        return !bytes.empty();
    }
    std::filesystem::path imagePath = sourcePath.parent_path() / uri;
    std::ifstream input(imagePath, std::ios::binary);
    if (!input.is_open()) return false;
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !bytes.empty();
}

TextureHandle ImportTexture(const cgltf_texture_view& view,
                            const std::filesystem::path& sourcePath, bool srgb)
{
    if (!view.texture || !view.texture->image) return {};
    const cgltf_image* image = view.texture->image;
    std::vector<uint8_t> encoded;
    if (!GetImageBytes(*image, sourcePath, encoded)) return {};

    int width = 0, height = 0, components = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        encoded.data(), static_cast<int>(encoded.size()),
        &width, &height, &components, 4);
    if (!pixels) return {};
    std::vector<uint8_t> rgba(
        pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    stbi_image_free(pixels);

    const ptrdiff_t imageIndex = image - view.texture->image +
        (view.texture->image - image); // keep identity independent of pointer address
    const ptrdiff_t textureIndex = view.texture - view.texture;
    (void)imageIndex;
    (void)textureIndex;
    const std::string suffix = image->name ? image->name :
        (image->uri ? std::filesystem::path(image->uri).stem().string() : "embedded");
    auto texture = std::make_shared<TextureAsset>(
        sourcePath.string() + "#texture-" + suffix + (srgb ? "-srgb" : "-linear"));
    texture->SetName(suffix);
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.sRGB = srgb;
    texture->SetPixelData(std::move(rgba), desc);
    return AssetManager::Get().Register(std::move(texture));
}

std::vector<MaterialHandle> ImportMaterials(
    const cgltf_data& data, const std::filesystem::path& sourcePath)
{
    std::vector<MaterialHandle> materials;
    for (cgltf_size i = 0; i < data.materials_count; ++i) {
        const cgltf_material& source = data.materials[i];
        const std::string materialName = source.name
            ? source.name : "Material" + std::to_string(i);
        auto material = MaterialAsset::CreateDefaultAtPath(
            sourcePath.string() + "#material-" + std::to_string(i), materialName);
        if (source.has_pbr_metallic_roughness) {
            const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
            material->SetParam("BaseColor", MaterialParam::FromVec4(
                pbr.base_color_factor[0], pbr.base_color_factor[1],
                pbr.base_color_factor[2], pbr.base_color_factor[3]));
            material->SetParam("Metallic", MaterialParam::FromFloat(pbr.metallic_factor));
            material->SetParam("Roughness", MaterialParam::FromFloat(pbr.roughness_factor));
            if (auto texture = ImportTexture(pbr.base_color_texture, sourcePath, true)) {
                material->SetTexture("BaseColorMap", texture);
            }
            if (auto texture = ImportTexture(pbr.metallic_roughness_texture, sourcePath, false)) {
                material->SetTexture("MetallicRoughnessMap", texture);
            }
        }
        material->SetParam("Emissive", MaterialParam::FromVec3(
            source.emissive_factor[0], source.emissive_factor[1], source.emissive_factor[2]));
        material->SetTwoSided(source.double_sided != 0);
        if (source.alpha_mode == cgltf_alpha_mode_blend) {
            material->SetBlendMode(BlendMode::Transparent);
        } else if (source.alpha_mode == cgltf_alpha_mode_mask) {
            material->SetBlendMode(BlendMode::AlphaTest);
            material->SetAlphaThreshold(source.alpha_cutoff);
        }
        if (auto texture = ImportTexture(source.normal_texture, sourcePath, false)) {
            material->SetTexture("NormalMap", texture);
        }
        if (auto texture = ImportTexture(source.occlusion_texture, sourcePath, false)) {
            material->SetTexture("OcclusionMap", texture);
        }
        if (auto texture = ImportTexture(source.emissive_texture, sourcePath, true)) {
            material->SetTexture("EmissiveMap", texture);
        }
        materials.push_back(AssetManager::Get().Register(std::move(material)));
    }
    if (materials.empty()) materials.push_back(AssetManager::Get().GetDefaultMaterial());
    return materials;
}

int MaterialSlot(const cgltf_data& data, const cgltf_material* material)
{
    return material ? static_cast<int>(material - data.materials) : 0;
}

void ImportSkinAndAnimations(const cgltf_data& data,
                             const std::vector<SkinWeight>& weights,
                             ModelAsset& model)
{
    if (data.skins_count == 0) return;
    const cgltf_skin& skin = data.skins[0];
    std::unordered_map<const cgltf_node*, uint16_t> nodeToBone;
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        nodeToBone[skin.joints[i]] = static_cast<uint16_t>(i);
    }

    std::vector<Bone> bones(skin.joints_count);
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        const cgltf_node* node = skin.joints[i];
        Bone& bone = bones[i];
        bone.name = node->name ? node->name : "Bone" + std::to_string(i);
        const auto parent = nodeToBone.find(node->parent);
        bone.parent = parent != nodeToBone.end() ? parent->second : -1;
        if (node->has_translation) {
            bone.bindTranslation = { node->translation[0], node->translation[1], node->translation[2] };
        }
        if (node->has_rotation) {
            bone.bindRotation = Quat{
                node->rotation[0], node->rotation[1],
                node->rotation[2], node->rotation[3] }.Normalized();
        }
        if (node->has_scale) {
            bone.bindScale = { node->scale[0], node->scale[1], node->scale[2] };
        }
        if (skin.inverse_bind_matrices && i < skin.inverse_bind_matrices->count) {
            cgltf_float matrix[16] = {};
            cgltf_accessor_read_float(skin.inverse_bind_matrices, i, matrix, 16);
            bone.inverseBind = MatrixFromColumnMajor(matrix);
        }
    }

    std::vector<AnimationClip> clips;
    for (cgltf_size animationIndex = 0;
         animationIndex < data.animations_count; ++animationIndex) {
        const cgltf_animation& source = data.animations[animationIndex];
        AnimationClip clip;
        clip.name = source.name ? source.name :
            "Animation" + std::to_string(animationIndex);
        std::unordered_map<uint16_t, std::map<float, BoneKeyframe>> keys;
        for (cgltf_size channelIndex = 0;
             channelIndex < source.channels_count; ++channelIndex) {
            const cgltf_animation_channel& channel = source.channels[channelIndex];
            if (!channel.sampler || !channel.target_node) continue;
            const auto boneIt = nodeToBone.find(channel.target_node);
            if (boneIt == nodeToBone.end()) continue;
            const cgltf_accessor* times = channel.sampler->input;
            const cgltf_accessor* values = channel.sampler->output;
            if (!times || !values) continue;
            for (cgltf_size keyIndex = 0; keyIndex < times->count; ++keyIndex) {
                cgltf_float time = 0.0f;
                cgltf_accessor_read_float(times, keyIndex, &time, 1);
                BoneKeyframe& key = keys[boneIt->second][time];
                if (key.time == 0.0f && keyIndex == 0) {
                    const Bone& bind = bones[boneIt->second];
                    key.translation = bind.bindTranslation;
                    key.rotation = bind.bindRotation;
                    key.scale = bind.bindScale;
                }
                key.time = time;
                if (channel.target_path == cgltf_animation_path_type_translation) {
                    key.translation = ReadAccessorVec3(values, keyIndex, key.translation);
                } else if (channel.target_path == cgltf_animation_path_type_scale) {
                    key.scale = ReadAccessorVec3(values, keyIndex, key.scale);
                } else if (channel.target_path == cgltf_animation_path_type_rotation) {
                    key.rotation = ReadAccessorQuat(values, keyIndex);
                }
                clip.duration = std::max(clip.duration, key.time);
            }
        }
        for (auto& entry : keys) {
            BoneTrack track;
            track.boneIndex = entry.first;
            for (auto& key : entry.second) track.keys.push_back(std::move(key.second));
            clip.tracks.push_back(std::move(track));
        }
        clips.push_back(std::move(clip));
    }
    model.SetSkin(std::move(bones), weights);
    model.SetAnimations(std::move(clips));
}

} // namespace

std::shared_ptr<ModelAsset> LoadModelAssetFromGltf(const std::string& path)
{
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success) {
        Logger::Error("[glTF] Parse failed: ", path, " code=", static_cast<int>(result));
        return {};
    }
    struct DataGuard {
        cgltf_data* data;
        ~DataGuard() { cgltf_free(data); }
    } guard{ data };
    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success) {
        Logger::Error("[glTF] Buffer load failed: ", path, " code=", static_cast<int>(result));
        return {};
    }
    if (cgltf_validate(data) != cgltf_result_success) {
        Logger::Warn("[glTF] Validation reported issues: ", path);
    }

    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> subMeshes;
    std::vector<SkinWeight> skinWeights;
    bool needsTangents = false;

    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex) {
        const cgltf_mesh& mesh = data->meshes[meshIndex];
        for (cgltf_size primitiveIndex = 0;
             primitiveIndex < mesh.primitives_count; ++primitiveIndex) {
            const cgltf_primitive& primitive = mesh.primitives[primitiveIndex];
            if (primitive.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor* positions =
                FindAttribute(primitive, cgltf_attribute_type_position);
            if (!positions) continue;
            const cgltf_accessor* normals =
                FindAttribute(primitive, cgltf_attribute_type_normal);
            const cgltf_accessor* tangents =
                FindAttribute(primitive, cgltf_attribute_type_tangent);
            const cgltf_accessor* uvs =
                FindAttribute(primitive, cgltf_attribute_type_texcoord);
            const cgltf_accessor* joints =
                FindAttribute(primitive, cgltf_attribute_type_joints);
            const cgltf_accessor* weights =
                FindAttribute(primitive, cgltf_attribute_type_weights);
            needsTangents = needsTangents || !tangents;

            const size_t vertexBase = vertices.size();
            vertices.resize(vertexBase + positions->count);
            skinWeights.resize(vertexBase + positions->count);
            for (cgltf_size i = 0; i < positions->count; ++i) {
                MeshVertex& vertex = vertices[vertexBase + i];
                vertex.position = ReadAccessorVec3(positions, i, Vec3::Zero());
                vertex.normal = ReadAccessorVec3(normals, i, Vec3::Up()).Normalized();
                vertex.tangent = ReadAccessorVec3(tangents, i, Vec3::Right()).Normalized();
                if (uvs) {
                    cgltf_float uv[2] = {};
                    cgltf_accessor_read_float(uvs, i, uv, 2);
                    vertex.u = uv[0];
                    vertex.v = uv[1];
                }
                if (joints && weights) {
                    cgltf_uint jointValues[4] = {};
                    cgltf_float weightValues[4] = {};
                    cgltf_accessor_read_uint(joints, i, jointValues, 4);
                    cgltf_accessor_read_float(weights, i, weightValues, 4);
                    for (size_t influence = 0; influence < 4; ++influence) {
                        skinWeights[vertexBase + i].boneIndices[influence] =
                            static_cast<uint16_t>(jointValues[influence]);
                        skinWeights[vertexBase + i].weights[influence] =
                            weightValues[influence];
                        vertex.boneIndices[influence] =
                            static_cast<float>(jointValues[influence]);
                        vertex.boneWeights[influence] = weightValues[influence];
                    }
                }
            }

            SubMesh subMesh;
            subMesh.indexOffset = static_cast<uint32_t>(indices.size());
            subMesh.vertexOffset = 0;
            subMesh.materialSlot = MaterialSlot(*data, primitive.material);
            const std::string meshName = mesh.name
                ? mesh.name
                : "Mesh" + std::to_string(meshIndex);
            subMesh.name = meshName + "/Primitive" + std::to_string(primitiveIndex);
            if (primitive.indices) {
                subMesh.indexCount = static_cast<uint32_t>(primitive.indices->count);
                for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
                    indices.push_back(static_cast<uint32_t>(vertexBase) +
                        static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i)));
                }
            } else {
                subMesh.indexCount = static_cast<uint32_t>(positions->count);
                for (cgltf_size i = 0; i < positions->count; ++i) {
                    indices.push_back(static_cast<uint32_t>(vertexBase + i));
                }
            }
            subMeshes.push_back(std::move(subMesh));
        }
    }
    if (vertices.empty() || indices.empty()) {
        Logger::Error("[glTF] No triangle geometry: ", path);
        return {};
    }
    if (needsTangents) GenerateTangents(vertices, indices);

    const std::filesystem::path sourcePath(path);
    auto mesh = std::make_shared<MeshAsset>(path + "#mesh");
    mesh->SetName(sourcePath.stem().string());
    mesh->SetGeometry(std::move(vertices), std::move(indices), std::move(subMeshes));

    auto model = std::make_shared<ModelAsset>(path);
    model->SetName(sourcePath.stem().string());
    model->SetMesh(AssetManager::Get().Register(std::move(mesh)));
    model->SetMaterials(ImportMaterials(*data, sourcePath));

    std::vector<ModelNode> nodes(data->nodes_count);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& node = data->nodes[i];
        nodes[i].name = node.name ? node.name : "Node" + std::to_string(i);
        cgltf_float local[16] = {};
        cgltf_node_transform_local(&node, local);
        nodes[i].localTransform = MatrixFromColumnMajor(local);
        if (node.mesh) nodes[i].mesh = model->GetMesh();
        for (cgltf_size child = 0; child < node.children_count; ++child) {
            nodes[i].children.push_back(static_cast<int>(node.children[child] - data->nodes));
        }
    }
    int rootIndex = 0;
    if (data->scene && data->scene->nodes_count > 0) {
        rootIndex = static_cast<int>(data->scene->nodes[0] - data->nodes);
    }
    model->SetNodes(std::move(nodes), rootIndex);
    ImportSkinAndAnimations(*data, skinWeights, *model);
    Logger::Info("[glTF] Imported '", model->GetName(), "' vertices=",
                 model->GetMesh()->VertexCount(), " materials=", model->MaterialCount(),
                 " bones=", model->GetBones().size(),
                 " animations=", model->GetAnimations().size());
    return model;
}
