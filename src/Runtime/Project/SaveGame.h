#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
struct SaveGameData {static constexpr int CurrentVersion=2;int version=CurrentVersion;std::string checkpoint; nlohmann::json player=nlohmann::json::object();std::vector<std::string> collected;nlohmann::json settings=nlohmann::json::object();};
class SaveGame {
public:
    static bool Write(const std::string& slot,const SaveGameData& data,std::string* error=nullptr);
    static bool Read(const std::string& slot,SaveGameData& data,std::string* error=nullptr);
    static bool Exists(const std::string& slot);static bool Remove(const std::string& slot,std::string* error=nullptr);
    static nlohmann::json ToJson(const SaveGameData& data);static bool FromJson(nlohmann::json value,SaveGameData& data,std::string* error=nullptr);
private:static bool Resolve(const std::string& slot,std::string& path,std::string* error);
};
