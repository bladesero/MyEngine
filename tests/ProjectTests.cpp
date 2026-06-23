#include "TestHarness.h"

#include "Assets/AssetManager.h"
#include "Assets/ShaderAsset.h"
#include "Core/Sha256.h"
#include "Editor/CookDependencyGraph.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorWorkspace.h"
#include "Editor/ProjectPublisher.h"
#include "Project/ContentArchive.h"
#include "Project/ContentPathPolicy.h"
#include "Project/CookedProjectCache.h"
#include "Project/CookManifest.h"
#include "Project/ProjectConfig.h"
#include "Project/PublishTargets.h"
#include "Project/RuntimeCompatibility.h"
#include "Project/RuntimeDependencies.h"

#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scripting/ScriptComponent.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace {

bool TestPublishHardeningPrimitives() {
    Sha256 sha;
    sha.Update("abc", 3);
    if (!Check(Sha256::ToHex(sha.Final()) ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
               "SHA-256 known vector mismatch")) return false;

    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "myengine_publish_hardening_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Content/Scenes");
    std::ofstream(root / "outside.bin") << "outside";
    fs::path resolved;
    std::string error;
    if (!Check(!ContentPathPolicy::ResolveContained(root / "Content", "../outside.bin",
                                                    resolved, &error),
               "Content path traversal was accepted")) return false;

    std::ofstream(root / "Content/Scenes/Main.scene.json")
        << R"({"actors":[{"components":[{"data":{"language":"angelscript","scriptPath":"Content/Scripts/missing.as"}}]}]})";
    PublishPreflightReport preflight;
    if (!Check(!CookDependencyGraph::Validate(root, preflight) &&
               !preflight.errors.empty() &&
               preflight.errors.front().code == PublishIssueCode::MissingDependency,
               "publish preflight accepted a missing script dependency")) return false;

    fs::remove_all(root / "Content/Scenes", ec);
    fs::create_directories(root / "Content/Scenes");
    fs::create_directories(root / "Content/Audio");
    std::ofstream(root / "Content/Audio/beep.wav", std::ios::binary) << "audio";
    std::ofstream(root / "Content/Scenes/Main.scene.json")
        << R"({"actors":[{"components":[{"data":{"clip":"Content/Audio/beep.wav"}}]}]})";
    preflight = {};
    if (!Check(CookDependencyGraph::Validate(root, preflight),
               "publish preflight rejected an audio clip dependency")) return false;
    if (!Check(std::find(preflight.visitedAssets.begin(), preflight.visitedAssets.end(),
                         "Content/Audio/beep.wav") != preflight.visitedAssets.end(),
               "publish preflight did not visit audio clip dependency")) return false;

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
#else
    manifest.requiredBackends = {"d3d11", "d3d12", "metal"};
#endif
    manifest.runtimeDependenciesHash = std::string(64, '0');
    manifest.archiveHash = std::string(64, '1');
    manifest.startupScene = "Content/Scenes/Main.scene.json";
    manifest.files = {{manifest.startupScene, 0, std::string(64, '2')}};
    if (!Check(manifest.Validate(&error), "valid Manifest v2 was rejected: " + error)) return false;
    manifest.version = 1;
    if (!Check(!manifest.Validate(&error) && error.find("unsupported") != std::string::npos,
               "legacy Manifest v1 was not rejected explicitly")) return false;
    manifest.version = CookManifest::kCurrentVersion;
    manifest.requiredBackends = {"d3d11"};
    if (!Check(!manifest.Validate(&error), "manifest with missing D3D12 backend was accepted")) return false;

    fs::create_directories(root / "Runtime");
    const auto dependency = root / "Runtime/test.dll";
    std::ofstream(dependency, std::ios::binary) << "dependency";
    RuntimeDependencyManifest dependencies;
    dependencies.files.push_back({"test.dll", "x64", fs::file_size(dependency),
                                  Sha256::HashFile(dependency, &error)});
    if (!Check(dependencies.ValidateFiles(root / "Runtime", &error),
               "valid runtime dependency was rejected: " + error)) return false;
    std::ofstream(dependency, std::ios::binary | std::ios::app) << "tampered";
    if (!Check(!dependencies.ValidateFiles(root / "Runtime", &error),
               "tampered runtime dependency was accepted")) return false;
    fs::remove_all(root, ec);
    return true;
}

