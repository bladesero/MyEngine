#include "TestHarness.h"

#include "Assets/AssetManager.h"
#include "Assets/AssetDatabase.h"
#include "Assets/ShaderAsset.h"
#include "Core/Sha256.h"
#include "Core/TransactionalFileWriter.h"
#include "Editor/CookDependencyGraph.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorWorkspace.h"
#include "Editor/ProjectPublisher.h"
#include "Editor/ProjectValidator.h"
#include "Project/ContentArchive.h"
#include "Project/ContentPathPolicy.h"
#include "Project/CookedProjectCache.h"
#include "Project/CookManifest.h"
#include "Project/ProjectConfig.h"
#include "Project/PublishTargets.h"
#include "Project/RuntimeCompatibility.h"
#include "Project/RuntimeDependencies.h"
#include "Project/RuntimePerformanceProfile.h"
#include "Project/JsonMigrationRegistry.h"
#include "Project/SaveGame.h"
#include "Input/InputActionMap.h"
#include "Assets/PrefabAsset.h"
#include "Renderer/EngineShaderCatalog.h"

#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scripting/ScriptComponent.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

class ScopedRuntimeFileSystem {
public:
    explicit ScopedRuntimeFileSystem(std::shared_ptr<IReadOnlyFileSystem> fileSystem) {
        RuntimeFileSystem::Set(std::move(fileSystem));
    }

    ~ScopedRuntimeFileSystem() { RuntimeFileSystem::Set({}); }
};

template <typename T> bool WriteBinaryValue(std::ostream& output, const T& value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return output.good();
}

bool WriteTestContentArchive(const std::filesystem::path& archive,
                             const std::vector<std::pair<std::string, std::string>>& entries, bool corruptFirstHash,
                             bool addTrailingData) {
    std::error_code ec;
    std::filesystem::create_directories(archive.parent_path(), ec);
    std::ofstream output(archive, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    constexpr std::array<char, 8> magic = {'M', 'E', 'P', 'A', 'K', '0', '2', '\0'};
    output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    const uint32_t fileCount = static_cast<uint32_t>(entries.size());
    if (!WriteBinaryValue(output, fileCount))
        return false;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& [path, payload] = entries[i];
        const uint32_t pathLength = static_cast<uint32_t>(path.size());
        const uint64_t size = static_cast<uint64_t>(payload.size());
        Sha256 hash;
        if (!payload.empty())
            hash.Update(payload.data(), payload.size());
        Sha256::Digest digest = hash.Final();
        if (corruptFirstHash && i == 0)
            digest[0] ^= 0xff;
        if (!WriteBinaryValue(output, pathLength) || !WriteBinaryValue(output, size))
            return false;
        output.write(reinterpret_cast<const char*>(digest.data()), static_cast<std::streamsize>(digest.size()));
        output.write(path.data(), static_cast<std::streamsize>(path.size()));
        output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
    if (addTrailingData)
        output << "trailing";
    return output.good();
}

bool TestPublishHardeningPrimitives() {
    Sha256 sha;
    sha.Update("abc", 3);
    if (!Check(Sha256::ToHex(sha.Final()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
               "SHA-256 known vector mismatch"))
        return false;

    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "myengine_publish_hardening_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Content/Scenes");
    std::ofstream(root / "outside.bin") << "outside";
    fs::path resolved;
    std::string error;
    AssetDatabase database;
    const auto dbPath = root / ".myengine" / "AssetDatabase.json";
    if (!Check(!ContentPathPolicy::ResolveContained(root / "Content", "../outside.bin", resolved, &error),
               "Content path traversal was accepted"))
        return false;

    std::ofstream(root / "Content/Scenes/Main.scene.json")
        << R"({"actors":[{"components":[{"data":{"language":"angelscript","scriptPath":"Content/Scripts/missing.as"}}]}]})";
    PublishPreflightReport preflight;
    if (!Check(!CookDependencyGraph::Validate(root, preflight) && !preflight.errors.empty() &&
                   preflight.errors.front().code == PublishIssueCode::MissingDependency,
               "publish preflight accepted a missing script dependency"))
        return false;

    fs::remove_all(root / "Content/Scenes", ec);
    fs::create_directories(root / "Content/Scenes");
    fs::create_directories(root / "Content/Audio");
    std::ofstream(root / "Content/Audio/beep.wav", std::ios::binary) << "audio";
    std::ofstream(root / "Content/Scenes/Main.scene.json")
        << R"({"actors":[{"components":[{"data":{"clip":"Content/Audio/beep.wav"}}]}]})";
    preflight = {};
    if (!Check(CookDependencyGraph::Validate(root, preflight), "publish preflight rejected an audio clip dependency"))
        return false;
    if (!Check(std::find(preflight.visitedAssets.begin(), preflight.visitedAssets.end(), "Content/Audio/beep.wav") !=
                   preflight.visitedAssets.end(),
               "publish preflight did not visit audio clip dependency"))
        return false;

    fs::remove_all(root / "Content/Scenes", ec);
    fs::create_directories(root / "Content/Scenes");
    fs::create_directories(root / "Content/Models");
    std::ofstream(root / "Content/Models/Thing.gltf") << R"({"asset":{"version":"2.0"}})";
    std::ofstream(root / "Content/Scenes/Main.scene.json")
        << std::string(R"({"actors":[{"components":[{"data":{"mesh":")") +
               (root / "Library/windows-x64/model-uuid/old.modelbin").generic_string() + R"(#mesh"}}]}]})";
    if (!Check(database.Open(dbPath, &error), "asset database open failed: " + error))
        return false;
    AssetRecord model;
    model.uuid = "model-uuid";
    model.sourcePath = (root / "Content/Models/Thing.gltf").generic_string();
    model.artifactPath = (root / "Library/windows-x64/model-uuid/current.modelbin").generic_string();
    model.type = "model";
    model.state = AssetImportState::Ready;
    fs::create_directories((root / "Library/windows-x64/model-uuid"), ec);
    std::ofstream(root / "Library/windows-x64/model-uuid/current.modelbin", std::ios::binary) << "model";
    if (!Check(database.Upsert(model, &error) && database.Save(&error),
               "asset database model record save failed: " + error))
        return false;
    preflight = {};
    if (!Check(CookDependencyGraph::Validate(root, preflight),
               "publish preflight rejected stale absolute model artifact reference: " + preflight.Summary()))
        return false;
    if (!Check(std::find(preflight.visitedAssets.begin(), preflight.visitedAssets.end(), "Content/Models/Thing.gltf") !=
                   preflight.visitedAssets.end(),
               "publish preflight did not resolve model artifact uuid to source asset"))
        return false;
    fs::remove_all(root / ".myengine", ec);
    fs::remove_all(root / "Library", ec);

    if (!Check(database.Open(dbPath, &error), "asset database open failed: " + error))
        return false;
    AssetRecord stale;
    stale.uuid = "stale-asset";
    stale.sourcePath = (root / "SourceAssets" / "missing.png").generic_string();
    stale.artifactPath = (root / "Library/windows-x64/stale-asset/missing.png").generic_string();
    stale.type = "texture";
    stale.state = AssetImportState::MissingSource;
    if (!Check(database.Upsert(stale, &error) && database.Save(&error),
               "asset database stale record save failed: " + error))
        return false;
    preflight = {};
    if (!Check(!CookDependencyGraph::Validate(root, preflight) && !preflight.errors.empty() &&
                   preflight.Summary().find("asset source is missing") != std::string::npos,
               "publish preflight accepted a stale asset database"))
        return false;
    fs::remove_all(root / ".myengine", ec);

    CookManifest manifest;
    manifest.project = "ContractTest";
    manifest.projectId = "project-id";
    manifest.engineVersion = RuntimeCompatibility::kEngineVersion;
    manifest.buildId = RuntimeCompatibility::kBuildId;
    manifest.contentSchemaVersion = RuntimeCompatibility::kContentSchemaVersion;
    manifest.archiveFormatVersion = RuntimeCompatibility::kArchiveFormatVersion;
    manifest.configuration = RuntimeCompatibility::kConfiguration;
