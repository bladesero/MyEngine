#include "TestHarness.h"
#include "Assets/ShaderAsset.h"
#include "Renderer/ShaderCompilerSlang.h"

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace {
std::filesystem::path FindInputShader(const char* name) {
    for (auto root : {std::filesystem::current_path(), gExecutableDirectory}) {
        for (unsigned depth = 0; depth < 8 && !root.empty(); ++depth) {
            const auto candidate = root / "EngineContent" / "Shaders" / name;
            if (std::filesystem::is_regular_file(candidate))
                return candidate;
            const auto parent = root.parent_path();
            if (parent == root)
                break;
            root = parent;
        }
    }
    return {};
}

bool HasInput(const CookedShaderStageReflection& reflection, const char* semantic, uint32_t index, uint32_t location) {
    for (const auto& input : reflection.inputs)
        if (input.semantic == semantic && input.semanticIndex == index && input.location == location)
            return true;
    return false;
}

bool TestSlangVertexInputLocations() {
    struct Case {
        const char* source;
        std::vector<std::string> defines;
        uint32_t colorLocation;
        uint32_t uvLocation;
    };
    const Case cases[] = {{"Mesh.hlsl", {}, 4, 3},
                          {"ShadowDepth.hlsl", {}, 2, 1},
                          {"ShadowDepth.hlsl", {"MYENGINE_SHADOW_SKINNED=1"}, 4, 1}};
    for (const auto& test : cases) {
        const auto path = FindInputShader(test.source);
        if (!Check(!path.empty(), std::string("shader input fixture missing: ") + test.source))
            return false;
        CookedShaderStageReflection reflection;
        std::vector<uint8_t> bytecode;
        std::string error;
        if (!Check(ShaderCompilerSlang::CompileStageFromFile(path, "VSMain", ShaderStage::Vertex, ShaderBackend::Vulkan,
                                                             bytecode, test.defines, &error, &reflection),
                   error))
            return false;
        if (!Check(HasInput(reflection, "POSITION", 0, 0) && HasInput(reflection, "TEXCOORD", 0, test.uvLocation) &&
                       HasInput(reflection, "COLOR", 0, test.colorLocation),
                   std::string("Slang locations must match this shader's actual input subset: ") + test.source))
            return false;
        if (!test.defines.empty() &&
            !Check(HasInput(reflection, "BLENDINDICES", 0, 2) && HasInput(reflection, "BLENDWEIGHT", 0, 3),
                   "skinned shadow inputs must retain their own locations"))
            return false;
    }
    return true;
}

bool TestCookedVertexInputReflectionRoundTrip() {
    namespace fs = std::filesystem;
    struct Directory {
        fs::path path;
        ~Directory() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } directory{
        fs::temp_directory_path() /
        ("myengine-vertex-inputs-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    if (!fs::create_directory(directory.path))
        return Check(false, "failed to create shader reflection test directory");
    // Exercise nested struct offsets and a nonzero semantic index through the real compiler, not just JSON IO.
    const auto nestedSource = directory.path / "NestedInputs.hlsl";
    {
        std::ofstream source(nestedSource);
        source << "struct Inner { float3 position : POSITION; float2 uv : TEXCOORD1; };\n"
                  "struct Input { float4 color : COLOR0; Inner nested; };\n"
                  "float4 VSMain(Input v) : SV_Position { return float4(v.nested.position + "
                  "float3(v.nested.uv, 0) * 0.001 + v.color.xyz * 0.001, 1); }\n";
    }
    CookedShaderStageReflection nestedReflection;
    std::vector<uint8_t> nestedBytecode;
    std::string nestedError;
    if (!Check(ShaderCompilerSlang::CompileStageFromFile(nestedSource, "VSMain", ShaderStage::Vertex,
                                                         ShaderBackend::Vulkan, nestedBytecode, {}, &nestedError,
                                                         &nestedReflection),
               nestedError) ||
        !Check(HasInput(nestedReflection, "COLOR", 0, 0) && HasInput(nestedReflection, "POSITION", 0, 1) &&
                   HasInput(nestedReflection, "TEXCOORD", 1, 2),
               "nested vertex input offsets and semantic index were not preserved"))
        return false;
    std::array<std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, static_cast<size_t>(ShaderPass::Count)>,
               kShaderBackendCount>
        blobs{};
    const auto backend = static_cast<size_t>(ShaderBackend::Vulkan);
    const auto stage = static_cast<size_t>(ShaderStage::Vertex);
    blobs[backend][0][stage] = {1, 2, 3, 4};
    blobs[backend][0][static_cast<size_t>(ShaderStage::Pixel)] = {5, 6, 7, 8};
    CookedShaderReflectionTable reflections{};
    reflections[backend][0][stage].inputs = {{"POSITION", 0, 0}, {"TEXCOORD", 1, 3}, {"COLOR", 0, 4}};
    ShaderAsset asset("InputReflection.shader");
    asset.SetCookedPasses(1u, 42, ShaderSourceMode::Code, ShaderDomain::Graphics, ShaderShadingModel::Unlit,
                          ShaderSurfaceType::Opaque, {}, std::move(blobs), std::move(reflections));
    const auto path = directory.path / "InputReflection.shader";
    std::string error;
    if (!Check(SaveCookedShaderAsset(asset, path, &error), error))
        return false;
    auto loaded = LoadShaderAssetFromFile(path.string());
    if (!Check(loaded &&
                   HasInput(loaded->GetReflection(ShaderBackend::Vulkan, ShaderPass::Default, ShaderStage::Vertex),
                            "TEXCOORD", 1, 3) &&
                   HasInput(loaded->GetReflection(ShaderBackend::Vulkan, ShaderPass::Default, ShaderStage::Vertex),
                            "COLOR", 0, 4),
               "cooked shader lost semantic index or location"))
        return false;

    // The current container's optional JSON extension must not prevent loading older v8 artifacts.
    std::ifstream file(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    constexpr size_t metadataSizeOffset = 8 + 4 + 4 + 8 + 4;
    constexpr size_t metadataOffset = metadataSizeOffset + sizeof(uint64_t);
    uint64_t metadataSize = 0;
    if (!Check(bytes.size() >= metadataOffset, "cooked test container truncated"))
        return false;
    std::memcpy(&metadataSize, bytes.data() + metadataSizeOffset, sizeof(metadataSize));
    if (!Check(metadataSize <= bytes.size() - metadataOffset, "cooked test metadata truncated"))
        return false;
    auto metadata = nlohmann::json::parse(bytes.substr(metadataOffset, static_cast<size_t>(metadataSize)));
    for (auto& entry : metadata["reflection"])
        entry.erase("inputs");
    const std::string oldMetadata = metadata.dump();
    const uint64_t oldSize = oldMetadata.size();
    std::string legacy = bytes.substr(0, metadataSizeOffset);
    legacy.append(reinterpret_cast<const char*>(&oldSize), sizeof(oldSize));
    legacy += oldMetadata;
    legacy += bytes.substr(metadataOffset + static_cast<size_t>(metadataSize));
    file.close();
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(legacy.data(), static_cast<std::streamsize>(legacy.size()));
    }
    loaded = LoadShaderAssetFromFile(path.string());
    return Check(
        loaded && loaded->GetReflection(ShaderBackend::Vulkan, ShaderPass::Default, ShaderStage::Vertex).inputs.empty(),
        "older cooked metadata without inputs must remain readable");
}

MYENGINE_REGISTER_TEST("Renderer", "TestSlangVertexInputLocations", TestSlangVertexInputLocations);
MYENGINE_REGISTER_TEST("Assets", "TestCookedVertexInputReflectionRoundTrip", TestCookedVertexInputReflectionRoundTrip);
} // namespace