bool TestProjectConfigAndPortableAssetPaths() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("myengine_project_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
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
               "project test scene creation failed")) return false;

    ProjectConfig project;
    std::string error;
    if (!Check(project.Open(root, true, &error), "missing project should open in editor mode")) return false;
    project.SetName("ProjectTest");
    project.GetGraphicsSettings().backend = "d3d12";
    if (!Check(project.SetInputConfigPath("Content/Config/Input.input.json", &error),
               "project input config path save failed: " + error)) return false;
    if (!Check(project.SetStartupScene(startupPath, &error) && project.Save(&error),
               "project startup scene save failed: " + error)) return false;

    ProjectConfig loaded;
    if (!Check(loaded.Open(root, false, &error), "project manifest load failed: " + error)) return false;
    if (!Check(loaded.GetVersion() == ProjectConfig::kCurrentVersion &&
               loaded.GetName() == "ProjectTest" &&
               loaded.GetStartupScene() == "Content/Scenes/Main.scene.json" &&
               loaded.GetInputSettings().config == "Content/Config/Input.input.json" &&
               loaded.GetGraphicsSettings().backend == "d3d12",
               "project manifest fields mismatch")) return false;

    fs::path resolved;
    if (!Check(loaded.ResolveStartupScene(resolved, &error) &&
               resolved == startupPath.lexically_normal(),
               "startup scene resolution failed")) return false;
    if (!Check(loaded.ResolveScenePath("Content/Scenes/Alternate.scene.json", resolved, true, &error) &&
               resolved == alternatePath.lexically_normal(),
               "scene override did not take precedence")) return false;
    if (!Check(!loaded.ResolveScenePath(startupPath.string(), resolved, true, &error),
               "absolute startup scene path was accepted")) return false;
    if (!Check(!loaded.ResolveScenePath("Content/../outside.scene.json", resolved, false, &error),
               "traversal startup scene path was accepted")) return false;
    if (!Check(!loaded.ResolveScenePath("Content/Scenes/Missing.scene.json", resolved, true, &error),
               "missing startup scene was accepted")) return false;
    if (!Check(loaded.ResolveInputConfigPath(resolved, false, &error) &&
               resolved == (root / "Content" / "Config" / "Input.input.json").lexically_normal(),
               "input config resolution failed")) return false;
    if (!Check(!loaded.SetInputConfigPath("../Outside.input.json", &error),
               "traversal input config path was accepted")) return false;
    loaded.GetGraphicsSettings().backend = "vulkan";
    if (!Check(!loaded.Save(&error),
               "unsupported graphics backend was accepted")) return false;
    loaded.GetGraphicsSettings().backend = "d3d11";
    if (!Check(loaded.Save(&error), "failed to restore graphics backend")) return false;
    std::ofstream(root / ProjectConfig::kFileName)
        << R"({"version":999,"name":"Future","startupScene":"Content/Scenes/Main.scene.json"})";
    ProjectConfig unsupported;
    if (!Check(!unsupported.Open(root, false, &error),
               "unsupported project version was accepted")) return false;
    if (!Check(project.Save(&error), "failed to restore project manifest")) return false;

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
    if (!Check(storedMesh == "Content/Mesh/Portable.mesh",
               "new scene did not store a project-relative asset path")) return false;

    Scene portableLoaded;
    if (!Check(SceneSerializer::LoadFromString(portableLoaded, portableJson),
               "project-relative asset scene failed to load")) return false;
    auto* loadedRenderer = portableLoaded.FindByName("PortableActor")
        ? portableLoaded.FindByName("PortableActor")->GetComponent<MeshRendererComponent>() : nullptr;
    if (!Check(loadedRenderer && loadedRenderer->GetMesh().IsValid(),
               "project-relative mesh did not resolve")) return false;

    nlohmann::json legacy = parsed;
    legacy["actors"][0]["components"][0]["data"]["mesh"] = meshPath.string();
    Scene legacyLoaded;
    if (!Check(SceneSerializer::LoadFromString(legacyLoaded, legacy.dump()),
               "legacy absolute asset scene failed to load")) return false;
    auto* legacyRenderer = legacyLoaded.FindByName("PortableActor")
        ? legacyLoaded.FindByName("PortableActor")->GetComponent<MeshRendererComponent>() : nullptr;
    if (!Check(legacyRenderer && legacyRenderer->GetMesh().IsValid(),
               "legacy absolute mesh path compatibility failed")) return false;

    const auto scripts = root / "Content" / "Scripts";
    fs::create_directories(scripts);
    const auto scriptPath = scripts / "Portable.as";
    std::ofstream(scriptPath) << "class Script { void Update(float dt) {} }\n";
    ScriptComponent script;
    script.SetScriptPath(scriptPath.string());
    nlohmann::json scriptData;
    script.Serialize(scriptData);
    if (!Check(scriptData.value("scriptPath", std::string{}) ==
                   "Content/Scripts/Portable.as",
               "script path was not stored project-relative")) return false;
    ScriptComponent loadedScript;
    loadedScript.Deserialize(scriptData);
    if (!Check(fs::path(loadedScript.GetScriptPath()) == scriptPath.lexically_normal(),
               "project-relative script path did not resolve")) return false;

    AssetManager::Get().Clear();
    AssetManager::Get().SetProjectRoot({});
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    return true;
}