#if defined(__APPLE__)
    manifest.requiredBackends = {"metal"};
#elif defined(MYENGINE_ENABLE_VULKAN)
    manifest.requiredBackends = {"d3d11", "d3d12", "vulkan"};
#else
    manifest.requiredBackends = {"d3d11", "d3d12"};
#endif
    manifest.runtimeDependenciesHash = std::string(64, '0');
    manifest.archiveHash = std::string(64, '1');
    manifest.startupScene = "Content/Scenes/Main.scene.json";
    manifest.files = {{manifest.startupScene, 0, std::string(64, '2')}};
    if (!Check(manifest.Validate(&error), "valid Manifest v2 was rejected: " + error))
        return false;
    manifest.version = 1;
    if (!Check(!manifest.Validate(&error) && error.find("unsupported") != std::string::npos,
               "legacy Manifest v1 was not rejected explicitly"))
        return false;
    manifest.version = CookManifest::kCurrentVersion;
#if defined(MYENGINE_ENABLE_VULKAN)
    manifest.requiredBackends = {"d3d11", "d3d12"};
    if (!Check(!manifest.Validate(&error), "manifest with missing Vulkan backend was accepted"))
        return false;
#else
    manifest.requiredBackends = {"d3d11", "d3d12", "vulkan"};
    if (!Check(!manifest.Validate(&error), "manifest with extra Vulkan backend was accepted"))
        return false;
#endif
#if defined(__APPLE__)
    manifest.requiredBackends = {"metal"};
#elif defined(MYENGINE_ENABLE_VULKAN)
    manifest.requiredBackends = {"d3d11", "d3d12", "vulkan"};
#else
    manifest.requiredBackends = {"d3d11", "d3d12"};
#endif

    fs::create_directories(root / "Runtime");
    const auto dependency = root / "Runtime/test.dll";
    std::ofstream(dependency, std::ios::binary) << "dependency";
    RuntimeDependencyManifest dependencies;
    dependencies.files.push_back({"test.dll", "x64", fs::file_size(dependency), Sha256::HashFile(dependency, &error)});
    if (!Check(dependencies.ValidateFiles(root / "Runtime", &error), "valid runtime dependency was rejected: " + error))
        return false;
    std::ofstream(dependency, std::ios::binary | std::ios::app) << "tampered";
    if (!Check(!dependencies.ValidateFiles(root / "Runtime", &error), "tampered runtime dependency was accepted"))
        return false;

    const auto package = root / "Package";
    fs::create_directories(package / "Content/Scenes");
    const auto packageDependency = package / "test.dll";
    std::ofstream(packageDependency, std::ios::binary) << "package dependency";
    RuntimeDependencyManifest packageDependencies;
    packageDependencies.files.push_back(
        {"test.dll", "x64", fs::file_size(packageDependency), Sha256::HashFile(packageDependency, &error)});
    if (!Check(packageDependencies.Save(package / RuntimeDependencyManifest::kFileName, &error),
               "failed to save runtime dependency manifest: " + error))
        return false;
    CookManifest packageManifest = manifest;
    packageManifest.runtimeDependenciesHash = Sha256::HashFile(package / RuntimeDependencyManifest::kFileName, &error);
    if (!Check(packageManifest.Save(package / CookManifest::kFileName, &error),
               "failed to save package Cook manifest: " + error))
        return false;
    if (!Check(RuntimeDependencyManifest::ValidatePackage(package, &error),
               "valid package runtime dependencies were rejected: " + error))
        return false;
    std::ofstream(package / RuntimeDependencyManifest::kFileName, std::ios::binary | std::ios::app) << " ";
    if (!Check(!RuntimeDependencyManifest::ValidatePackage(package, &error),
               "tampered runtime dependency manifest was accepted"))
        return false;
    packageDependencies.Save(package / RuntimeDependencyManifest::kFileName, &error);
    fs::remove(packageDependency);
    if (!Check(!RuntimeDependencyManifest::ValidatePackage(package, &error),
               "package with missing runtime dependency was accepted"))
        return false;
    fs::remove_all(root, ec);
    return true;
}

