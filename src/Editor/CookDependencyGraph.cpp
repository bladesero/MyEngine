#include "Editor/CookDependencyGraph.h"

#include "Project/ContentPathPolicy.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;
namespace {
struct Scanner {
    fs::path projectRoot, contentRoot;
    PublishPreflightReport* report = nullptr;
    std::unordered_set<std::string> visited;

    void Error(PublishIssueCode code, const fs::path& path, const fs::path& from,
               const std::vector<std::string>& chain, std::string message) {
        report->errors.push_back({code, path.generic_string(), from.generic_string(), chain, std::move(message)});
    }
    bool Resolve(const std::string& value, const fs::path& from, fs::path& out,
                 const std::vector<std::string>& chain) {
        if (value.empty() || value.rfind("__builtin__/",0)==0 || value.rfind("data:",0)==0) return false;
        std::string withoutFragment = value.substr(0, value.find('#'));
        fs::path logical(withoutFragment), relative;
        if (logical.is_absolute() || logical.has_root_name()) {
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
        if(IsPrefab(from)){Error(PublishIssueCode::UnsupportedReference,from,from,chain,"nested prefabs are not supported");return;}
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
            "mesh","material","scriptPath","shader","uri","clip"};
        if (node.is_string() && direct.count(key)) {
            ScanReference(node.get<std::string>(), from, chain); return;
        }
        if (node.is_object()) {
            for (auto it=node.begin();it!=node.end();++it) {
                if(it.key()=="prefabInstance") ScanPrefabReference(it.value(),from,chain);
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
        const std::string key=path.generic_string(); if(!visited.insert(key).second)return;
        report->visitedAssets.push_back(fs::relative(path,projectRoot).generic_string());
        std::string ext=path.extension().string(); for(char& c:ext)c=char(std::tolower((unsigned char)c));
        try {
            if (ext==".json" || ext==".mat" || ext==".shader" || ext==".gltf") {
                std::ifstream input(path); nlohmann::json json; input>>json;
                if(IsPrefab(path)){
                    if(json.value("version",0u)!=1u||!json.contains("nodes")||!json["nodes"].is_array())throw std::runtime_error("invalid prefab asset header");
                    std::ifstream meta(path.string()+".meta");nlohmann::json metaJson;if(!meta)throw std::runtime_error("prefab metadata is missing");meta>>metaJson;
                    if(json.value("uuid",std::string{})!=metaJson.value("uuid",std::string{}))throw std::runtime_error("prefab UUID does not match metadata");
                }
                VisitJson(json,path,chain);
            } else if(ext==".obj" || ext==".mtl") ScanTextDependencies(path,std::move(chain));
        } catch(const std::exception& e) { Error(PublishIssueCode::InvalidAsset,path,{},chain,e.what()); }
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
    std::vector<ContentFileInfo> files; std::string error;
    if(!ContentPathPolicy::Enumerate(scanner.contentRoot,files,report.totalContentBytes,&error)) {
        report.errors.push_back({PublishIssueCode::ContentLimit,{}, {}, {}, error}); return false;
    }
    for(const auto& file:files) {
        const std::string name=file.relative.generic_string();
        if(name.rfind("Scenes/",0)==0 && name.size()>=11 && name.rfind(".scene.json")==name.size()-11)
            scanner.Scan(file.absolute,{"Content/"+name});
    }
    if(report.visitedAssets.empty())
        report.errors.push_back({PublishIssueCode::MissingDependency,"Content/Scenes",{}, {},"no scene files found"});
    return report.Passed();
}
