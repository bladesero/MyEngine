#include "Editor/CookDependencyGraph.h"

#include "Assets/AssetDatabase.h"
#include "Project/ContentPathPolicy.h"
#include "Project/RuntimePerformanceProfile.h"
#include "UI/Core/RuntimeUIScreenConfig.h"
#include "UI/Core/RuntimeUIScreenStack.h"
#include "Input/InputGlyphAtlas.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;
namespace {
PublishIssueCode ToPublishIssue(AssetDatabaseValidationIssueCode code) {
    switch (code) {
    case AssetDatabaseValidationIssueCode::MissingSource:
    case AssetDatabaseValidationIssueCode::MissingArtifact:
    case AssetDatabaseValidationIssueCode::UnknownDependency:
        return PublishIssueCode::MissingDependency;
    case AssetDatabaseValidationIssueCode::ArtifactHashMismatch:
    case AssetDatabaseValidationIssueCode::StateNotReady:
    case AssetDatabaseValidationIssueCode::DependencyCycle:
        return PublishIssueCode::Compatibility;
    default:
        return PublishIssueCode::InvalidAsset;
    }
}

std::string AbsoluteKey(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = path.is_absolute() || path.has_root_name()
        ? fs::absolute(path, ec)
        : fs::absolute(path, ec);
    if (ec) absolute = path;
    return absolute.lexically_normal().generic_string();
}

bool IsWithin(const fs::path& path, const fs::path& root) {
    std::error_code ec;
    const fs::path relative = fs::relative(path, root, ec);
    return !ec && !relative.empty() && !relative.is_absolute() &&
           *relative.begin() != "..";
}

std::string ToProjectContentReference(const fs::path& projectRoot,
                                      const std::string& sourcePath) {
    fs::path source(sourcePath);
    if (source.is_relative() && source.generic_string().rfind("Content/", 0) == 0) {
        return source.generic_string();
    }
    if (source.is_relative()) source = projectRoot / source;
    std::error_code ec;
    source = fs::absolute(source, ec).lexically_normal();
    if (ec) return {};
    const fs::path contentRoot = projectRoot / "Content";
    if (!IsWithin(source, contentRoot)) return {};
    return (fs::path("Content") / fs::relative(source, contentRoot, ec)).generic_string();
}

std::unordered_map<std::string, std::string> BuildArtifactReferenceMap(
    const fs::path& projectRoot) {
    std::unordered_map<std::string, std::string> result;
    AssetDatabase database;
    std::string error;
    if (!database.Open(projectRoot / ".myengine" / "AssetDatabase.json", &error)) return result;
    for (const AssetRecord& record : database.GetAll()) {
        if (record.artifactPath.empty() || record.sourcePath.empty()) continue;
        const std::string sourceReference =
            ToProjectContentReference(projectRoot, record.sourcePath);
        if (sourceReference.empty()) continue;
        result[AbsoluteKey(record.artifactPath)] = sourceReference;
    }
    return result;
}

std::unordered_map<std::string, std::string> BuildArtifactUuidReferenceMap(
    const fs::path& projectRoot) {
    std::unordered_map<std::string, std::string> result;
    AssetDatabase database;
    std::string error;
    if (!database.Open(projectRoot / ".myengine" / "AssetDatabase.json", &error)) return result;
    for (const AssetRecord& record : database.GetAll()) {
        if (record.uuid.empty() || record.sourcePath.empty()) continue;
        const std::string sourceReference =
            ToProjectContentReference(projectRoot, record.sourcePath);
        if (sourceReference.empty()) continue;
        result[record.uuid] = sourceReference;
    }
    return result;
}

std::optional<std::string> ExtractProjectLibraryArtifactUuid(
    const fs::path& projectRoot,
    const fs::path& path) {
    std::error_code ec;
    fs::path absolute = path.is_absolute() || path.has_root_name()
        ? fs::absolute(path, ec)
        : fs::absolute(path, ec);
    if (ec) absolute = path;
    absolute = absolute.lexically_normal();
    fs::path libraryRoot =
        fs::absolute(projectRoot / "Library" / "windows-x64", ec);
    if (ec) libraryRoot = projectRoot / "Library" / "windows-x64";
    libraryRoot = libraryRoot.lexically_normal();
    const fs::path relative = fs::relative(absolute, libraryRoot, ec);
    if (ec || relative.empty() || relative.is_absolute()) return std::nullopt;
    auto part = relative.begin();
    if (part == relative.end() || *part == "..") return std::nullopt;
    const std::string uuid = part->generic_string();
    ++part;
    if (part == relative.end()) return std::nullopt;
    return uuid;
}

void ValidateAssetDatabase(const fs::path& projectRoot, PublishPreflightReport& report) {
    const fs::path databasePath = projectRoot / ".myengine" / "AssetDatabase.json";
    const bool hasImportState =
        fs::exists(databasePath) || fs::exists(projectRoot / "SourceAssets") ||
        fs::exists(projectRoot / "Library");
    if (!hasImportState) return;
    if (!fs::exists(databasePath)) {
        report.errors.push_back({PublishIssueCode::MissingDependency,
            databasePath.generic_string(), {}, {}, "asset database is missing"});
        return;
    }
    AssetDatabase database;
    std::string error;
    if (!database.Open(databasePath, &error)) {
        report.errors.push_back({PublishIssueCode::InvalidAsset,
            databasePath.generic_string(), {}, {}, error});
        return;
    }
    AssetDatabaseValidationReport validation;
    database.ValidateAgainstProject(projectRoot, validation);
    for (const auto& issue : validation.issues) {
        report.errors.push_back({ToPublishIssue(issue.code), issue.path, {}, {issue.uuid},
                                 issue.message});
    }
    for (const AssetRecord& record : database.GetAll()) {
        if (record.alwaysCook && !record.artifactPath.empty()) {
            report.visitedAssets.push_back(record.artifactPath);
        }
    }
}

struct Scanner {
    fs::path projectRoot, contentRoot;
    PublishPreflightReport* report = nullptr;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> active;
    std::unordered_map<std::string, std::string> artifactReferences;
    std::unordered_map<std::string, std::string> artifactUuidReferences;

