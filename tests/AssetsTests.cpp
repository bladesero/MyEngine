#include "TestHarness.h"

#include "Assets/AssetManager.h"
#include "Assets/AssetMeta.h"
#include "Assets/ModelCacheAsset.h"
#include "Assets/ShaderAsset.h"
#include "Animation/SkinnedMeshRendererComponent.h"
#include "Audio/AudioClipAsset.h"
#include "Editor/AssetImportService.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorImportService.h"
#include "Project/ProjectConfig.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scripting/ScriptComponent.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

void WriteSilentWav(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    const uint32_t sampleRate = 8000;
    const uint16_t channels = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t frameCount = 16;
    const uint32_t dataBytes = frameCount * channels * (bitsPerSample / 8);
    const uint32_t riffSize = 36 + dataBytes;
    const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
    const uint16_t blockAlign = channels * (bitsPerSample / 8);
    output.write("RIFF", 4);
    output.write(reinterpret_cast<const char*>(&riffSize), sizeof(riffSize));
    output.write("WAVEfmt ", 8);
    const uint32_t fmtSize = 16;
    const uint16_t audioFormat = 1;
    output.write(reinterpret_cast<const char*>(&fmtSize), sizeof(fmtSize));
    output.write(reinterpret_cast<const char*>(&audioFormat), sizeof(audioFormat));
    output.write(reinterpret_cast<const char*>(&channels), sizeof(channels));
    output.write(reinterpret_cast<const char*>(&sampleRate), sizeof(sampleRate));
    output.write(reinterpret_cast<const char*>(&byteRate), sizeof(byteRate));
    output.write(reinterpret_cast<const char*>(&blockAlign), sizeof(blockAlign));
    output.write(reinterpret_cast<const char*>(&bitsPerSample), sizeof(bitsPerSample));
    output.write("data", 4);
    output.write(reinterpret_cast<const char*>(&dataBytes), sizeof(dataBytes));
    std::vector<int16_t> silence(frameCount * channels, 0);
    output.write(reinterpret_cast<const char*>(silence.data()),
                 static_cast<std::streamsize>(silence.size() * sizeof(int16_t)));
}

bool TestShaderAssetFormats() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_shader_asset_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    auto write = [&](const char* name, const char* json) {
        const fs::path path = root / name;
        std::ofstream(path) << json;
        return path;
    };
    const auto graphics = write(
        "Mesh.shader",
        R"({"type":"Shader","version":1,"stages":{"vertex":{"source":"Mesh.hlsl","entry":"VSMain"},"pixel":{"source":"Mesh.hlsl","entry":"PSMain"}},"defines":[]})");
    auto asset = LoadShaderAssetFromFile(graphics.string());
    if (!Check(asset && !asset->IsCooked() && !asset->IsCompute(), "valid graphics shader description was rejected"))
        return false;
    const auto compute = write(
        "Compute.shader",
        R"({"type":"Shader","version":1,"stages":{"compute":{"source":"Compute.hlsl","entry":"CSMain"}},"defines":[]})");
    if (!Check(LoadShaderAssetFromFile(compute.string())->IsCompute(), "valid compute shader description was rejected"))
        return false;
    const char* invalid[] = {
        R"({"type":"Shader","version":2,"stages":{"compute":{"source":"A.hlsl","entry":"CSMain"}}})",
        R"({"type":"Shader","version":1,"stages":{"vertex":{"source":"A.hlsl","entry":"VSMain"}}})",
        R"({"type":"Shader","version":1,"stages":{"compute":{"source":"../A.hlsl","entry":"CSMain"}}})",
        R"({"type":"Shader","version":1,"stages":{"compute":{"source":"C:/A.hlsl","entry":"CSMain"},"pixel":{"source":"A.hlsl","entry":"PSMain"}}})",
        R"({"type":"Shader","version":1,"stages":{"compute":{"source":"A.hlsl","entry":"CSMain"},"compute":{"source":"B.hlsl","entry":"CSMain"}}})",
        R"({"type":"Shader","version":1,"stages":{"geometry":{"source":"A.hlsl","entry":"GSMain"}}})"};
    for (size_t i = 0; i < std::size(invalid); ++i)
        if (!Check(!LoadShaderAssetFromFile(
                       write(("Invalid" + std::to_string(i) + ".shader").c_str(), invalid[i]).string()),
                   "invalid shader description was accepted"))
            return false;
    std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, kShaderBackendCount> blobs{};
    for (auto& backend : blobs) {
        backend[0] = {1, 2, 3};
        backend[1] = {4, 5};
    }
    ShaderAsset cooked(graphics.string());
    cooked.SetCooked(ShaderAsset::kVertexMask | ShaderAsset::kPixelMask, 42, std::move(blobs));
    const fs::path cookedPath = root / "Cooked.shader";
    std::string error;
    if (!Check(SaveCookedShaderAsset(cooked, cookedPath, &error), error))
        return false;
    auto loaded = LoadShaderAssetFromFile(cookedPath.string());
    if (!Check(loaded && loaded->IsCooked() &&
                   loaded->GetBytecode(ShaderBackend::D3D12, ShaderStage::Pixel).size() == 2,
               "cooked shader container round-trip failed"))
        return false;
    {
        std::ofstream append(cookedPath, std::ios::binary | std::ios::app);
        append.put('x');
    }
    if (!Check(!LoadShaderAssetFromFile(cookedPath.string()), "corrupt shader container was accepted"))
        return false;
    fs::remove_all(root, ec);
    return true;
}

bool TestPbrMaterialParameters() {
    auto material = MaterialAsset::CreateDefault("PbrTest");
    material->SetParam("Metallic", MaterialParam::FromFloat(0.8f));
    material->SetParam("Roughness", MaterialParam::FromFloat(0.25f));
    if (!Check(NearlyEqual(material->GetFloat("Metallic", 0.0f), 0.8f), "PBR metallic parameter mismatch"))
        return false;
    if (!Check(NearlyEqual(material->GetFloat("Roughness", 1.0f), 0.25f), "PBR roughness parameter mismatch"))
        return false;
    return Check(NearlyEqual(material->GetFloat("AmbientOcclusion", 0.7f), 0.7f),
                 "material default parameter fallback mismatch");
}