bool TestProjectConfigAndPortableAssetPaths() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() /
        ("myengine_project_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto scenes = root / "Content" / "Scenes";
    const auto meshes = root / "Content" / "Mesh";
    fs::create_directories(scenes);
    fs::create_directories(meshes);

    Scene startup("Startup");
    Scene alternate("Alternate");
    const auto startupPath = scenes / "Main.scene.json";
    const auto alternatePath = scenes / "Alternate.scene.json";
    if (!Check(SceneSerializer::SaveToFile(startup, startupPath.string()) &&
                   SceneSerializer::SaveToFile(alternate, alternatePath.string()),
               "project test scene creation failed"))
        return false;

    ProjectConfig project;
    std::string error;
    if (!Check(project.Open(root, true, &error), "missing project should open in editor mode"))
        return false;
    project.SetName("ProjectTest");
    project.GetGraphicsSettings().backend = "d3d12";
    project.GetGraphicsSettings().renderPath = "deferred";
    project.GetGraphicsSettings().deviceProfile = GraphicsDeviceProfile::Console;
    if (!Check(project.SetInputConfigPath("Content/Config/Input.input.json", &error),
               "project input config path save failed: " + error))
        return false;
    if (!Check(project.SetStartupScene(startupPath, &error) && project.Save(&error),
               "project startup scene save failed: " + error))
        return false;

    ProjectConfig loaded;
    if (!Check(loaded.Open(root, false, &error), "project manifest load failed: " + error))
        return false;
    if (!Check(loaded.GetVersion() == ProjectConfig::kCurrentVersion && loaded.GetName() == "ProjectTest" &&
                   loaded.GetStartupScene() == "Content/Scenes/Main.scene.json" &&
                   loaded.GetInputSettings().config == "Content/Config/Input.input.json" &&
                   loaded.GetGraphicsSettings().backend == "d3d12" &&
                   loaded.GetGraphicsSettings().renderPath == "deferred" &&
                   loaded.GetGraphicsSettings().deviceProfile == GraphicsDeviceProfile::Console,
               "project manifest fields mismatch"))
        return false;

    fs::path resolved;
    if (!Check(loaded.ResolveStartupScene(resolved, &error) && resolved == startupPath.lexically_normal(),
               "startup scene resolution failed"))
        return false;
    if (!Check(loaded.ResolveScenePath("Content/Scenes/Alternate.scene.json", resolved, true, &error) &&
                   resolved == alternatePath.lexically_normal(),
               "scene override did not take precedence"))
        return false;
    if (!Check(!loaded.ResolveScenePath(startupPath.string(), resolved, true, &error),
               "absolute startup scene path was accepted"))
        return false;
    if (!Check(!loaded.ResolveScenePath("Content/../outside.scene.json", resolved, false, &error),
               "traversal startup scene path was accepted"))
        return false;
    if (!Check(!loaded.ResolveScenePath("Content/Scenes/Missing.scene.json", resolved, true, &error),
               "missing startup scene was accepted"))
        return false;
    if (!Check(loaded.ResolveInputConfigPath(resolved, false, &error) &&
                   resolved == (root / "Content" / "Config" / "Input.input.json").lexically_normal(),
               "input config resolution failed"))
        return false;
    if (!Check(!loaded.SetInputConfigPath("../Outside.input.json", &error), "traversal input config path was accepted"))
        return false;
    loaded.GetGraphicsSettings().backend = "vulkan";
    if (!Check(loaded.Save(&error), "supported Vulkan graphics backend was rejected"))
        return false;
    loaded.GetGraphicsSettings().backend = "opengl";
    if (!Check(!loaded.Save(&error), "unsupported graphics backend was accepted"))
        return false;
    loaded.GetGraphicsSettings().backend = "d3d11";
    loaded.GetGraphicsSettings().renderPath = "raytraced";
    if (!Check(!loaded.Save(&error), "unsupported render path was accepted"))
        return false;
    loaded.GetGraphicsSettings().renderPath = "forward";
    if (!Check(loaded.Save(&error), "failed to restore graphics backend"))
        return false;
    if (!Check(ProjectConfig::IsSupportedDeviceProfile("desktop") &&
                   ProjectConfig::IsSupportedDeviceProfile("console") &&
                   ProjectConfig::IsSupportedDeviceProfile("mobile") &&
                   !ProjectConfig::IsSupportedDeviceProfile("handheld"),
               "graphics device profile validation mismatch"))
        return false;
    {
        nlohmann::json legacy;
        std::ifstream input(root / ProjectConfig::kFileName);
        input >> legacy;
        legacy["version"] = 1;
        legacy["graphics"].erase("deviceProfile");
        std::ofstream(root / ProjectConfig::kFileName) << legacy.dump(2);
        ProjectConfig migrated;
        if (!Check(migrated.Open(root, false, &error) && migrated.GetVersion() == ProjectConfig::kCurrentVersion &&
                       migrated.GetGraphicsSettings().deviceProfile == GraphicsDeviceProfile::Desktop,
                   "v1 project graphics profile migration failed: " + error))
            return false;
    }
    std::ofstream(root / ProjectConfig::kFileName)
        << R"({"version":999,"name":"Future","startupScene":"Content/Scenes/Main.scene.json"})";
    ProjectConfig unsupported;
    if (!Check(!unsupported.Open(root, false, &error), "unsupported project version was accepted"))
        return false;
    if (!Check(project.Save(&error), "failed to restore project manifest"))
        return false;

    AssetManager::Get().Clear();
    AssetManager::Get().SetProjectRoot(root);
    const auto meshPath = meshes / "Portable.mesh";
    std::ofstream(meshPath) << "mesh";
    auto mesh = std::make_shared<MeshAsset>(meshPath.string());
    mesh->SetGeometry({MeshVertex{}}, {0}, {});
    const MeshHandle meshHandle = AssetManager::Get().Register(std::move(mesh));
    Scene portableScene("Portable");
    Actor* actor = portableScene.CreateActor("PortableActor");
    actor->AddComponent<MeshRendererComponent>()->SetMesh(meshHandle);
    const std::string portableJson = SceneSerializer::SaveToString(portableScene);
    const auto parsed = nlohmann::json::parse(portableJson);
    const std::string storedMesh = parsed["actors"][0]["components"][0]["data"]["mesh"];
    if (!Check(storedMesh == "Content/Mesh/Portable.mesh", "new scene did not store a project-relative asset path"))
        return false;

    Scene portableLoaded;
    if (!Check(SceneSerializer::LoadFromString(portableLoaded, portableJson),
               "project-relative asset scene failed to load"))
        return false;
    auto* loadedRenderer = portableLoaded.FindByName("PortableActor")
                               ? portableLoaded.FindByName("PortableActor")->GetComponent<MeshRendererComponent>()
                               : nullptr;
    if (!Check(loadedRenderer && loadedRenderer->GetMesh().IsValid(), "project-relative mesh did not resolve"))
        return false;

    nlohmann::json legacy = parsed;
    legacy["actors"][0]["components"][0]["data"]["mesh"] = meshPath.string();
    Scene legacyLoaded;
    if (!Check(SceneSerializer::LoadFromString(legacyLoaded, legacy.dump()),
               "legacy absolute asset scene failed to load"))
        return false;
    auto* legacyRenderer = legacyLoaded.FindByName("PortableActor")
                               ? legacyLoaded.FindByName("PortableActor")->GetComponent<MeshRendererComponent>()
                               : nullptr;
    if (!Check(legacyRenderer && legacyRenderer->GetMesh().IsValid(), "legacy absolute mesh path compatibility failed"))
        return false;

    const auto scripts = root / "Content" / "Scripts";
    fs::create_directories(scripts);
    const auto scriptPath = scripts / "Portable.as";
    std::ofstream(scriptPath) << "class Script { void Update(float dt) {} }\n";
    ScriptComponent script;
    script.SetScriptPath(scriptPath.string());
    nlohmann::json scriptData;
    script.Serialize(scriptData);
    if (!Check(scriptData.value("scriptPath", std::string{}) == "Content/Scripts/Portable.as",
               "script path was not stored project-relative"))
        return false;
    ScriptComponent loadedScript;
    loadedScript.Deserialize(scriptData);
    if (!Check(fs::path(loadedScript.GetScriptPath()) == scriptPath.lexically_normal(),
               "project-relative script path did not resolve"))
        return false;

    AssetManager::Get().Clear();
    AssetManager::Get().SetProjectRoot({});
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    return true;
}