    void Error(PublishIssueCode code, const fs::path& path, const fs::path& from,
               const std::vector<std::string>& chain, std::string message) {
        report->errors.push_back({code, path.generic_string(), from.generic_string(), chain, std::move(message)});
    }
    bool Resolve(const std::string& value, const fs::path& from, fs::path& out,
                 const std::vector<std::string>& chain) {
        if (value.empty() || value.rfind("__builtin__/",0)==0 || value.rfind("data:",0)==0) return false;
        const size_t fragmentPosition = value.find('#');
        std::string withoutFragment = value.substr(0, fragmentPosition);
        const std::string fragment = fragmentPosition == std::string::npos
            ? std::string{} : value.substr(fragmentPosition);
        fs::path logical(withoutFragment), relative;
        if (logical.is_absolute() || logical.has_root_name()) {
            const auto artifact = artifactReferences.find(AbsoluteKey(logical));
            if (artifact != artifactReferences.end()) {
                return Resolve(artifact->second + fragment, from, out, chain);
            }
            if (const std::optional<std::string> uuid =
                    ExtractProjectLibraryArtifactUuid(projectRoot, logical)) {
                const auto byUuid = artifactUuidReferences.find(*uuid);
                if (byUuid != artifactUuidReferences.end()) {
                    return Resolve(byUuid->second + fragment, from, out, chain);
                }
            }
            Error(PublishIssueCode::UnsafePath, logical, from, chain, "absolute asset reference is forbidden"); return false;
        }
        const std::string generic = logical.generic_string();
        if (generic.rfind("Content/Engine/",0)==0) return false;
        relative = generic.rfind("Content/",0)==0
            ? fs::path(generic.substr(8))
            : fs::relative(from.parent_path() / logical, contentRoot);
        std::string error;
        if (!ContentPathPolicy::ResolveContained(contentRoot, relative, out, &error, true)) {
            Error(error.find("missing") != std::string::npos ? PublishIssueCode::MissingDependency : PublishIssueCode::UnsafePath,
                  logical, from, chain, error); return false;
        }
        return true;
    }
    void ScanReference(const std::string& value, const fs::path& from,
                       std::vector<std::string> chain) {
        fs::path resolved;
        chain.push_back(value);
        if (Resolve(value, from, resolved, chain)) Scan(resolved, std::move(chain));
    }
    static bool IsPrefab(const fs::path& path) {
        const std::string name=path.filename().generic_string();
        return name.size()>=12&&name.rfind(".prefab.json")==name.size()-12;
    }
    void ScanPrefabReference(const nlohmann::json& reference,const fs::path& from,
                             std::vector<std::string> chain) {
        if(!reference.is_object()){Error(PublishIssueCode::InvalidAsset,from,from,chain,"prefab reference is not an object");return;}
        const std::string value=reference.value("asset",std::string{}), expected=reference.value("uuid",std::string{});fs::path resolved;chain.push_back(value);
        if(!Resolve(value,from,resolved,chain))return;
        try {std::ifstream assetInput(resolved),metaInput(resolved.string()+".meta");nlohmann::json assetJson,metaJson;assetInput>>assetJson;
            if(!metaInput){Error(PublishIssueCode::MissingDependency,resolved.string()+".meta",from,chain,"prefab metadata is missing");return;}metaInput>>metaJson;
            const std::string assetUuid=assetJson.value("uuid",std::string{}),metaUuid=metaJson.value("uuid",std::string{});
            if(expected.empty()||expected!=assetUuid||assetUuid!=metaUuid){Error(PublishIssueCode::Compatibility,resolved,from,chain,"prefab UUID mismatch");return;}
        }catch(const std::exception& e){Error(PublishIssueCode::InvalidAsset,resolved,from,chain,e.what());return;}
        Scan(resolved,std::move(chain));
    }
    void VisitJson(const nlohmann::json& node, const fs::path& from,
                   const std::vector<std::string>& chain, const std::string& key = {}) {
        static const std::unordered_set<std::string> direct = {
            "mesh","material","scriptPath","shader","uri","clip","asset",
            "navMeshAsset","documentPath","document","atlas","stylePaths","generatedStylePaths",
            "defaultFontPaths","preloadAssets"};
        if (node.is_string() && direct.count(key)) {
            ScanReference(node.get<std::string>(), from, chain); return;
        }
        if (node.is_object()) {
            for (auto it=node.begin();it!=node.end();++it) {
                if(it.key()=="prefabInstance") ScanPrefabReference(it.value(),from,chain);
                else if(it.key()=="nestedInstances" && it.value().is_array()) {
                    for (const auto& nested : it.value()) ScanPrefabReference(nested,from,chain);
                }
                else if (it.key()=="textures" && it.value().is_object()) {
                    for (auto texture=it.value().begin();texture!=it.value().end();++texture)
                        if (texture.value().is_string()) ScanReference(texture.value().get<std::string>(),from,chain);
                } else VisitJson(it.value(),from,chain,it.key());
            }
        } else if (node.is_array()) for (const auto& item:node) VisitJson(item,from,chain,key);
    }
    void ScanTextDependencies(const fs::path& path, std::vector<std::string> chain) {
        std::ifstream input(path); std::string line;
        while(std::getline(input,line)) {
            std::istringstream words(line); std::string command; words>>command;
            bool dependency = command=="mtllib" || command=="map_Kd" || command=="map_Ks" ||
                              command=="map_Bump" || command=="bump" || command=="norm";
            if (!dependency) continue;
            std::string value, token; while(words>>token) value=token;
            if (!value.empty()) ScanReference(value,path,chain);
        }
    }
    void Scan(const fs::path& path, std::vector<std::string> chain) {
        const std::string key=path.generic_string();
        if(active.count(key)){Error(PublishIssueCode::InvalidAsset,path,path,chain,"prefab dependency cycle");return;}
        if(!visited.insert(key).second)return;
        active.insert(key);
        report->visitedAssets.push_back(fs::relative(path,projectRoot).generic_string());
        std::string ext=path.extension().string(); for(char& c:ext)c=char(std::tolower((unsigned char)c));
        try {
            if (ext==".json" || ext==".mat" || ext==".shader" || ext==".gltf") {
                std::ifstream input(path); nlohmann::json json; input>>json;
                const std::string filename = path.filename().string();
                if (filename.size() >= 13 &&
                    filename.rfind(".profile.json") == filename.size() - 13) {
                    RuntimePerformanceProfile profile;
                    std::string profileError;
                    if (!RuntimePerformanceProfile::FromJson(json, profile, &profileError))
                        throw std::runtime_error(profileError);
                }
                if(filename=="RuntimeScreens.ui.json"){
                    RuntimeUIScreenConfig config;std::string configError;
                    if(!RuntimeUIScreenConfig::FromJson(json,config,&configError))
                        throw std::runtime_error(configError);
                    RuntimeUIScreenStack stack=RuntimeUIScreenStack::CreateStandard();
                    if(!config.Apply(stack,&configError))throw std::runtime_error(configError);
                }
                if(filename=="InputGlyphs.glyph.json"){
                    InputGlyphAtlas atlas;std::string atlasError;
                    if(!InputGlyphAtlas::FromJson(json,atlas,&atlasError))
                        throw std::runtime_error(atlasError);
                }
                if(IsPrefab(path)){
                    if(json.value("version",0u)!=1u||!json.contains("nodes")||!json["nodes"].is_array())throw std::runtime_error("invalid prefab asset header");
                    std::ifstream meta(path.string()+".meta");nlohmann::json metaJson;if(!meta)throw std::runtime_error("prefab metadata is missing");meta>>metaJson;
                    if(json.value("uuid",std::string{})!=metaJson.value("uuid",std::string{}))throw std::runtime_error("prefab UUID does not match metadata");
                }
                VisitJson(json,path,chain);
            } else if(ext==".obj" || ext==".mtl") ScanTextDependencies(path,std::move(chain));
        } catch(const std::exception& e) { Error(PublishIssueCode::InvalidAsset,path,{},chain,e.what()); }
        active.erase(key);
    }
};
}
std::string PublishPreflightReport::Summary() const {
    if(errors.empty()) return "preflight passed";
    const auto& issue=errors.front();
    return "preflight failed ("+std::to_string(errors.size())+" errors): "+issue.message+
           (issue.path.empty()?std::string{}:" ["+issue.path+"]");
}
bool CookDependencyGraph::Validate(const fs::path& projectRoot, PublishPreflightReport& report) {
    report={}; Scanner scanner{projectRoot,projectRoot/"Content",&report};
    scanner.artifactReferences = BuildArtifactReferenceMap(projectRoot);
    scanner.artifactUuidReferences = BuildArtifactUuidReferenceMap(projectRoot);
    ValidateAssetDatabase(projectRoot, report);
    std::vector<ContentFileInfo> files; std::string error;
    if(!ContentPathPolicy::Enumerate(scanner.contentRoot,files,report.totalContentBytes,&error)) {
        report.errors.push_back({PublishIssueCode::ContentLimit,{}, {}, {}, error}); return false;
    }
    for(const auto& file:files) {
        const std::string name=file.relative.generic_string();
        const bool scene = name.rfind("Scenes/",0)==0 && name.size()>=11 &&
            name.rfind(".scene.json")==name.size()-11;
        const bool config = name.rfind("Config/",0)==0 && name.size()>=5 &&
            name.rfind(".json")==name.size()-5;
        if(scene || config)
            scanner.Scan(file.absolute,{"Content/"+name});
    }
    if(report.visitedAssets.empty())
        report.errors.push_back({PublishIssueCode::MissingDependency,"Content/Scenes",{}, {},"no scene files found"});
    return report.Passed();
}
