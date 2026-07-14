#pragma once

#include "Project/FormatVersions.h"

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>

struct InputGlyphDescriptor {
    std::string family;
    std::string source;
    std::string atlasPath;
    std::string sprite;
    std::string label;
    bool valid=false;
    nlohmann::json ToJson() const;
};

class InputGlyphAtlas {
public:
    static constexpr uint32_t CurrentVersion=FormatVersions::InputGlyphAtlas;
    static constexpr const char* DefaultPath="Content/Config/InputGlyphs.glyph.json";
    static bool FromJson(const nlohmann::json&,InputGlyphAtlas&,std::string* error=nullptr);
    static bool FromText(const std::string&,InputGlyphAtlas&,std::string* error=nullptr);
    InputGlyphDescriptor Resolve(const std::string& family,const std::string& source,
                                 const std::string& locale) const;
    const std::string& GetDefaultLocale() const{return m_DefaultLocale;}
    const std::string& GetAtlasPath() const{return m_AtlasPath;}
private:
    struct Glyph {std::string sprite;std::unordered_map<std::string,std::string> labels;};
    std::string m_AtlasPath,m_DefaultLocale="en";
    std::unordered_map<std::string,std::unordered_map<std::string,Glyph>> m_Families;
};