bool TestMaterialAssetFileRoundTrip() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_material_asset_test";
    fs::remove_all(root);
    fs::create_directories(root);

    const fs::path texPath = root / "base.ppm";
    {
        std::ofstream output(texPath, std::ios::binary);
        output << "P6\n1 1\n255\n";
        const char pixel[3] = {static_cast<char>(255), static_cast<char>(64), static_cast<char>(32)};
        output.write(pixel, sizeof(pixel));
    }

    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    TextureHandle texture = manager.Load<TextureAsset>(texPath.string());
    if (!Check(texture.IsValid(), "material test texture load failed"))
        return false;
    const fs::path shaderPath = root / "material.shader";
    std::ofstream(shaderPath)
        << R"({"type":"Shader","version":1,"stages":{"vertex":{"source":"material.hlsl","entry":"VSMain"},"pixel":{"source":"material.hlsl","entry":"PSMain"}},"defines":[]})";
    ShaderAssetHandle shaderAsset = manager.Load<ShaderAsset>(shaderPath.string());
    if (!Check(shaderAsset.IsValid(), "material test shader load failed"))
        return false;

    const fs::path matPath = root / "test.mat";
    auto material = std::make_shared<MaterialAsset>(matPath.string());
    material->SetName("RoundTrip");
    material->SetBlendMode(BlendMode::Transparent);
    material->SetTwoSided(true);
    material->SetAlphaThreshold(0.33f);
    material->SetParam("BaseColor", MaterialParam::FromVec4(0.2f, 0.3f, 0.4f, 0.5f));
    material->SetParam("Metallic", MaterialParam::FromFloat(0.9f));
    material->SetParam("Roughness", MaterialParam::FromFloat(0.21f));
    material->SetParam("AmbientOcclusion", MaterialParam::FromFloat(0.8f));
    material->SetTexture("BaseColorMap", texture);
    material->SetShaderAsset(shaderAsset);
    if (!Check(SaveMaterialAssetToFile(*material, matPath.string()), "material save failed"))
        return false;

    manager.Clear();
    MaterialHandle loaded = manager.Load<MaterialAsset>(matPath.string());
    if (!Check(loaded.IsValid(), "material load failed"))
        return false;
    if (!Check(loaded->GetBlendMode() == BlendMode::Transparent && loaded->IsTwoSided(),
               "material render state roundtrip failed"))
        return false;
    if (!Check(NearlyEqual(loaded->GetAlphaThreshold(), 0.33f) && NearlyEqual(loaded->GetFloat("Metallic"), 0.9f) &&
                   NearlyEqual(loaded->GetFloat("Roughness"), 0.21f) &&
                   NearlyEqual(loaded->GetFloat("AmbientOcclusion"), 0.8f),
               "material scalar roundtrip failed"))
        return false;
    if (!Check(loaded->GetTexture("BaseColorMap").IsValid(), "material texture slot roundtrip failed"))
        return false;
    if (!Check(loaded->GetShaderAsset().IsValid(), "material shader asset reference roundtrip failed"))
        return false;

    manager.Clear();
    fs::remove_all(root);
    return true;
}

bool TestAssetManagerSharedAcrossRuntimeBoundary() {
    auto mesh = MeshAsset::CreateTriangle("__dll_shared_mesh");
    const std::string path = mesh->GetPath();
    MeshHandle registered = AssetManager::Get().Register(mesh);

    Scene source("DllBoundary");
    Actor* actor = source.CreateActor("SharedMesh");
    auto* renderer = actor->AddComponent<MeshRendererComponent>();
    renderer->SetMesh(registered);
    renderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());

    Scene loaded("Loaded");
    if (!Check(SceneSerializer::LoadFromString(loaded, SceneSerializer::SaveToString(source)),
               "DLL boundary scene load failed"))
        return false;

    auto* loadedRenderer = loaded.FindByName("SharedMesh")->GetComponent<MeshRendererComponent>();
    if (!Check(loadedRenderer && loadedRenderer->GetMesh().IsValid(),
               "runtime DLL could not see executable-registered asset"))
        return false;
    return Check(loadedRenderer->GetMesh()->GetPath() == path, "shared asset path changed across DLL boundary");
}

bool TestAssetFileImporters() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_asset_import_test";
    fs::create_directories(root);

    const fs::path texPath = root / "albedo.ppm";
    const fs::path mtlPath = root / "tri.mtl";
    const fs::path objPath = root / "tri.obj";

    {
        std::ofstream tex(texPath, std::ios::binary);
        tex << "P6\n2 2\n255\n";
        const unsigned char pixels[] = {
            255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255,
        };
        tex.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
    }

    {
        std::ofstream mtl(mtlPath, std::ios::binary);
        mtl << "newmtl Material0\n";
        mtl << "Kd 1 1 1\n";
        mtl << "map_Kd albedo.ppm\n";
    }

    {
        std::ofstream obj(objPath, std::ios::binary);
        obj << "mtllib tri.mtl\n";
        obj << "o Tri\n";
        obj << "usemtl Material0\n";
        obj << "v 0 0 0\n";
        obj << "v 1 0 0\n";
        obj << "v 0 1 0\n";
        obj << "vt 0 0\n";
        obj << "vt 1 0\n";
        obj << "vt 0 1\n";
        obj << "vn 0 0 1\n";
        obj << "vn 0 0 1\n";
        obj << "vn 0 0 1\n";
        obj << "f 1/1/1 2/2/2 3/3/3\n";
    }

    AssetManager& am = AssetManager::Get();
    auto tex = am.Load<TextureAsset>(texPath.string());
    if (!Check(tex.IsValid(), "texture import should succeed"))
        return false;
    if (!Check(tex->GetWidth() == 2 && tex->GetHeight() == 2, "texture dimensions mismatch"))
        return false;
    if (!Check(tex->GetPixelData().size() == 16, "texture pixel data size mismatch"))
        return false;
    if (!Check(tex->GetMipLevels() == 2 && tex->GetMips().size() == 2 && tex->GetMips()[1].width == 1 &&
                   tex->GetMips()[1].height == 1,
               "imported texture mip chain mismatch"))
        return false;

    auto model = am.Load<ModelAsset>(objPath.string());
    if (!Check(model.IsValid(), "model import should succeed"))
        return false;
    if (!Check(model->GetMesh() && model->GetMesh()->VertexCount() == 3, "model vertex count mismatch"))
        return false;
    if (!Check(model->MaterialCount() == 1, "model material count mismatch"))
        return false;
    if (!Check(model->GetMaterial(0).IsValid(), "model material should be valid"))
        return false;
    if (!Check(model->GetMaterial(0)->HasTexture("BaseColorMap"), "material should keep imported texture"))
        return false;

    am.Clear();
    fs::remove_all(root);
    return true;
}