bool TestWorkspaceCookAndPublish() {
    namespace fs = std::filesystem;
    const auto base = fs::temp_directory_path() /
        ("myengine_publish_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto projectRoot = base / "GameProject";
    const auto workspacePath = base / "settings" / "workspace.json";
    EditorWorkspace workspace(workspacePath);
    std::string error;
    if (!Check(workspace.CreateProject(projectRoot, "CookTest", &error),
               "workspace project creation failed: " + error)) return false;
    if (!Check(fs::is_regular_file(projectRoot / ProjectConfig::kFileName) &&
               fs::is_regular_file(projectRoot / "Content/Scenes/Main.scene.json"),
               "workspace did not create project files")) return false;

    EditorWorkspace reloaded(workspacePath);
    if (!Check(reloaded.Load(&error) && reloaded.GetRecentProjects().size() == 1 &&
               reloaded.GetRecentProjects()[0] == projectRoot.lexically_normal(),
               "workspace recent project persistence failed")) return false;

    const auto assetPath = projectRoot / "Content" / "Data" / "payload.bin";
    fs::create_directories(assetPath.parent_path());
    std::ofstream(assetPath, std::ios::binary) << "cooked payload";
    const auto scriptPath = projectRoot / "Content" / "Scripts" / "main.as";
    fs::create_directories(scriptPath.parent_path());
    std::ofstream(scriptPath) << "class Script { void Update(float dt) {} }\n";
    const auto editorScriptPath = projectRoot / "Content" / "Editor" / "Scripts" / "tool.lua";
    fs::create_directories(editorScriptPath.parent_path());
    std::ofstream(editorScriptPath) << "Editor.log('tool')\n";

    ProjectConfig project;
    if (!Check(project.Open(projectRoot, false, &error),
               "created project failed to reopen: " + error)) return false;
    project.GetPublishSettings().outputDirectory = (base / "Published").string();
    project.GetPublishSettings().target = PublishTargets::kDefaultTargetId;
    if (!Check(project.Save(&error), "publish settings save failed: " + error)) return false;
    project.GetPublishSettings().target = "unsupported-target";
    if (!Check(!project.Save(&error), "unsupported publish target was accepted")) return false;
    project.GetPublishSettings().target = PublishTargets::kDefaultTargetId;
    if (!Check(project.Save(&error), "failed to restore Windows publish target")) return false;

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
        fs::copy_file(builtBinaries / file, binaries / file,
                      fs::copy_options::overwrite_existing);
    }

    PublishReport report;
    const bool publishSucceeded = ProjectPublisher::Publish(
        project, binaries, engineContent, report, &error);
    if (!Check(publishSucceeded, "project publish failed: " + error)) return false;
    if (!Check(fs::is_regular_file(report.contentArchive) &&
               fs::is_regular_file(report.outputDirectory / "CookManifest.json") &&
               !fs::exists(report.outputDirectory / "Content") &&
               report.cookedFiles.size() >= 3 && report.contentBytes > 0,
               "publish output layout or cook report mismatch")) return false;
    for (const char* file : runtimeFiles) {
        if (!Check(fs::is_regular_file(report.outputDirectory / file),
                   std::string("published runtime file missing: ") + file)) return false;
    }

    CookManifest manifest;
    if (!Check(CookManifest::Load(report.outputDirectory / CookManifest::kFileName,
                                  manifest, &error),
               "published Cook manifest failed to load: " + error)) return false;
    if (!Check(manifest.project == project.GetName() &&
               manifest.startupScene == project.GetStartupScene() &&
               manifest.target == PublishTargets::kDefaultTargetId &&
               manifest.files.size() == report.cookedFiles.size(),
               "published Cook manifest fields mismatch")) return false;

    CookManifest invalidManifest = manifest;
    invalidManifest.version = 999;
    if (!Check(!invalidManifest.Validate(&error),
               "unknown Cook manifest version was accepted")) return false;
    invalidManifest = manifest;
    invalidManifest.startupScene = "Content/../Outside.scene.json";
    if (!Check(!invalidManifest.Validate(&error),
               "Cook manifest traversal path was accepted")) return false;

    const auto cacheBase = base / "CookedCache";
    CookedProjectMount firstMount;
    if (!Check(CookedProjectCache::Prepare(report.outputDirectory, cacheBase,
                                           firstMount, &error) && firstMount.rebuilt,
               "first cooked cache prepare failed: " + error)) return false;
    CookedProjectMount reusedMount;
    if (!Check(CookedProjectCache::Prepare(report.outputDirectory, cacheBase,
                                           reusedMount, &error) && !reusedMount.rebuilt &&
               reusedMount.projectRoot == firstMount.projectRoot,
               "valid cooked cache was not reused: " + error)) return false;

    const auto cachedPayload = firstMount.projectRoot / "Content/Data/payload.bin";
    std::ofstream(cachedPayload, std::ios::binary | std::ios::trunc) << "damaged payload";
    CookedProjectMount repairedMount;
    if (!Check(CookedProjectCache::Prepare(report.outputDirectory, cacheBase,
                                           repairedMount, &error) && repairedMount.rebuilt,
               "corrupt cooked cache was not rebuilt: " + error)) return false;
    std::ifstream repairedPayload(cachedPayload, std::ios::binary);
    const std::string repairedText((std::istreambuf_iterator<char>(repairedPayload)),
                                   std::istreambuf_iterator<char>());
    if (!Check(repairedText == "cooked payload",
               "rebuilt cooked cache did not restore payload")) return false;
    repairedPayload.close();

    std::error_code concurrentCleanup;
    fs::remove_all(cacheBase, concurrentCleanup);
    if (!Check(!concurrentCleanup && !fs::exists(cacheBase),
               "failed to reset cooked cache before concurrency test")) return false;
    CookedProjectMount concurrentMounts[2];
    std::string concurrentErrors[2];
    bool concurrentResults[2] = {false, false};
    std::thread first([&] {
        concurrentResults[0] = CookedProjectCache::Prepare(
            report.outputDirectory, cacheBase, concurrentMounts[0], &concurrentErrors[0]);
    });
    std::thread second([&] {
        concurrentResults[1] = CookedProjectCache::Prepare(
            report.outputDirectory, cacheBase, concurrentMounts[1], &concurrentErrors[1]);
    });
    first.join();
    second.join();
    if (!Check(concurrentResults[0] && concurrentResults[1] &&
               concurrentMounts[0].projectRoot == concurrentMounts[1].projectRoot,
               "concurrent cooked cache prepare failed: " + concurrentErrors[0] +
               " / " + concurrentErrors[1])) return false;

    const auto oldPackageMarker = report.outputDirectory / "previous-package.marker";
    std::ofstream(oldPackageMarker) << "keep";
    fs::remove(binaries / runtimeFiles[0]);
    PublishReport failedReport;
    if (!Check(!ProjectPublisher::Publish(project, binaries, engineContent, failedReport, &error) &&
               fs::is_regular_file(oldPackageMarker) &&
               !fs::exists(report.outputDirectory.string() + ".staging") &&
               !fs::exists(report.outputDirectory.string() + ".backup"),
               "failed publish damaged the previous package or left temporary output")) return false;
    fs::copy_file(builtBinaries / runtimeFiles[0], binaries / runtimeFiles[0],
                  fs::copy_options::overwrite_existing);
    PublishReport replacementReport;
    if (!Check(ProjectPublisher::Publish(project, binaries, engineContent, replacementReport, &error) &&
               !fs::exists(oldPackageMarker) &&
               !fs::exists(replacementReport.outputDirectory.string() + ".backup"),
               "transactional publish replacement failed: " + error)) return false;

    const fs::path interruptedBackup = replacementReport.outputDirectory.string() + ".backup";
    fs::rename(replacementReport.outputDirectory, interruptedBackup);
    fs::remove(binaries / runtimeFiles[0]);
    if (!Check(!ProjectPublisher::Publish(project, binaries, engineContent, failedReport, &error) &&
               fs::is_directory(replacementReport.outputDirectory) &&
               !fs::exists(interruptedBackup),
               "interrupted publish backup was not restored before preflight")) return false;
    fs::copy_file(builtBinaries / runtimeFiles[0], binaries / runtimeFiles[0],
                  fs::copy_options::overwrite_existing);

    const auto extracted = base / "Extracted";
    if (!Check(ContentArchive::Extract(report.contentArchive, extracted, &error),
               "Content archive extraction failed: " + error)) return false;
    if (!Check(fs::is_regular_file(extracted / "Content/Scenes/Main.scene.json") &&
               fs::is_regular_file(extracted / "Content/Data/payload.bin") &&
               fs::is_regular_file(extracted / "Content/Scripts/main.as") &&
               !fs::exists(extracted / "Content/Editor/Scripts/tool.lua") &&
               fs::is_regular_file(extracted / "Content/Engine/Shaders/Mesh.shader") &&
               !fs::exists(extracted / "Content/Engine/Shaders/Mesh.hlsl"),
               "cooked Content files were not restored")) return false;
    auto cookedShader = LoadShaderAssetFromFile(
        (extracted / "Content/Engine/Shaders/Mesh.shader").string());
#if defined(__APPLE__)
    if (!Check(cookedShader && cookedShader->IsCooked() &&
               cookedShader->GetBytecode(ShaderBackend::D3D11, ShaderStage::Vertex).empty() &&
               cookedShader->GetBytecode(ShaderBackend::D3D12, ShaderStage::Pixel).empty() &&
               !cookedShader->GetBytecode(ShaderBackend::Metal, ShaderStage::Vertex).empty(),
               "published macOS shader does not contain the expected Metal backend")) return false;
#else
    if (!Check(cookedShader && cookedShader->IsCooked() &&
               !cookedShader->GetBytecode(ShaderBackend::D3D11, ShaderStage::Vertex).empty() &&
               !cookedShader->GetBytecode(ShaderBackend::D3D12, ShaderStage::Pixel).empty() &&
               !cookedShader->GetBytecode(ShaderBackend::Metal, ShaderStage::Vertex).empty(),
               "published shader does not contain all RHI backends")) return false;
#endif
    std::ifstream payload(extracted / "Content/Data/payload.bin", std::ios::binary);
    std::string payloadText((std::istreambuf_iterator<char>(payload)),
                            std::istreambuf_iterator<char>());
    if (!Check(payloadText == "cooked payload", "cooked payload content changed")) return false;

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
               "corrupt Content archive was accepted")) return false;

    const auto trailing = base / "Trailing.pak";
    fs::copy_file(report.contentArchive, trailing);
    std::ofstream(trailing, std::ios::binary | std::ios::app) << "trailing";
    if (!Check(!ContentArchive::Extract(trailing, base / "TrailingExtract", &error) &&
               error.find("trailing") != std::string::npos,
               "Content archive trailing data was accepted")) return false;

    const auto corruptPackage = base / "CorruptPackage";
    fs::copy(replacementReport.outputDirectory, corruptPackage,
             fs::copy_options::recursive);
    fs::copy_file(corrupt, corruptPackage / ContentArchive::kFileName,
                  fs::copy_options::overwrite_existing);
    CookedProjectMount rejectedMount;
    if (!Check(!CookedProjectCache::Prepare(corruptPackage, base / "RejectedCache",
                                            rejectedMount, &error),
               "package with archive hash mismatch was accepted")) return false;

    std::error_code cleanupError;
    fs::remove_all(base, cleanupError);
    return true;
}

MYENGINE_REGISTER_TEST("Project", "TestPublishHardeningPrimitives", TestPublishHardeningPrimitives);
MYENGINE_REGISTER_TEST("Project", "TestProjectConfigAndPortableAssetPaths", TestProjectConfigAndPortableAssetPaths);
MYENGINE_REGISTER_TEST("Project", "TestWorkspaceCookAndPublish", TestWorkspaceCookAndPublish);

} // namespace
