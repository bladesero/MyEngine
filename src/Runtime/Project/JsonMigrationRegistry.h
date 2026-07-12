#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>

class JsonMigrationRegistry {
public:
    using Migration = std::function<bool(nlohmann::json&, std::string*)>;
    JsonMigrationRegistry(std::string formatName, uint32_t currentVersion);
    bool Register(uint32_t fromVersion, Migration migration, std::string* error = nullptr);
    bool Migrate(nlohmann::json& value, std::string* error = nullptr) const;

private:
    std::string m_FormatName;
    uint32_t m_CurrentVersion = 0;
    std::map<uint32_t, Migration> m_Migrations;
};

