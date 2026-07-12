#include "Project/JsonMigrationRegistry.h"

namespace { void SetError(std::string* out,const std::string& value){if(out)*out=value;} }

JsonMigrationRegistry::JsonMigrationRegistry(std::string name,uint32_t current)
    :m_FormatName(std::move(name)),m_CurrentVersion(current){}
bool JsonMigrationRegistry::Register(uint32_t from,Migration migration,std::string* error){
    if(from>=m_CurrentVersion||!migration||m_Migrations.count(from)){SetError(error,"invalid or duplicate "+m_FormatName+" migration");return false;}m_Migrations.emplace(from,std::move(migration));return true;}
bool JsonMigrationRegistry::Migrate(nlohmann::json& value,std::string* error)const{
    if(!value.is_object()){SetError(error,m_FormatName+" root must be an object");return false;}
    uint32_t version=value.value("version",0u);if(version>m_CurrentVersion){SetError(error,"unsupported "+m_FormatName+" version "+std::to_string(version));return false;}
    nlohmann::json migrated=value;while(version<m_CurrentVersion){auto it=m_Migrations.find(version);if(it==m_Migrations.end()){SetError(error,"missing "+m_FormatName+" migration from version "+std::to_string(version));return false;}if(!it->second(migrated,error))return false;++version;migrated["version"]=version;}value=std::move(migrated);return true;}

