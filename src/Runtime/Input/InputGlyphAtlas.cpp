#include "Input/InputGlyphAtlas.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace {
void SetError(std::string* error,const std::string& value){if(error)*error=value;}
std::string Lower(std::string value){std::transform(value.begin(),value.end(),value.begin(),
    [](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
}

nlohmann::json InputGlyphDescriptor::ToJson() const
{
    return{{"valid",valid},{"family",family},{"source",source},{"atlas",atlasPath},
           {"sprite",sprite},{"label",label}};
}

bool InputGlyphAtlas::FromJson(const nlohmann::json& value,InputGlyphAtlas& out,
                               std::string* error)
{
    if(!value.is_object()||value.value("version",0u)!=CurrentVersion){
        SetError(error,"unsupported input glyph atlas root or version");return false;}
    InputGlyphAtlas parsed;parsed.m_AtlasPath=value.value("atlas",std::string{});
    parsed.m_DefaultLocale=value.value("defaultLocale",std::string("en"));
    if(parsed.m_AtlasPath.rfind("Content/UI/Glyphs/",0)!=0||
       parsed.m_AtlasPath.find("..")!=std::string::npos||parsed.m_DefaultLocale.empty()){
        SetError(error,"glyph atlas path or default locale is invalid");return false;}
    const auto families=value.value("families",nlohmann::json::object());
    if(!families.is_object()||families.empty()||families.size()>16){
        SetError(error,"glyph families must be a bounded object");return false;}
    for(auto familyIt=families.begin();familyIt!=families.end();++familyIt){
        if(!familyIt.value().is_object()||familyIt.key().empty()){
            SetError(error,"glyph family must be a named object");return false;}
        auto& family=parsed.m_Families[Lower(familyIt.key())];
        for(auto glyphIt=familyIt.value().begin();glyphIt!=familyIt.value().end();++glyphIt){
            if(!glyphIt.value().is_object()){SetError(error,"glyph entry must be an object");return false;}
            Glyph glyph;glyph.sprite=glyphIt.value().value("sprite",std::string{});
            const auto labels=glyphIt.value().value("labels",nlohmann::json::object());
            if(glyph.sprite.empty()||!labels.is_object()||labels.empty()){
                SetError(error,"glyph sprite and labels must not be empty");return false;}
            for(auto label=labels.begin();label!=labels.end();++label){
                if(!label.value().is_string()||label.value().get<std::string>().empty()){
                    SetError(error,"localized glyph label must be a non-empty string");return false;}
                glyph.labels.emplace(label.key(),label.value().get<std::string>());
            }
            if(!glyph.labels.count(parsed.m_DefaultLocale)){
                SetError(error,"glyph is missing its default-locale label");return false;}
            family.emplace(Lower(glyphIt.key()),std::move(glyph));
        }
    }
    out=std::move(parsed);if(error)error->clear();return true;
}

bool InputGlyphAtlas::FromText(const std::string& text,InputGlyphAtlas& out,
                               std::string* error)
{
    try{return FromJson(nlohmann::json::parse(text),out,error);}
    catch(const std::exception& exception){SetError(error,exception.what());return false;}
}

InputGlyphDescriptor InputGlyphAtlas::Resolve(const std::string& familyName,
    const std::string& source,const std::string& locale) const
{
    InputGlyphDescriptor result;result.family=familyName;result.source=source;
    result.atlasPath=m_AtlasPath;
    auto family=m_Families.find(Lower(familyName));
    if(family==m_Families.end())family=m_Families.find("xbox");
    if(family==m_Families.end())return result;
    const auto glyph=family->second.find(Lower(source));if(glyph==family->second.end())return result;
    result.sprite=glyph->second.sprite;
    auto label=glyph->second.labels.find(locale);
    if(label==glyph->second.labels.end()){
        const std::string language=locale.substr(0,locale.find_first_of("-_"));
        label=std::find_if(glyph->second.labels.begin(),glyph->second.labels.end(),
            [&](const auto& entry){return entry.first==language||
                entry.first.rfind(language+"-",0)==0||entry.first.rfind(language+"_",0)==0;});
    }
    if(label==glyph->second.labels.end())label=glyph->second.labels.find(m_DefaultLocale);
    if(label==glyph->second.labels.end())return result;
    result.label=label->second;result.valid=true;return result;
}
