#include "UI/Core/RuntimeUIScreenConfig.h"
#include "UI/Core/RuntimeUIScreenStack.h"

#include <nlohmann/json.hpp>
#include <unordered_set>

namespace { void SetError(std::string* error,const std::string& value){if(error)*error=value;} }

bool RuntimeUIScreenConfig::FromJson(const nlohmann::json& value,
    RuntimeUIScreenConfig& out,std::string* error)
{
    if(!value.is_object()){SetError(error,"runtime screen config root must be an object");return false;}
    const uint32_t version=value.value("version",0u);
    if(version!=CurrentVersion){SetError(error,"unsupported runtime screen config version");return false;}
    const auto screens=value.value("screens",nlohmann::json::array());
    if(!screens.is_array()||screens.size()>16){SetError(error,"runtime screens must be a bounded array");return false;}
    RuntimeUIScreenConfig parsed;std::unordered_set<std::string> names;
    for(const auto& item:screens){
        if(!item.is_object()){SetError(error,"runtime screen override must be an object");return false;}
        RuntimeUIScreenOverride screen;
        screen.stableName=item.value("name",std::string{});
        screen.title=item.value("title",std::string{});
        screen.documentPath=item.value("document",std::string{});
        if(screen.stableName.empty()||screen.title.empty()||
           screen.documentPath.rfind("Content/UI/",0)!=0||
           screen.documentPath.size()<5||
           screen.documentPath.rfind(".rml")!=screen.documentPath.size()-4||
           screen.documentPath.find("..")!=std::string::npos||
           !names.insert(screen.stableName).second){
            SetError(error,"invalid runtime screen name, title, or Content/UI Rml path");return false;
        }
        const auto actions=item.value("actions",nlohmann::json::object());
        if(!actions.is_object()||actions.size()>64){SetError(error,"runtime screen actions must be an object");return false;}
        for(auto it=actions.begin();it!=actions.end();++it){
            if(it.key().empty()||!it.value().is_string()||it.value().get<std::string>().empty()){
                SetError(error,"runtime screen action labels must be non-empty strings");return false;
            }
            screen.actionLabels.emplace(it.key(),it.value().get<std::string>());
        }
        parsed.screens.push_back(std::move(screen));
    }
    out=std::move(parsed);if(error)error->clear();return true;
}

bool RuntimeUIScreenConfig::FromText(const std::string& text,
    RuntimeUIScreenConfig& out,std::string* error)
{
    try{return FromJson(nlohmann::json::parse(text),out,error);}
    catch(const std::exception& exception){SetError(error,exception.what());return false;}
}

bool RuntimeUIScreenConfig::Apply(RuntimeUIScreenStack& stack,std::string* error) const
{
    RuntimeUIScreenStack candidate=stack;
    for(const auto& screen:screens)
        if(!candidate.ApplyProjectOverride(screen.stableName,screen.title,
            screen.documentPath,screen.actionLabels,error))return false;
    stack=std::move(candidate);if(error)error->clear();return true;
}