bool TestAudioClipAssetLoader() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_audio_clip_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const fs::path wavPath = root / "beep.wav";
    WriteSilentWav(wavPath);

    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    AudioClipHandle clip = manager.Load<AudioClipAsset>(wavPath.string());
    if (!Check(clip.IsValid(), "audio clip load failed"))
        return false;
    if (!Check(clip->GetChannels() == 1 && clip->GetSampleRate() == 8000, "audio clip metadata mismatch"))
        return false;
    if (!Check(clip->GetFrameCount() == 16 && clip->GetDurationSeconds() > 0.0f, "audio clip duration mismatch"))
        return false;

    const fs::path badPath = root / "bad.wav";
    std::ofstream(badPath) << "not a wave";
    if (!Check(!manager.Load<AudioClipAsset>(badPath.string()).IsValid(), "invalid audio clip was accepted"))
        return false;
    manager.Clear();
    fs::remove_all(root, ec);
    return true;
}

template <typename T> void AppendBinary(std::vector<uint8_t>& output, const std::vector<T>& values) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(values.data());
    output.insert(output.end(), bytes, bytes + values.size() * sizeof(T));
}

void WriteSimpleTriangleGltf(const std::filesystem::path& gltfPath, const std::filesystem::path& binPath,
                             float secondVertexX = 1.0f) {
    std::vector<uint8_t> binary;
    AppendBinary(binary, std::vector<float>{0, 0, 0, secondVertexX, 0, 0, 0, 1, 0});
    AppendBinary(binary, std::vector<float>{0, 0, 1, 0, 0, 1, 0, 0, 1});
    AppendBinary(binary, std::vector<float>{0, 0, secondVertexX, 0, 0, 1});
    AppendBinary(binary, std::vector<uint16_t>{0, 1, 2});
    {
        std::ofstream output(binPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
    }

    nlohmann::json gltf;
    gltf["asset"] = {{"version", "2.0"}, {"generator", "MyEngineTests"}};
    gltf["buffers"] =
        nlohmann::json::array({{{"uri", binPath.filename().generic_string()}, {"byteLength", binary.size()}}});
    gltf["bufferViews"] = nlohmann::json::array({
        {{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
        {{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 36}},
        {{"buffer", 0}, {"byteOffset", 72}, {"byteLength", 24}},
        {{"buffer", 0}, {"byteOffset", 96}, {"byteLength", 6}},
    });
    gltf["accessors"] = nlohmann::json::array({
        {{"bufferView", 0},
         {"componentType", 5126},
         {"count", 3},
         {"type", "VEC3"},
         {"min", nlohmann::json::array({0, 0, 0})},
         {"max", nlohmann::json::array({secondVertexX, 1, 0})}},
        {{"bufferView", 1}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
        {{"bufferView", 2}, {"componentType", 5126}, {"count", 3}, {"type", "VEC2"}},
        {{"bufferView", 3}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}},
    });
    gltf["materials"] = nlohmann::json::array(
        {{{"name", "CacheMat"},
          {"pbrMetallicRoughness", {{"baseColorFactor", nlohmann::json::array({0.4, 0.6, 0.8, 1.0})}}}}});
    gltf["meshes"] = nlohmann::json::array(
        {{{"name", "CacheTriangle"},
          {"primitives", nlohmann::json::array({{{"attributes", {{"POSITION", 0}, {"NORMAL", 1}, {"TEXCOORD_0", 2}}},
                                                 {"indices", 3},
                                                 {"material", 0}}})}}});
    gltf["nodes"] = nlohmann::json::array({{{"mesh", 0}}});
    gltf["scenes"] = nlohmann::json::array({{{"nodes", nlohmann::json::array({0})}}});
    gltf["scene"] = 0;
    std::ofstream(gltfPath) << gltf.dump(2);
}

bool TestGltfImportAndStableMeta() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_gltf_import_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path gltfPath = root / "skinned_triangle.gltf";
    const fs::path binPath = root / "skinned_triangle.bin";

    std::vector<uint8_t> binary;
    AppendBinary(binary, std::vector<float>{0, 0, 0, 1, 0, 0, 0, 1, 0});
    AppendBinary(binary, std::vector<float>{0, 0, 1, 0, 0, 1, 0, 0, 1});
    AppendBinary(binary, std::vector<float>{0, 0, 1, 0, 0, 1});
    AppendBinary(binary, std::vector<uint16_t>{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    AppendBinary(binary, std::vector<float>{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    AppendBinary(binary, std::vector<uint16_t>{0, 1, 2});
    binary.resize(176, 0);
    AppendBinary(binary, std::vector<float>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    AppendBinary(binary, std::vector<float>{0, 1});
    AppendBinary(binary, std::vector<float>{0, 0, 0, 1, 0, 0});

    {
        std::ofstream output(binPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
    }

    nlohmann::json gltf;
    gltf["asset"] = {{"version", "2.0"}, {"generator", "MyEngineTests"}};
    gltf["buffers"] = nlohmann::json::array({{{"uri", "skinned_triangle.bin"}, {"byteLength", binary.size()}}});
    gltf["bufferViews"] = nlohmann::json::array({
        {{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
        {{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 36}},
        {{"buffer", 0}, {"byteOffset", 72}, {"byteLength", 24}},
        {{"buffer", 0}, {"byteOffset", 96}, {"byteLength", 24}},
        {{"buffer", 0}, {"byteOffset", 120}, {"byteLength", 48}},
        {{"buffer", 0}, {"byteOffset", 168}, {"byteLength", 6}},
        {{"buffer", 0}, {"byteOffset", 176}, {"byteLength", 64}},
        {{"buffer", 0}, {"byteOffset", 240}, {"byteLength", 8}},
        {{"buffer", 0}, {"byteOffset", 248}, {"byteLength", 24}},
    });
    gltf["accessors"] = nlohmann::json::array({
        {{"bufferView", 0},
         {"componentType", 5126},
         {"count", 3},
         {"type", "VEC3"},
         {"min", nlohmann::json::array({0, 0, 0})},
         {"max", nlohmann::json::array({1, 1, 0})}},
        {{"bufferView", 1}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
        {{"bufferView", 2}, {"componentType", 5126}, {"count", 3}, {"type", "VEC2"}},
        {{"bufferView", 3}, {"componentType", 5123}, {"count", 3}, {"type", "VEC4"}},
        {{"bufferView", 4}, {"componentType", 5126}, {"count", 3}, {"type", "VEC4"}},
        {{"bufferView", 5}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}},
        {{"bufferView", 6}, {"componentType", 5126}, {"count", 1}, {"type", "MAT4"}},
        {{"bufferView", 7},
         {"componentType", 5126},
         {"count", 2},
         {"type", "SCALAR"},
         {"min", nlohmann::json::array({0})},
         {"max", nlohmann::json::array({1})}},
        {{"bufferView", 8}, {"componentType", 5126}, {"count", 2}, {"type", "VEC3"}},
    });
    gltf["materials"] = nlohmann::json::array({{{"name", "RedMetal"},
                                                {"pbrMetallicRoughness",
                                                 {{"baseColorFactor", nlohmann::json::array({0.8, 0.1, 0.05, 1.0})},
                                                  {"metallicFactor", 0.7},
                                                  {"roughnessFactor", 0.25}}}}});
    gltf["meshes"] = nlohmann::json::array(
        {{{"name", "Triangle"},
          {"primitives",
           nlohmann::json::array(
               {{{"attributes", {{"POSITION", 0}, {"NORMAL", 1}, {"TEXCOORD_0", 2}, {"JOINTS_0", 3}, {"WEIGHTS_0", 4}}},
                 {"indices", 5},
                 {"material", 0}}})}}});
    gltf["nodes"] = nlohmann::json::array({{{"name", "RootBone"}}, {{"name", "MeshNode"}, {"mesh", 0}, {"skin", 0}}});
    gltf["skins"] =
        nlohmann::json::array({{{"name", "Skin"}, {"joints", nlohmann::json::array({0})}, {"inverseBindMatrices", 6}}});
    gltf["animations"] = nlohmann::json::array(
        {{{"name", "Move"},
          {"samplers", nlohmann::json::array({{{"input", 7}, {"output", 8}, {"interpolation", "LINEAR"}}})},
          {"channels",
           nlohmann::json::array({{{"sampler", 0}, {"target", {{"node", 0}, {"path", "translation"}}}}})}}});
    gltf["scenes"] = nlohmann::json::array({{{"nodes", nlohmann::json::array({0, 1})}}});
    gltf["scene"] = 0;
    {
        std::ofstream output(gltfPath);
        output << gltf.dump(2);
    }

    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    AssetMeta meta = AssetMeta::Create(gltfPath.string());
    if (!Check(AssetMeta::Save(meta), "failed to author glTF metadata"))
        return false;
    ModelHandle model = manager.Load<ModelAsset>(gltfPath.string());
    if (!Check(model.IsValid(), "glTF model import failed"))
        return false;
    if (!Check(model->GetMesh()->VertexCount() == 3 && model->GetMesh()->IndexCount() == 3, "glTF geometry mismatch"))
        return false;
    if (!Check(model->MaterialCount() == 1 && NearlyEqual(model->GetMaterial(0)->GetFloat("Metallic"), 0.7f),
               "glTF PBR material mismatch"))
        return false;
    if (!Check(model->HasSkin() && model->GetBones().size() == 1 && model->GetSkinWeights().size() == 3,
               "glTF skin import mismatch"))
        return false;
    if (!Check(model->GetAnimations().size() == 1 && model->GetAnimations()[0].tracks.size() == 1,
               "glTF animation import mismatch"))
        return false;
    if (!Check(model->GetMesh()->GetVertices()[0].tangent.LengthSq() > 0.5f, "glTF tangent generation failed"))
        return false;
    if (!Check(model->GetDependencies().size() >= 2, "glTF model dependencies were not tracked"))
        return false;
    if (!Check(!model->GetUuid().empty() && fs::exists(gltfPath.string() + ".meta"),
               "glTF stable metadata was not generated"))
        return false;

    const AssetID firstID = model->GetID();
    const std::string firstUuid = model->GetUuid();
    manager.Clear();
    model = manager.Load<ModelAsset>(gltfPath.string());
    if (!Check(model.IsValid() && model->GetID() == firstID && model->GetUuid() == firstUuid,
               "asset UUID changed after reload"))
        return false;

    manager.Clear();
    fs::remove_all(root);
    return true;
}

bool TestGltfImporterDeduplicatesSharedTextures() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_gltf_texture_dedupe_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const fs::path gltfPath = root / "shared_texture.gltf";
    const fs::path binPath = root / "shared_texture.bin";
    const fs::path imagePath = root / "shared.ppm";

    std::vector<uint8_t> binary;
    AppendBinary(binary, std::vector<float>{0, 0, 0, 1, 0, 0, 0, 1, 0});
    AppendBinary(binary, std::vector<float>{0, 0, 1, 0, 0, 1, 0, 0, 1});
    AppendBinary(binary, std::vector<float>{0, 0, 1, 0, 0, 1});
    AppendBinary(binary, std::vector<uint16_t>{0, 1, 2});
    {
        std::ofstream output(binPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
    }
    {
        std::ofstream output(imagePath, std::ios::binary);
        output << "P6\n2 2\n255\n";
        const unsigned char pixels[12] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
        output.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
    }

    nlohmann::json gltf;
    gltf["asset"] = {{"version", "2.0"}, {"generator", "MyEngineTests"}};
    gltf["buffers"] = nlohmann::json::array({{{"uri", "shared_texture.bin"}, {"byteLength", binary.size()}}});
    gltf["bufferViews"] = nlohmann::json::array({
        {{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
        {{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 36}},
        {{"buffer", 0}, {"byteOffset", 72}, {"byteLength", 24}},
        {{"buffer", 0}, {"byteOffset", 96}, {"byteLength", 6}},
    });
    gltf["accessors"] = nlohmann::json::array({
        {{"bufferView", 0},
         {"componentType", 5126},
         {"count", 3},
         {"type", "VEC3"},
         {"min", nlohmann::json::array({0, 0, 0})},
         {"max", nlohmann::json::array({1, 1, 0})}},
        {{"bufferView", 1}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
        {{"bufferView", 2}, {"componentType", 5126}, {"count", 3}, {"type", "VEC2"}},
        {{"bufferView", 3}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}},
    });
    gltf["images"] = nlohmann::json::array({{{"uri", "shared.ppm"}, {"name", "SharedImage"}}});
    gltf["textures"] = nlohmann::json::array({{{"source", 0}}});
    gltf["materials"] =
        nlohmann::json::array({{{"name", "MatA"}, {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}}},
                               {{"name", "MatB"}, {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}}}});
    gltf["meshes"] = nlohmann::json::array(
        {{{"name", "TwoMaterials"},
          {"primitives",
           nlohmann::json::array(
               {{{"attributes", {{"POSITION", 0}, {"NORMAL", 1}, {"TEXCOORD_0", 2}}}, {"indices", 3}, {"material", 0}},
                {{"attributes", {{"POSITION", 0}, {"NORMAL", 1}, {"TEXCOORD_0", 2}}},
                 {"indices", 3},
                 {"material", 1}}})}}});
    gltf["nodes"] = nlohmann::json::array({{{"mesh", 0}}});
    gltf["scenes"] = nlohmann::json::array({{{"nodes", nlohmann::json::array({0})}}});
    gltf["scene"] = 0;
    {
        std::ofstream output(gltfPath);
        output << gltf.dump(2);
    }

    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    ModelHandle model = manager.Load<ModelAsset>(gltfPath.string());
    if (!Check(model.IsValid(), "shared-texture glTF import failed"))
        return false;
    if (!Check(model->MaterialCount() == 2, "shared-texture glTF material count mismatch"))
        return false;
    TextureHandle first = model->GetMaterial(0)->GetTexture("BaseColorMap");
    TextureHandle second = model->GetMaterial(1)->GetTexture("BaseColorMap");
    if (!Check(first.IsValid() && second.IsValid(), "shared-texture glTF did not import material textures"))
        return false;
    if (!Check(first.Get() == second.Get(), "glTF importer created duplicate TextureAsset instances for one image"))
        return false;
    if (!Check(first->GetPath() == gltfPath.string() + "#texture-0-srgb",
               "glTF imported texture subasset path is not stable"))
        return false;
    if (!Check(manager.GetByPath<TextureAsset>(gltfPath.string() + "#texture-0-srgb").Get() == first.Get(),
               "stable glTF texture subasset path was not registered"))
        return false;

    manager.Clear();
    fs::remove_all(root, ec);
    return true;
}

bool TestGltfLibraryModelCache() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_gltf_library_cache_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Content" / "Models");
    fs::create_directories(root / ".myengine");
    const fs::path gltfPath = root / "Content" / "Models" / "cached_triangle.gltf";
    const fs::path binPath = root / "Content" / "Models" / "cached_triangle.bin";
    WriteSimpleTriangleGltf(gltfPath, binPath);

    AssetImportService imports;
    std::string error;
    if (!Check(imports.OpenProject(root, &error), error))
        return false;
    AssetImportReport report = imports.ImportSource(gltfPath, "{}", {}, &error);
    if (!Check(report.succeeded, "glTF Library import failed: " + error))
        return false;
    if (!Check(fs::path(report.record.artifactPath).extension() == ".modelbin",
               "glTF Library artifact should be a native model cache"))
        return false;
    if (!Check(fs::is_regular_file(report.record.artifactPath), "model cache artifact was not written"))
        return false;
    const std::string firstArtifact = report.record.artifactPath;

    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    manager.SetProjectRoot(root);
    ModelHandle model = manager.Load<ModelAsset>("Content/Models/cached_triangle.gltf");
    if (!Check(model.IsValid(), "AssetManager did not resolve glTF through Library model cache"))
        return false;
    if (!Check(fs::path(model->GetPath()).extension() == ".modelbin",
               "AssetManager loaded raw glTF instead of cached modelbin"))
        return false;
    if (!Check(model->GetMesh() && model->GetMesh()->VertexCount() == 3 && model->GetMesh()->IndexCount() == 3,
               "cached model geometry mismatch"))
        return false;
    if (!Check(manager.GetByPath<ModelAsset>(gltfPath.string()).Get() == model.Get(),
               "source glTF path was not mapped to cached model asset"))
        return false;
    MeshHandle mesh = manager.Load<MeshAsset>("Content/Models/cached_triangle.gltf#mesh");
    if (!Check(mesh.IsValid() && mesh.Get() == model->GetMesh().Get(),
               "source glTF mesh subasset was not aliased to cached modelbin mesh"))
        return false;
    if (!Check(manager.MakeProjectRelativePath(mesh->GetPath()) == "Content/Models/cached_triangle.gltf#mesh",
               "cached modelbin mesh serialized as a Library artifact path"))
        return false;

    WriteSimpleTriangleGltf(gltfPath, binPath, 2.0f);
    AssetImportReport updated = imports.Reimport(report.record.uuid, &error);
    if (!Check(updated.succeeded, "glTF Library reimport failed: " + error))
        return false;
    if (!Check(updated.record.artifactPath != firstArtifact,
               "external glTF buffer changes did not invalidate the model cache key"))
        return false;
    if (!Check(fs::is_regular_file(updated.record.artifactPath), "updated model cache artifact was not written"))
        return false;

    manager.Clear();
    manager.SetProjectRoot({});
    fs::remove_all(root, ec);
    return true;
}

bool TestModelCacheRestoresTextureMips() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_model_cache_mips_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const fs::path cachePath = root / "textured.modelbin";

    TextureDesc desc;
    desc.width = 4;
    desc.height = 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(desc.width * desc.height * 4), 255);
    auto texture = std::make_shared<TextureAsset>((root / "texture").string());
    texture->SetName("MipTexture");
    texture->SetPixelData(std::move(pixels), desc);
    if (!Check(texture->GetMips().size() == 3, "source texture mip generation failed"))
        return false;

    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    TextureHandle textureHandle = manager.Register(texture);
    auto material = std::make_shared<MaterialAsset>((root / "material").string());
    material->SetName("MipMaterial");
    material->SetTexture("BaseColorMap", textureHandle);
    material->MarkReady();
    MaterialHandle materialHandle = manager.Register(material);
    MeshHandle mesh = manager.Register(MeshAsset::CreateTriangle("MipMesh"));
    auto model = ModelAsset::Create("MipModel", mesh, materialHandle);

    if (!Check(SaveModelCacheAssetToFile(*model, cachePath.string()), "failed to save model cache with texture mips"))
        return false;
    manager.Clear();
    std::shared_ptr<ModelAsset> loaded = LoadModelCacheAssetFromFile(cachePath.string());
    if (!Check(loaded && loaded->MaterialCount() == 1, "failed to load model cache with texture mips"))
        return false;
    MaterialHandle loadedMaterial = loaded->GetMaterial(0);
    if (!Check(loadedMaterial.IsValid(), "cached material missing"))
        return false;
    if (!Check(manager.GetByPath<MaterialAsset>(cachePath.string() + "#material-0").Get() == loadedMaterial.Get(),
               "model cache material subasset path was not registered"))
        return false;
    TextureHandle loadedTexture = loadedMaterial->GetTexture("BaseColorMap");
    if (!Check(loadedTexture.IsValid(), "cached material texture missing"))
        return false;
    if (!Check(loadedTexture->HasDeferredPayload(), "model cache did not defer texture payload loading"))
        return false;
    if (!Check(loadedTexture->GetMips().empty(), "deferred texture payload was loaded during model cache hit"))
        return false;
    if (!Check(loadedTexture->EnsurePayloadLoaded(), "deferred texture payload failed to load"))
        return false;
    if (!Check(loadedTexture->GetMips().size() == 3 && loadedTexture->GetMips()[1].width == 2 &&
                   loadedTexture->GetMips()[2].width == 1,
               "model cache did not restore prebuilt texture mip chain"))
        return false;
    if (!Check(loadedTexture->GetCompressedBc3Mip(0).size() == 16,
               "model cache did not restore BC3/DXT5 texture payload"))
        return false;

    manager.Clear();
    fs::remove_all(root, ec);
    return true;
}

bool TestEditorWarmsSceneGltfModelCache() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_editor_gltf_cache_warm_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Content" / "Models");
    fs::create_directories(root / "Content" / "Scenes");
    fs::create_directories(root / ".myengine");
    const fs::path gltfPath = root / "Content" / "Models" / "warm_triangle.gltf";
    const fs::path binPath = root / "Content" / "Models" / "warm_triangle.bin";
    const fs::path scenePath = root / "Content" / "Scenes" / "Warm.scene.json";
    WriteSimpleTriangleGltf(gltfPath, binPath);
    {
        nlohmann::json scene;
        scene["name"] = "Warm";
        scene["actors"] = nlohmann::json::array(
            {{{"name", "Mesh"},
              {"components", {{"MeshRendererComponent", {{"mesh", "Content/Models/warm_triangle.gltf#mesh"}}}}}}});
        std::ofstream(scenePath) << scene.dump(2);
    }

    AssetMeta meta = AssetMeta::Create(gltfPath.string());
    if (!Check(AssetMeta::Save(meta), "failed to create warmup test metadata"))
        return false;
    const fs::path oldArtifact = root / "Library" / "windows-x64" / meta.uuid / "legacy.gltf";
    fs::create_directories(oldArtifact.parent_path());
    fs::copy_file(gltfPath, oldArtifact, fs::copy_options::overwrite_existing, ec);
    if (!Check(!ec, "failed to create legacy glTF artifact"))
        return false;
    AssetDatabase database;
    std::string error;
    if (!Check(database.Open(root / ".myengine" / "AssetDatabase.json", &error), error))
        return false;
    AssetRecord legacy;
    legacy.uuid = meta.uuid;
    legacy.sourcePath = gltfPath.generic_string();
    legacy.artifactPath = oldArtifact.generic_string();
    legacy.type = "model";
    legacy.importer = "model-sdf-voxel";
    legacy.importerVersion = 1;
    legacy.sourceHash = "legacy";
    legacy.artifactHash = "legacy";
    legacy.state = AssetImportState::Ready;
    if (!Check(database.Upsert(legacy, &error) && database.Save(&error), error))
        return false;

    EditorContext context;
    context.SetProjectRoot(root);
    EditorImportService imports;
    imports.OnAttach(context);
    std::vector<std::string> failures;
    const size_t warmed = imports.EnsureModelCachesForScene(scenePath, &failures);
    if (!Check(failures.empty(), failures.empty() ? std::string{} : failures.front()))
        return false;
    if (!Check(warmed == 1, "scene model cache warmup did not process the glTF reference"))
        return false;

    AssetDatabase updated;
    if (!Check(updated.Open(root / ".myengine" / "AssetDatabase.json", &error), error))
        return false;
    const AssetRecord* record = updated.FindBySourcePath(gltfPath.generic_string());
    if (!Check(record && fs::path(record->artifactPath).extension() == ".modelbin",
               "scene model cache warmup did not migrate legacy glTF artifact to modelbin"))
        return false;
    if (!Check(fs::is_regular_file(record->artifactPath), "scene model cache warmup did not write modelbin artifact"))
        return false;

    fs::remove_all(root, ec);
    return true;
}

bool TestAssetAsyncLoadingAndHotReload() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_asset_reload_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path texturePath = root / "reload.ppm";

    auto writeTexture = [&texturePath](uint8_t red, uint8_t green, uint8_t blue) {
        std::ofstream output(texturePath, std::ios::binary);
        output << "P6\n1 1\n255\n";
        const char pixel[3] = {static_cast<char>(red), static_cast<char>(green), static_cast<char>(blue)};
        output.write(pixel, sizeof(pixel));
    };

    writeTexture(255, 0, 0);
    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    std::shared_ptr<Asset> loaded = manager.LoadAsync(texturePath.string()).Get();
    auto texture = std::dynamic_pointer_cast<TextureAsset>(loaded);
    if (!Check(texture && texture->IsReady(), "async texture load failed"))
        return false;
    if (!Check(texture->GetVersion() == 1 && texture->GetPixelData()[0] == 255, "async texture contents mismatch"))
        return false;

    TextureAsset* originalAddress = texture.get();
    const auto previousWriteTime = fs::last_write_time(texturePath);
    writeTexture(0, 255, 0);
    fs::last_write_time(texturePath, previousWriteTime + std::chrono::seconds(2));

    if (!Check(manager.PollHotReload() == 1, "hot reload did not detect source change"))
        return false;
    auto reloaded = manager.GetByPath<TextureAsset>(texturePath.string());
    if (!Check(reloaded.Get() == originalAddress, "hot reload invalidated an existing asset handle"))
        return false;
    if (!Check(reloaded->GetVersion() == 2 && reloaded->GetPixelData()[0] == 0 && reloaded->GetPixelData()[1] == 255,
               "hot reload did not update texture contents or version"))
        return false;

    manager.Unload(texturePath.string());
    if (!Check(!manager.IsLoaded(texturePath.string()), "path-based unload failed for UUID-backed asset"))
        return false;
    manager.Clear();
    fs::remove_all(root);
    return true;
}

bool TestAssetManagerFailureRollback() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_asset_failure_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path assetPath = root / "rollback.robustasset";
    {
        std::ofstream output(assetPath, std::ios::binary);
        output << "version 1";
    }

    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    int loaderMode = 0;
    manager.RegisterLoader("robustasset", [&loaderMode](const std::string& path) -> std::shared_ptr<Asset> {
        if (loaderMode == 1) {
            return std::static_pointer_cast<Asset>(MeshAsset::CreateTriangle("WrongType"));
        }
        if (loaderMode == 2) {
            throw std::runtime_error("loader failed intentionally");
        }
        auto texture = std::make_shared<TextureAsset>(path);
        TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        texture->SetPixelData({17, 0, 0, 255}, desc);
        return std::static_pointer_cast<Asset>(texture);
    });
    manager.RegisterLoader("throwasset", [](const std::string& path) -> std::shared_ptr<Asset> {
        (void)path;
        throw std::runtime_error("async loader failed intentionally");
    });

    TextureHandle texture = manager.Load<TextureAsset>(assetPath.string());
    if (!Check(texture.IsValid(), "initial robust asset load failed"))
        return false;
    TextureAsset* original = texture.Get();
    const uint64_t version = texture->GetVersion();
    if (!Check(texture->GetPixelData()[0] == 17, "initial robust asset contents mismatch"))
        return false;

    const auto previousWriteTime = fs::last_write_time(assetPath);
    {
        std::ofstream output(assetPath, std::ios::binary | std::ios::trunc);
        output << "wrong type";
    }
    fs::last_write_time(assetPath, previousWriteTime + std::chrono::seconds(2));
    loaderMode = 1;
    if (!Check(manager.PollHotReload() == 0, "wrong-type reload should fail"))
        return false;
    if (!Check(texture.Get() == original && texture->GetVersion() == version && texture->GetPixelData()[0] == 17,
               "wrong-type reload mutated existing asset"))
        return false;

    {
        std::ofstream output(assetPath, std::ios::binary | std::ios::trunc);
        output << "throw";
    }
    fs::last_write_time(assetPath, previousWriteTime + std::chrono::seconds(4));
    loaderMode = 2;
    if (!Check(manager.PollHotReload() == 0, "throwing reload should fail"))
        return false;
    if (!Check(texture.Get() == original && texture->GetVersion() == version && texture->GetPixelData()[0] == 17,
               "throwing reload mutated existing asset"))
        return false;

    const fs::path throwPath = root / "async.throwasset";
    {
        std::ofstream output(throwPath, std::ios::binary);
        output << "throw";
    }
    std::shared_ptr<Asset> asyncResult = manager.LoadAsync(throwPath.string()).Get();
    if (!Check(!asyncResult && !manager.IsLoaded(throwPath.string()),
               "async loader exception should not cache an asset"))
        return false;

    manager.Clear();
    fs::remove_all(root);
    return true;
}

bool TestMeshDerivedData() {
    auto mesh = MeshAsset::CreateCube("DerivedDataCube");
    if (!Check(mesh->GetLods().size() >= 2, "mesh LOD chain was not generated"))
        return false;
    if (!Check(mesh->GetLod(0).indices.size() == mesh->GetIndices().size() &&
                   mesh->GetLod(1).indices.size() < mesh->GetLod(0).indices.size(),
               "mesh LOD index counts are invalid"))
        return false;
    const MeshColliderData& collider = mesh->GetColliderData();
    if (!Check(collider.vertices.size() == 8 && collider.indices.size() == 36,
               "automatic box collider data was not generated"))
        return false;
    return Check(NearlyEqual(collider.bounds.min.x, -0.5f) && NearlyEqual(collider.bounds.max.x, 0.5f),
                 "automatic collider bounds mismatch");
}

bool TestTextureDerivedData() {
    TextureDesc desc;
    desc.width = 4;
    desc.height = 4;
    std::vector<uint8_t> pixels(4 * 4 * 4, 255);
    auto texture = std::make_shared<TextureAsset>("__builtin__/DerivedTexture");
    texture->SetPixelData(std::move(pixels), desc);
    if (!Check(texture->GetMipLevels() == 3 && texture->GetMips().size() == 3, "texture mip chain was not generated"))
        return false;
    if (!Check(texture->GetMips()[1].width == 2 && texture->GetMips()[2].width == 1,
               "texture mip dimensions are invalid"))
        return false;
    if (!Check(texture->GetCompressedMip(0).empty() && texture->GetCompressedMip(2).empty() &&
                   texture->GetCompressedBc3Mip(0).empty(),
               "texture compression should be explicit for runtime imports"))
        return false;
    texture->GenerateCompressedMips();
    return Check(texture->GetCompressedMip(0).size() == 8 && texture->GetCompressedBc3Mip(0).size() == 16 &&
                     texture->GetCompressedMip(2).size() == 8,
                 "explicit block texture compression output size mismatch");
}

bool TestTextureSamplerSettingsFromAssetDatabase() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_texture_sampler_settings_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "SourceAssets");
    fs::create_directories(root / ".myengine");
    const fs::path texturePath = root / "SourceAssets" / "tile.ppm";
    {
        std::ofstream output(texturePath, std::ios::binary);
        output << "P6\n1 1\n255\n";
        const unsigned char pixel[3] = {255, 255, 255};
        output.write(reinterpret_cast<const char*>(pixel), 3);
    }
    const nlohmann::json settings = {
        {"textureSampler", {{"filter", "nearest"}, {"wrapU", "clamp"}, {"wrapV", "clamp"}}}};
    const nlohmann::json database = {{"version", 1},
                                     {"assets", nlohmann::json::array({{{"uuid", "texture-sampler-test"},
                                                                        {"sourcePath", texturePath.generic_string()},
                                                                        {"artifactPath", ""},
                                                                        {"type", "texture"},
                                                                        {"importer", "passthrough"},
                                                                        {"importerVersion", 1},
                                                                        {"sourceHash", "test"},
                                                                        {"artifactHash", ""},
                                                                        {"settings", settings.dump()},
                                                                        {"dependencies", nlohmann::json::array()},
                                                                        {"state", "ready"},
                                                                        {"diagnostics", nlohmann::json::array()},
                                                                        {"alwaysCook", false}}})}};
    std::ofstream(root / ".myengine" / "AssetDatabase.json") << database.dump(2);

    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    TextureHandle texture = manager.Load<TextureAsset>(texturePath.string());
    const bool ok = texture.IsValid() && texture->GetFilter() == TextureFilter::Nearest &&
                    texture->GetWrapU() == TextureWrap::Clamp && texture->GetWrapV() == TextureWrap::Clamp;
    manager.Clear();
    fs::remove_all(root, ec);
    return Check(ok, "texture sampler settings were not restored from AssetDatabase");
}

bool TestShaderLibraryArtifactCacheHit() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_shader_library_cache_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Content" / "Shaders");
    fs::create_directories(root / ".myengine");

    const fs::path source = root / "Content" / "Shaders" / "Cached.shader";
    std::ofstream(source)
        << R"({"type":"Shader","version":1,"stages":{"vertex":{"source":"Cached.hlsl","entry":"VSMain"},"pixel":{"source":"Cached.hlsl","entry":"PSMain"}},"defines":[]})";

    std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, kShaderBackendCount> blobs{};
    blobs[static_cast<size_t>(ShaderBackend::D3D11)][static_cast<size_t>(ShaderStage::Vertex)] = {1, 2, 3};
    blobs[static_cast<size_t>(ShaderBackend::D3D11)][static_cast<size_t>(ShaderStage::Pixel)] = {4, 5};
    blobs[static_cast<size_t>(ShaderBackend::D3D12)][static_cast<size_t>(ShaderStage::Vertex)] = {6, 7, 8};
    blobs[static_cast<size_t>(ShaderBackend::D3D12)][static_cast<size_t>(ShaderStage::Pixel)] = {9, 10};

    const std::string uuid = "shader-cache-test";
    const fs::path artifact = root / "Library" / "windows-x64" / uuid / "cached.shader";
    fs::create_directories(artifact.parent_path());
    ShaderAsset cooked(artifact.string());
    cooked.SetCooked(ShaderAsset::kVertexMask | ShaderAsset::kPixelMask, 77, std::move(blobs));
    std::string error;
    if (!Check(SaveCookedShaderAsset(cooked, artifact, &error), error))
        return false;

    const nlohmann::json database = {
        {"version", 1},
        {"assets", nlohmann::json::array(
                       {{{"uuid", uuid},
                         {"sourcePath", source.generic_string()},
                         {"artifactPath", artifact.generic_string()},
                         {"type", "shader"},
                         {"importer", "shader"},
                         {"importerVersion", 1},
                         {"sourceHash", "test"},
                         {"artifactHash", ""},
                         {"settings", "{}"},
                         {"dependencies",
                          nlohmann::json::array({(root / "Content" / "Shaders" / "Cached.hlsl").generic_string()})},
                         {"state", "ready"},
                         {"diagnostics", nlohmann::json::array()},
                         {"alwaysCook", false}}})}};
    std::ofstream(root / ".myengine" / "AssetDatabase.json") << database.dump(2);

    AssetDatabase importedDatabase;
    if (!Check(importedDatabase.Open(root / ".myengine" / "AssetDatabase.json", &error),
               "shader artifact database failed to open: " + error))
        return false;
    AssetDatabaseValidationReport validation;
    if (!Check(importedDatabase.ValidateAgainstProject(root, validation),
               "shader source include dependencies should not be treated as asset UUIDs: " + validation.Summary()))
        return false;

    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    manager.SetProjectRoot(root);
    ShaderAssetHandle shader = manager.Load<ShaderAsset>("Content/Shaders/Cached.shader");
    const bool ok = shader.IsValid() && shader->IsCooked() && shader->GetUuid() == uuid &&
                    shader->GetBytecode(ShaderBackend::D3D12, ShaderStage::Pixel).size() == 2;
    manager.Clear();
    manager.SetProjectRoot({});
    fs::remove_all(root, ec);
    return Check(ok, "shader source did not resolve to Library cooked artifact");
}

MYENGINE_REGISTER_TEST("Assets", "TestShaderAssetFormats", TestShaderAssetFormats);
MYENGINE_REGISTER_TEST("Assets", "TestPbrMaterialParameters", TestPbrMaterialParameters);
MYENGINE_REGISTER_TEST("Assets", "TestMaterialAssetFileRoundTrip", TestMaterialAssetFileRoundTrip);
MYENGINE_REGISTER_TEST("Assets", "TestAssetManagerSharedAcrossRuntimeBoundary",
                       TestAssetManagerSharedAcrossRuntimeBoundary);
MYENGINE_REGISTER_TEST("Assets", "TestAssetFileImporters", TestAssetFileImporters);
MYENGINE_REGISTER_TEST("Assets", "TestAudioClipAssetLoader", TestAudioClipAssetLoader);
MYENGINE_REGISTER_TEST("Assets", "TestGltfImportAndStableMeta", TestGltfImportAndStableMeta);
MYENGINE_REGISTER_TEST("Assets", "TestGltfImporterDeduplicatesSharedTextures",
                       TestGltfImporterDeduplicatesSharedTextures);
MYENGINE_REGISTER_TEST("Assets", "TestGltfLibraryModelCache", TestGltfLibraryModelCache);
MYENGINE_REGISTER_TEST("Assets", "TestModelCacheRestoresTextureMips", TestModelCacheRestoresTextureMips);
MYENGINE_REGISTER_TEST("Assets", "TestEditorWarmsSceneGltfModelCache", TestEditorWarmsSceneGltfModelCache);
MYENGINE_REGISTER_TEST("Assets", "TestAssetAsyncLoadingAndHotReload", TestAssetAsyncLoadingAndHotReload);
MYENGINE_REGISTER_TEST("Assets", "TestAssetManagerFailureRollback", TestAssetManagerFailureRollback);
MYENGINE_REGISTER_TEST("Assets", "TestMeshDerivedData", TestMeshDerivedData);
MYENGINE_REGISTER_TEST("Assets", "TestTextureDerivedData", TestTextureDerivedData);
MYENGINE_REGISTER_TEST("Assets", "TestTextureSamplerSettingsFromAssetDatabase",
                       TestTextureSamplerSettingsFromAssetDatabase);
MYENGINE_REGISTER_TEST("Assets", "TestShaderLibraryArtifactCacheHit", TestShaderLibraryArtifactCacheHit);

} // namespace