bool TestWorkspaceCookAndPublish() {
    namespace fs = std::filesystem;
    const auto base =
        fs::temp_directory_path() /
        ("myengine_publish_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto projectRoot = base / "GameProject";
    const auto workspacePath = base / "settings" / "workspace.json";
    EditorWorkspace workspace(workspacePath);
    std::string error;
    if (!Check(workspace.CreateProject(projectRoot, "CookTest", &error), "workspace project creation failed: " + error))
        return false;
    if (!Check(fs::is_regular_file(projectRoot / ProjectConfig::kFileName) &&
                   fs::is_regular_file(projectRoot / "Content/Scenes/Main.scene.json"),
               "workspace did not create project files"))
        return false;

    EditorWorkspace reloaded(workspacePath);
    if (!Check(reloaded.Load(&error) && reloaded.GetRecentProjects().size() == 1 &&
                   reloaded.GetRecentProjects()[0] == projectRoot.lexically_normal(),
               "workspace recent project persistence failed"))
        return false;

    const auto assetPath = projectRoot / "Content" / "Data" / "payload.bin";
    fs::create_directories(assetPath.parent_path());
    std::ofstream(assetPath, std::ios::binary) << "cooked payload";
    const auto scriptPath = projectRoot / "Content" / "Scripts" / "main.as";
    fs::create_directories(scriptPath.parent_path());
    std::ofstream(scriptPath) << "class Script { void Update(float dt) {} }\n";
    const auto editorScriptPath = projectRoot / "Content" / "Editor" / "Scripts" / "tool.lua";
    fs::create_directories(editorScriptPath.parent_path());
    std::ofstream(editorScriptPath) << "Editor.log('tool')\n";
    const auto performanceProfilePath = projectRoot / "Content" / "Config" / "Performance.profile.json";
    fs::create_directories(performanceProfilePath.parent_path());
    RuntimePerformanceProfile performanceProfile;
    std::ofstream(performanceProfilePath) << performanceProfile.ToJson().dump(2) << '\n';
    const auto modelPath = projectRoot / "Content" / "Models" / "cached.gltf";
    fs::create_directories(modelPath.parent_path());
    std::ofstream(modelPath) << R"({"asset":{"version":"2.0"},"scenes":[]})";
    const auto modelArtifact = projectRoot / "Library" / "windows-x64" / "cached-model" / "cached.modelbin";
    const auto staleModelArtifact = projectRoot / "Library" / "windows-x64" / "cached-model" / "stale.modelbin";
    fs::create_directories(modelArtifact.parent_path());
    std::ofstream(modelArtifact, std::ios::binary) << "model cache";
    AssetDatabase importDatabase;
    AssetRecord cachedModel;
    cachedModel.uuid = "cached-model";
    cachedModel.sourcePath = modelPath.generic_string();
    cachedModel.artifactPath = modelArtifact.generic_string();
    cachedModel.type = "model";
    cachedModel.importer = "gltf";
    cachedModel.state = AssetImportState::Ready;
    if (!Check(importDatabase.Open(projectRoot / ".myengine" / "AssetDatabase.json", &error) &&
                   importDatabase.Upsert(cachedModel, &error) && importDatabase.Save(&error),
               "cached model import database setup failed: " + error))
        return false;
    std::ofstream(projectRoot / "Content" / "Scenes" / "Main.scene.json")
        << "{\"preloadAssets\":[\"Content/Data/payload.bin\"],"
        << "\"actors\":[{\"components\":[{\"data\":{\"mesh\":\"" << staleModelArtifact.generic_string()
        << "#mesh\",\"scriptPath\":\"Content/Scripts/main.as\"}}]}]}";

    ProjectConfig project;
    if (!Check(project.Open(projectRoot, false, &error), "created project failed to reopen: " + error))
        return false;
    project.GetPublishSettings().outputDirectory = (base / "Published").string();
    project.GetPublishSettings().target = PublishTargets::kDefaultTargetId;
    if (!Check(project.Save(&error), "publish settings save failed: " + error))
        return false;
    project.GetPublishSettings().target = "unsupported-target";
    if (!Check(!project.Save(&error), "unsupported publish target was accepted"))
        return false;
    project.GetPublishSettings().target = PublishTargets::kDefaultTargetId;
    if (!Check(project.Save(&error), "failed to restore Windows publish target"))
        return false;

    const auto binaries = base / "Binaries";
    fs::create_directories(binaries);
#ifdef _WIN32
    const char* runtimeFiles[] = {"MyEnginePlayer.exe", "runtime.dll", "SDL3.dll"};
#elif defined(__APPLE__)
    const char* runtimeFiles[] = {"MyEnginePlayer", "libruntime.dylib", "libSDL3.dylib", "libSDL3.0.dylib"};
#else
    const char* runtimeFiles[] = {"MyEnginePlayer", "libruntime.so", "libSDL3.so"};
#endif
    const auto builtBinaries = gExecutableDirectory;
    const auto engineContent = std::filesystem::current_path() / "EngineContent";
    for (const char* file : runtimeFiles) {
        fs::copy_file(builtBinaries / file, binaries / file, fs::copy_options::overwrite_existing);
    }

    PublishReport report;
    const bool publishSucceeded = ProjectPublisher::Publish(project, binaries, engineContent, report, &error);
    if (!Check(publishSucceeded, "project publish failed: " + error))
        return false;
    if (!Check(fs::is_regular_file(report.contentArchive) &&
                   fs::is_regular_file(report.outputDirectory / "CookManifest.json") &&
                   !fs::exists(report.outputDirectory / "Content") && report.cookedFiles.size() >= 3 &&
                   report.contentBytes > 0,
               "publish output layout or cook report mismatch"))
        return false;
    for (const char* file : runtimeFiles) {
        if (!Check(fs::is_regular_file(report.outputDirectory / file),
                   std::string("published runtime file missing: ") + file))
            return false;
    }

    CookManifest manifest;
    if (!Check(CookManifest::Load(report.outputDirectory / CookManifest::kFileName, manifest, &error),
               "published Cook manifest failed to load: " + error))
        return false;
    if (!Check(manifest.project == project.GetName() && manifest.startupScene == project.GetStartupScene() &&
                   manifest.target == PublishTargets::kDefaultTargetId &&
                   manifest.files.size() == report.cookedFiles.size(),
               "published Cook manifest fields mismatch"))
        return false;

    auto archiveReader = std::make_shared<ContentArchiveReader>();
    if (!Check(archiveReader->Open(report.contentArchive, &error),
               "Content archive reader failed to open package: " + error))
        return false;
    if (!Check(archiveReader->ValidateAgainstManifest(manifest, &error),
               "Content archive reader rejected manifest: " + error))
        return false;
    if (!Check(archiveReader->EntryCount() == manifest.files.size() &&
                   archiveReader->ContentBytes() == report.contentBytes,
               "Content archive reader indexed unexpected entry count or content bytes"))
        return false;
    std::vector<uint8_t> payloadBytes;
    if (!Check(archiveReader->ReadFile("Content/Data/payload.bin", payloadBytes, &error),
               "Content archive reader failed to read payload: " + error))
        return false;
    const std::string pakPayload(payloadBytes.begin(), payloadBytes.end());
    if (!Check(pakPayload == "cooked payload", "Content archive reader returned wrong payload bytes"))
        return false;
    std::vector<uint8_t> profileBytes;
    if (!Check(archiveReader->ReadFile("Content/Config/Performance.profile.json", profileBytes, &error),
               "Content archive omitted the project performance profile: " + error))
        return false;
    RuntimePerformanceProfile cookedProfile;
    if (!Check(RuntimePerformanceProfile::FromText(std::string(profileBytes.begin(), profileBytes.end()), cookedProfile,
                                                   &error) &&
                   cookedProfile.name == performanceProfile.name,
               "cooked performance profile failed validation: " + error))
        return false;
    FileStat payloadStat;
    if (!Check(archiveReader->Stat("Content/Data/payload.bin", payloadStat, &error) &&
                   payloadStat.sourceKind == "pak" && payloadStat.normalizedPath == "Content/Data/payload.bin" &&
                   payloadStat.size == payloadBytes.size() && payloadStat.hash.size() == 64,
               "Content archive stat did not describe the pak payload: " + error))
        return false;
    const auto shaderEntries = archiveReader->ListFiles("Content/Engine/Shaders");
    if (!Check(std::find(shaderEntries.begin(), shaderEntries.end(), std::string(EngineShaders::kShadowDepth)) !=
                   shaderEntries.end(),
               "Content archive ListFiles did not expose engine shader logical paths"))
        return false;

    auto mountedFileSystem = std::make_shared<MountedFileSystem>();
    mountedFileSystem->SetProjectRoot(report.outputDirectory);
    mountedFileSystem->AddMount(std::make_shared<PakFileSystem>(archiveReader));
    mountedFileSystem->AddMount(std::make_shared<PackageRootFileSystem>(report.outputDirectory));
    std::ofstream(report.outputDirectory / "NotAllowed.txt") << "physical root noise";
    if (!Check(mountedFileSystem->Exists((report.outputDirectory / CookManifest::kFileName).string()) &&
                   !mountedFileSystem->Exists((report.outputDirectory / "NotAllowed.txt").string()),
               "package root mount exposed files outside the package root allowlist"))
        return false;
    std::string mountedSceneText;
    if (!Check(mountedFileSystem->ReadText((report.outputDirectory / "Content/Scenes/Main.scene.json").string(),
                                           mountedSceneText, &error) &&
                   mountedSceneText.find("Content/Library/windows-x64/cached-model/cached.modelbin#mesh") !=
                       std::string::npos,
               "mounted file system failed to read scene from Content.pak: " + error))
        return false;
    FileStat mountedSceneStat;
    if (!Check(mountedFileSystem->Stat((report.outputDirectory / "Content/Scenes/Main.scene.json").string(),
                                       mountedSceneStat, &error) &&
                   mountedSceneStat.sourceKind == "pak" &&
                   mountedSceneStat.normalizedPath == "Content/Scenes/Main.scene.json",
               "mounted file system did not select pak for Content scene: " + error))
        return false;
    {
        ScopedRuntimeFileSystem mountedScope(mountedFileSystem);
        auto mountedShader =
            LoadShaderAssetFromFile((report.outputDirectory / "Content/Engine/Shaders/Mesh.shader").string());
        if (!Check(mountedShader && mountedShader->IsCooked(),
                   "Runtime file system failed to load cooked shader from Content.pak"))
            return false;
        AssetManager::Get().Clear();
        AssetManager::Get().SetProjectRoot(report.outputDirectory);
        AssetManager::Get().SetEngineContentRoot(std::filesystem::current_path() / "EngineContent");
        const char* engineShaders[] = {
            EngineShaders::kAtmosphereCubemap, EngineShaders::kAtmosphereSH,    EngineShaders::kDeferredLighting,
            EngineShaders::kEnvironmentMipmap, EngineShaders::kGBuffer,         EngineShaders::kMesh,
            EngineShaders::kPostProcessFXAA,   EngineShaders::kPostProcessSSAO, EngineShaders::kPostProcessSSAOBlur,
            EngineShaders::kProceduralSky,     EngineShaders::kShadowDepth,
            EngineShaders::kShadowDepthSkinned, EngineShaders::kShadowedMainPass};
        for (const char* engineShader : engineShaders) {
            auto assetManagerShader = AssetManager::Get().Load<ShaderAsset>(engineShader);
            if (!Check(assetManagerShader && assetManagerShader->IsCooked(),
                       std::string("AssetManager resolved engine shader to physical raw descriptor instead of mounted "
                                   "Content.pak: ") +
                           engineShader)) {
                return false;
            }
        }
        AssetManager::Get().Clear();
        AssetManager::Get().SetProjectRoot(std::filesystem::current_path());
        AssetManager::Get().SetEngineContentRoot(std::filesystem::current_path() / "EngineContent");
    }

    CookManifest invalidManifest = manifest;
    invalidManifest.version = 999;
    if (!Check(!invalidManifest.Validate(&error), "unknown Cook manifest version was accepted"))
        return false;
    invalidManifest = manifest;
    invalidManifest.startupScene = "Content/../Outside.scene.json";
    if (!Check(!invalidManifest.Validate(&error), "Cook manifest traversal path was accepted"))
        return false;

    const auto cacheBase = base / "CookedCache";
    CookedProjectMount firstMount;
    if (!Check(CookedProjectCache::Prepare(report.outputDirectory, cacheBase, firstMount, &error) && firstMount.rebuilt,
               "first cooked cache prepare failed: " + error))
        return false;
    CookedProjectMount reusedMount;
    if (!Check(CookedProjectCache::Prepare(report.outputDirectory, cacheBase, reusedMount, &error) &&
                   !reusedMount.rebuilt && reusedMount.projectRoot == firstMount.projectRoot,
               "valid cooked cache was not reused: " + error))
        return false;

    const auto cachedPayload = firstMount.projectRoot / "Content/Data/payload.bin";
    std::ofstream(cachedPayload, std::ios::binary | std::ios::trunc) << "damaged payload";
    CookedProjectMount repairedMount;
    if (!Check(CookedProjectCache::Prepare(report.outputDirectory, cacheBase, repairedMount, &error) &&
                   repairedMount.rebuilt,
               "corrupt cooked cache was not rebuilt: " + error))
        return false;
    std::ifstream repairedPayload(cachedPayload, std::ios::binary);
    const std::string repairedText((std::istreambuf_iterator<char>(repairedPayload)), std::istreambuf_iterator<char>());
    if (!Check(repairedText == "cooked payload", "rebuilt cooked cache did not restore payload"))
        return false;
    repairedPayload.close();

    std::error_code concurrentCleanup;
    fs::remove_all(cacheBase, concurrentCleanup);
    if (!Check(!concurrentCleanup && !fs::exists(cacheBase), "failed to reset cooked cache before concurrency test"))
        return false;
    CookedProjectMount concurrentMounts[2];
    std::string concurrentErrors[2];
    bool concurrentResults[2] = {false, false};
    std::thread first([&] {
        concurrentResults[0] =
            CookedProjectCache::Prepare(report.outputDirectory, cacheBase, concurrentMounts[0], &concurrentErrors[0]);
    });
    std::thread second([&] {
        concurrentResults[1] =
            CookedProjectCache::Prepare(report.outputDirectory, cacheBase, concurrentMounts[1], &concurrentErrors[1]);
    });
    first.join();
    second.join();
    if (!Check(concurrentResults[0] && concurrentResults[1] &&
                   concurrentMounts[0].projectRoot == concurrentMounts[1].projectRoot,
               "concurrent cooked cache prepare failed: " + concurrentErrors[0] + " / " + concurrentErrors[1]))
        return false;

    const auto oldPackageMarker = report.outputDirectory / "previous-package.marker";
    std::ofstream(oldPackageMarker) << "keep";
    fs::remove(binaries / runtimeFiles[0]);
    PublishReport failedReport;
    if (!Check(!ProjectPublisher::Publish(project, binaries, engineContent, failedReport, &error) &&
                   fs::is_regular_file(oldPackageMarker) && !fs::exists(report.outputDirectory.string() + ".staging") &&
                   !fs::exists(report.outputDirectory.string() + ".backup"),
               "failed publish damaged the previous package or left temporary output"))
        return false;
    fs::copy_file(builtBinaries / runtimeFiles[0], binaries / runtimeFiles[0], fs::copy_options::overwrite_existing);
    PublishReport replacementReport;
    if (!Check(ProjectPublisher::Publish(project, binaries, engineContent, replacementReport, &error) &&
                   !fs::exists(oldPackageMarker) && !fs::exists(replacementReport.outputDirectory.string() + ".backup"),
               "transactional publish replacement failed: " + error))
        return false;

    const fs::path interruptedBackup = replacementReport.outputDirectory.string() + ".backup";
    fs::rename(replacementReport.outputDirectory, interruptedBackup);
    fs::remove(binaries / runtimeFiles[0]);
    if (!Check(!ProjectPublisher::Publish(project, binaries, engineContent, failedReport, &error) &&
                   fs::is_directory(replacementReport.outputDirectory) && !fs::exists(interruptedBackup),
               "interrupted publish backup was not restored before preflight"))
        return false;
    fs::copy_file(builtBinaries / runtimeFiles[0], binaries / runtimeFiles[0], fs::copy_options::overwrite_existing);

    const auto extracted = base / "Extracted";
    if (!Check(ContentArchive::Extract(report.contentArchive, extracted, &error),
               "Content archive extraction failed: " + error))
        return false;
    if (!Check(fs::is_regular_file(extracted / "Content/Scenes/Main.scene.json") &&
                   fs::is_regular_file(extracted / "Content/Data/payload.bin") &&
                   fs::is_regular_file(extracted / "Content/Scripts/main.as") &&
                   !fs::exists(extracted / "Content/Editor/Scripts/tool.lua") &&
                   fs::is_regular_file(extracted / "Content/Engine/Shaders/Mesh.shader") &&
                   !fs::exists(extracted / "Content/Engine/Shaders/Mesh.hlsl"),
               "cooked Content files were not restored"))
        return false;
    auto cookedShader = LoadShaderAssetFromFile((extracted / "Content/Engine/Shaders/Mesh.shader").string());
#if defined(__APPLE__)
    if (!Check(cookedShader && cookedShader->IsCooked() &&
                   cookedShader->GetBytecode(ShaderBackend::D3D11, ShaderStage::Vertex).empty() &&
                   cookedShader->GetBytecode(ShaderBackend::D3D12, ShaderStage::Pixel).empty() &&
                   !cookedShader->GetBytecode(ShaderBackend::Metal, ShaderStage::Vertex).empty() &&
                   cookedShader->GetBytecode(ShaderBackend::Vulkan, ShaderStage::Vertex).empty(),
               "published macOS shader does not contain the expected Metal backend"))
        return false;
#else
#if defined(MYENGINE_ENABLE_VULKAN)
    if (!Check(cookedShader && cookedShader->IsCooked() &&
                   !cookedShader->GetBytecode(ShaderBackend::D3D11, ShaderStage::Vertex).empty() &&
                   !cookedShader->GetBytecode(ShaderBackend::D3D12, ShaderStage::Pixel).empty() &&
                   !cookedShader->GetBytecode(ShaderBackend::Vulkan, ShaderStage::Vertex).empty() &&
                   cookedShader->GetBytecode(ShaderBackend::Metal, ShaderStage::Vertex).empty(),
               "published Windows shader does not contain the expected D3D/Vulkan backends"))
        return false;
#else
    if (!Check(cookedShader && cookedShader->IsCooked() &&
                   !cookedShader->GetBytecode(ShaderBackend::D3D11, ShaderStage::Vertex).empty() &&
                   !cookedShader->GetBytecode(ShaderBackend::D3D12, ShaderStage::Pixel).empty() &&
                   cookedShader->GetBytecode(ShaderBackend::Vulkan, ShaderStage::Vertex).empty() &&
                   cookedShader->GetBytecode(ShaderBackend::Metal, ShaderStage::Vertex).empty(),
               "published Windows shader does not contain the expected D3D-only backends"))
        return false;
#endif
#endif
    std::ifstream payload(extracted / "Content/Data/payload.bin", std::ios::binary);
    std::string payloadText((std::istreambuf_iterator<char>(payload)), std::istreambuf_iterator<char>());
    if (!Check(payloadText == "cooked payload", "cooked payload content changed"))
        return false;
    std::ifstream cookedScene(extracted / "Content/Scenes/Main.scene.json");
    std::string cookedSceneText((std::istreambuf_iterator<char>(cookedScene)), std::istreambuf_iterator<char>());
    if (!Check(cookedSceneText.find("Content/Library/windows-x64/cached-model/cached.modelbin#mesh") !=
                       std::string::npos &&
                   cookedSceneText.find("Content/Models/cached.gltf#mesh") == std::string::npos &&
                   cookedSceneText.find(projectRoot.generic_string()) == std::string::npos,
               "publish did not rewrite stale model cache artifact references"))
        return false;
    if (!Check(fs::is_regular_file(extracted / "Content/Library/windows-x64/cached-model/cached.modelbin") &&
                   !fs::exists(extracted / "Content/Models/cached.gltf"),
               "publish did not stage only the cooked model artifact"))
        return false;

    const auto corrupt = base / "Corrupt.pak";
    fs::copy_file(report.contentArchive, corrupt);
    {
        std::fstream file(corrupt, std::ios::binary | std::ios::in | std::ios::out);
        char value = 0;
        file.seekg(-1, std::ios::end);
        file.read(&value, 1);
        value = static_cast<char>(value ^ 0x7f);
        file.seekp(-1, std::ios::end);
        file.write(&value, 1);
    }
    if (!Check(!ContentArchive::Extract(corrupt, base / "CorruptExtract", &error),
               "corrupt Content archive was accepted"))
        return false;

    const auto trailing = base / "Trailing.pak";
    fs::copy_file(report.contentArchive, trailing);
    std::ofstream(trailing, std::ios::binary | std::ios::app) << "trailing";
    if (!Check(!ContentArchive::Extract(trailing, base / "TrailingExtract", &error) &&
                   error.find("trailing") != std::string::npos,
               "Content archive trailing data was accepted"))
        return false;
    ContentArchiveReader trailingReader;
    if (!Check(!trailingReader.Open(trailing, &error) && error.find("trailing") != std::string::npos,
               "Content archive reader accepted trailing data"))
        return false;

    const auto readerBad = base / "ReaderBad";
    const auto duplicatePak = readerBad / "Duplicate.pak";
    if (!Check(WriteTestContentArchive(duplicatePak, {{"Data/a.bin", "one"}, {"Data/a.bin", "two"}}, false, false),
               "failed to write duplicate-path test archive"))
        return false;
    ContentArchiveReader duplicateReader;
    if (!Check(!duplicateReader.Open(duplicatePak, &error) && error.find("duplicate") != std::string::npos,
               "Content archive reader accepted duplicate entry paths"))
        return false;

    const auto unsafePak = readerBad / "Unsafe.pak";
    if (!Check(WriteTestContentArchive(unsafePak, {{"../evil.bin", "evil"}}, false, false),
               "failed to write unsafe-path test archive"))
        return false;
    ContentArchiveReader unsafeReader;
    if (!Check(!unsafeReader.Open(unsafePak, &error) && error.find("unsafe") != std::string::npos,
               "Content archive reader accepted unsafe entry paths"))
        return false;

    const auto hashMismatchPak = readerBad / "HashMismatch.pak";
    if (!Check(WriteTestContentArchive(hashMismatchPak, {{"Data/hash.bin", "payload"}}, true, false),
               "failed to write hash-mismatch test archive"))
        return false;
    ContentArchiveReader hashMismatchReader;
    std::vector<uint8_t> hashMismatchBytes;
    if (!Check(hashMismatchReader.Open(hashMismatchPak, &error) &&
                   !hashMismatchReader.ReadFile("Content/Data/hash.bin", hashMismatchBytes, &error) &&
                   error.find("hash mismatch") != std::string::npos,
               "Content archive reader accepted an entry with mismatched hash"))
        return false;

    const auto corruptPackage = base / "CorruptPackage";
    fs::copy(replacementReport.outputDirectory, corruptPackage, fs::copy_options::recursive);
    fs::copy_file(corrupt, corruptPackage / ContentArchive::kFileName, fs::copy_options::overwrite_existing);
    CookedProjectMount rejectedMount;
    if (!Check(!CookedProjectCache::Prepare(corruptPackage, base / "RejectedCache", rejectedMount, &error),
               "package with archive hash mismatch was accepted"))
        return false;

    std::error_code cleanupError;
    fs::remove_all(base, cleanupError);
    return true;
}

bool TestPublishedCompatibilityFixtures() {
    namespace fs = std::filesystem;
    const fs::path fixtures = fs::path("tests") / "fixtures" / "compatibility";
    std::string error;

    const fs::path projectRoot = fs::temp_directory_path() / "myengine_compat_project_v1";
    std::error_code ec;
    fs::remove_all(projectRoot, ec);
    fs::create_directories(projectRoot, ec);
    fs::copy_file(fixtures / "project-v1.json", projectRoot / ProjectConfig::kFileName,
                  fs::copy_options::overwrite_existing, ec);
    ProjectConfig project;
    if (!Check(!ec && project.Open(projectRoot, false, &error) &&
                   project.GetVersion() == ProjectConfig::kCurrentVersion,
               "published project fixture failed: " + error))
        return false;

    std::ifstream sceneInput(fixtures / "scene-v0.json");
    const std::string sceneJson((std::istreambuf_iterator<char>(sceneInput)), {});
    Scene scene;
    if (!Check(sceneInput.good() || sceneInput.eof(), "scene fixture could not be read"))
        return false;
    if (!Check(SceneSerializer::LoadFromString(scene, sceneJson) && scene.GetName() == "LegacySceneV0",
               "legacy scene fixture failed"))
        return false;

    PrefabAsset prefab;
    if (!Check(PrefabAsset::Load(fixtures / "prefab-v1.json", prefab, &error) && prefab.rootLocalId == "root",
               "published prefab fixture failed: " + error))
        return false;

    InputActionMap input;
    if (!Check(input.LoadFromFile(fixtures / "input-v1.json", &error) && input.FindAction("Confirm"),
               "published input fixture failed: " + error))
        return false;

    std::ifstream saveInput(fixtures / "save-v1.json");
    nlohmann::json saveJson;
    saveInput >> saveJson;
    SaveGameData save;
    if (!Check(SaveGame::FromJson(saveJson, save, &error) && save.version == SaveGameData::CurrentVersion &&
                   save.settings.is_object(),
               "published save fixture failed: " + error))
        return false;

    fs::remove_all(projectRoot, ec);
    return true;
}

bool TestTransactionalWriterAndMigrationRegistry() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_transactional_writer";
    const fs::path destination = root / "state.json";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    std::string error;
    if (!Check(TransactionalFileWriter::WriteText(destination, "{\"version\":1,\"value\":\"old\"}\n", {}, &error),
               error))
        return false;
    const auto read = [&]() {
        std::ifstream in(destination);
        return std::string((std::istreambuf_iterator<char>(in)), {});
    };
    const std::string original = read();
    for (TransactionalWriteFault fault :
         {TransactionalWriteFault::AfterWrite, TransactionalWriteFault::AfterFlush,
          TransactionalWriteFault::AfterValidation, TransactionalWriteFault::BeforeReplace}) {
        TransactionalWriteOptions options;
        options.injectedFault = fault;
        options.validator = [](const fs::path& path, std::string*) {
            std::ifstream input(path);
            nlohmann::json value;
            input >> value;
            return value.is_object();
        };
        if (!Check(!TransactionalFileWriter::WriteText(destination, "{\"version\":1,\"value\":\"new\"}\n", options,
                                                       &error) &&
                       read() == original,
                   "transaction fault replaced the previous valid file"))
            return false;
    }
    TransactionalWriteOptions valid;
    valid.validator = [](const fs::path& path, std::string*) {
        std::ifstream input(path);
        nlohmann::json value;
        input >> value;
        return value.value("value", std::string{}) == "new";
    };
    if (!Check(TransactionalFileWriter::WriteText(destination, "{\"version\":1,\"value\":\"new\"}\n", valid, &error) &&
                   fs::is_regular_file(destination.string() + ".bak") && read().find("new") != std::string::npos,
               "transaction commit or backup failed: " + error))
        return false;

    const fs::path scenePath = root / "Format.scene.json";
    Scene scene("LastKnownGood");
    scene.CreateActor("OldActor");
    if (!Check(SceneSerializer::SaveToFile(scene, scenePath.string()), "initial scene format save failed"))
        return false;
    scene.SetName("Uncommitted");
    scene.CreateActor("NewActor");
    TransactionalFileWriter::SetNextFaultForTesting(TransactionalWriteFault::BeforeReplace);
    if (!Check(!SceneSerializer::SaveToFile(scene, scenePath.string()), "scene save ignored injected replace failure"))
        return false;
    Scene retainedScene;
    if (!Check(SceneSerializer::LoadFromFile(retainedScene, scenePath.string()) &&
                   retainedScene.GetName() == "LastKnownGood" && retainedScene.ActorCount() == 1,
               "scene format failure did not preserve the previous valid file"))
        return false;

    const fs::path prefabPath = root / "Format.prefab.json";
    PrefabAsset prefab;
    prefab.uuid = "format-prefab";
    prefab.rootLocalId = "root";
    PrefabNode prefabRoot;
    prefabRoot.localId = "root";
    prefabRoot.name = "OldRoot";
    prefab.nodes.push_back(prefabRoot);
    if (!Check(prefab.Save(prefabPath, &error), "initial prefab format save failed: " + error))
        return false;
    prefab.nodes.front().name = "UncommittedRoot";
    TransactionalFileWriter::SetNextFaultForTesting(TransactionalWriteFault::BeforeReplace);
    if (!Check(!prefab.Save(prefabPath, &error), "prefab save ignored injected replace failure"))
        return false;
    PrefabAsset retainedPrefab;
    if (!Check(PrefabAsset::Load(prefabPath, retainedPrefab, &error) && retainedPrefab.nodes.front().name == "OldRoot",
               "prefab format failure did not preserve the previous valid file: " + error))
        return false;

    JsonMigrationRegistry migrations("fixture", 2);
    if (!Check(migrations.Register(
                   0,
                   [](nlohmann::json& value, std::string*) {
                       value["legacy"] = true;
                       return true;
                   },
                   &error) &&
                   migrations.Register(
                       1,
                       [](nlohmann::json& value, std::string*) {
                           value["current"] = true;
                           return true;
                       },
                       &error),
               error))
        return false;
    nlohmann::json value = nlohmann::json::object();
    if (!Check(migrations.Migrate(value, &error) && value["version"] == 2 && value["legacy"] && value["current"],
               "consecutive JSON migration failed: " + error))
        return false;
    JsonMigrationRegistry incomplete("incomplete", 2);
    incomplete.Register(1, [](nlohmann::json&, std::string*) { return true; });
    nlohmann::json missing = nlohmann::json::object();
    fs::remove_all(root, ec);
    return Check(!incomplete.Migrate(missing, &error), "missing migration step was accepted");
}

bool TestOneClickProjectValidatorDiagnostics() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_project_validator";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Content/Scenes");
    fs::create_directories(root / "Content/Scripts");
    fs::create_directories(root / "Content/Data");
    std::ofstream(root / ProjectConfig::kFileName)
        << R"({"version":1,"name":"Validator","projectId":"validator-project","startupScene":"Content/Scenes/Main.scene.json","publish":{"outputDirectory":"Builds","target":"windows-x64"},"input":{"config":"Content/Config/Input.input.json"},"graphics":{"backend":"d3d11","renderPath":"forward"}})";
    std::ofstream(root / "Content/Scenes/Main.scene.json") << R"({"name":"Main","actors":[]})";
    std::ofstream(root / "Content/Scripts/Valid.as") << "class Valid { void Update(float dt) {} }\n";
    std::ofstream(root / "Content/Data/Large.bin", std::ios::binary) << std::string(128, 'x');

    ProjectConfig project;
    std::string error;
    if (!Check(project.Open(root, false, &error), "validator project failed to open: " + error))
        return false;
    ProjectValidationOptions options;
    options.oversizedAssetWarningBytes = 64;
    ProjectValidationReport report;
    const bool validationPassed = ProjectValidator::Validate(project, report, options);
    if (!Check(validationPassed && report.Passed() && report.ErrorCount() == 0 && report.WarningCount() == 1 &&
                   report.scannedFiles == 3,
               "valid project did not pass with only an oversized warning: " + report.Summary()))
        return false;
    if (!Check(report.issues.front().code == ProjectValidationCode::OversizedAsset &&
                   report.issues.front().path == "Content/Data/Large.bin",
               "oversized warning did not retain a stable asset location"))
        return false;

    std::ofstream(root / "Content/Scripts/Broken.as") << "class Broken {\n";
    std::ofstream(root / "Content/Scenes/Main.scene.json")
        << R"({"name":"Main","actors":[{"components":[{"data":{"mesh":"C:/outside/model.gltf"}}]}]})";
    ProjectValidator::Validate(project, report, options);
    bool scriptError = false;
    bool unsafeReference = false;
    for (const ProjectValidationIssue& issue : report.issues) {
        scriptError |= issue.code == ProjectValidationCode::ScriptCompile && issue.path == "Content/Scripts/Broken.as";
        unsafeReference |= issue.code == ProjectValidationCode::CookDependency &&
                           issue.path.find("outside/model.gltf") != std::string::npos;
    }
    if (!Check(!report.Passed() && scriptError && unsafeReference,
               "validator did not report script and absolute-path blockers"))
        return false;

    fs::remove(root / "Content/Scenes/Main.scene.json", ec);
    ProjectValidator::Validate(project, report, options);
    const bool startupError =
        std::any_of(report.issues.begin(), report.issues.end(), [](const ProjectValidationIssue& issue) {
            return issue.code == ProjectValidationCode::InvalidStartupScene;
        });
    fs::remove_all(root, ec);
    return Check(startupError, "missing startup scene was not a project validation error");
}

MYENGINE_REGISTER_TEST("Project", "TestOneClickProjectValidatorDiagnostics", TestOneClickProjectValidatorDiagnostics);
MYENGINE_REGISTER_TEST("Project", "TestPublishedCompatibilityFixtures", TestPublishedCompatibilityFixtures);
MYENGINE_REGISTER_TEST("Project", "TestTransactionalWriterAndMigrationRegistry",
                       TestTransactionalWriterAndMigrationRegistry);
MYENGINE_REGISTER_TEST("Project", "TestPublishHardeningPrimitives", TestPublishHardeningPrimitives);
MYENGINE_REGISTER_TEST("Project", "TestProjectConfigAndPortableAssetPaths", TestProjectConfigAndPortableAssetPaths);
MYENGINE_REGISTER_TEST("Project", "TestWorkspaceCookAndPublish", TestWorkspaceCookAndPublish);

} // namespace
