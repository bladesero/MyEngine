#include "Project/RuntimeUserSettings.h"

#include "Assets/AssetManager.h"
#include "Core/TransactionalFileWriter.h"
#include "Input/InputActionMap.h"
#include "Project/JsonMigrationRegistry.h"
#include "Project/ProjectConfig.h"

#include <array>
#include <fstream>
#include <mutex>

namespace fs=std::filesystem;
namespace {
std::mutex g_SettingsMutex;
fs::path g_SettingsRoot;
void SetError(std::string* error,const std::string& value){if(error)*error=value;}
bool InRange(float value,float minimum,float maximum){return value>=minimum&&value<=maximum;}
bool ReadSettings(const fs::path& path,RuntimeUserSettings& settings,std::string* error)
{
    try{std::ifstream input(path,std::ios::binary);if(!input){SetError(error,"user settings do not exist");return false;}
        nlohmann::json value;input>>value;return RuntimeUserSettingsStore::FromJson(std::move(value),settings,error);
    }catch(const std::exception& exception){SetError(error,exception.what());return false;}
}
}

RuntimeUserSettings RuntimeUserSettingsStore::Defaults(){return {};}

bool RuntimeUserSettingsStore::Validate(const RuntimeUserSettings& value,std::string* error)
{
    if(value.display.width<640||value.display.width>7680||value.display.height<360||value.display.height>4320){SetError(error,"display resolution is outside 640x360..7680x4320");return false;}
    if(value.display.windowMode!="windowed"&&value.display.windowMode!="borderless"&&value.display.windowMode!="fullscreen"){SetError(error,"invalid window mode");return false;}
    if(value.display.frameRateLimit<30||value.display.frameRateLimit>1000){SetError(error,"frame rate limit must be between 30 and 1000");return false;}
    if(!ProjectConfig::IsSupportedGraphicsBackend(value.graphics.backend)){SetError(error,"unsupported user graphics backend");return false;}
    if(value.graphics.qualityLevel<0||value.graphics.qualityLevel>3||value.graphics.shadowQuality<0||value.graphics.shadowQuality>3){SetError(error,"graphics quality must be between 0 and 3");return false;}
    if(!InRange(value.audio.master,0,1)||!InRange(value.audio.music,0,1)||!InRange(value.audio.effects,0,1)||!InRange(value.audio.voice,0,1)){SetError(error,"audio volumes must be between 0 and 1");return false;}
    if(!InRange(value.input.mouseSensitivity,0.01f,10)||!InRange(value.input.gamepadDeadZone,0,0.95f)||!InRange(value.input.gamepadSensitivity,0.1f,5)||!InRange(value.input.vibration,0,1)){SetError(error,"input preference is outside its supported range");return false;}
    if(!value.input.actionMap.is_null()){InputActionMap map;if(!map.LoadFromJson(value.input.actionMap,error))return false;}
    if(!InRange(value.accessibility.subtitleScale,0.5f,2)||!InRange(value.accessibility.uiScale,0.5f,2)){SetError(error,"accessibility scale must be between 0.5 and 2");return false;}
    constexpr std::array<const char*,4> modes={"none","protanopia","deuteranopia","tritanopia"};
    bool valid=false;for(const char* mode:modes)if(value.accessibility.colorVisionMode==mode)valid=true;
    if(!valid){SetError(error,"invalid color vision mode");return false;}if(error)error->clear();return true;
}

nlohmann::json RuntimeUserSettingsStore::ToJson(const RuntimeUserSettings& value)
{
    return{{"version",RuntimeUserSettings::CurrentVersion},{"display",{{"width",value.display.width},{"height",value.display.height},{"windowMode",value.display.windowMode},{"vsync",value.display.vsync},{"frameRateLimit",value.display.frameRateLimit},{"pauseWhenUnfocused",value.display.pauseWhenUnfocused}}},{"graphics",{{"backend",value.graphics.backend},{"qualityLevel",value.graphics.qualityLevel},{"shadowQuality",value.graphics.shadowQuality},{"postProcessing",value.graphics.postProcessing}}},{"audio",{{"master",value.audio.master},{"music",value.audio.music},{"effects",value.audio.effects},{"voice",value.audio.voice}}},{"input",{{"mouseSensitivity",value.input.mouseSensitivity},{"invertY",value.input.invertY},{"gamepadDeadZone",value.input.gamepadDeadZone},{"gamepadSensitivity",value.input.gamepadSensitivity},{"vibration",value.input.vibration},{"actionMap",value.input.actionMap}}},{"accessibility",{{"subtitles",value.accessibility.subtitles},{"subtitleScale",value.accessibility.subtitleScale},{"uiScale",value.accessibility.uiScale},{"reduceCameraShake",value.accessibility.reduceCameraShake},{"highContrast",value.accessibility.highContrast},{"colorVisionMode",value.accessibility.colorVisionMode}}}};
}

bool RuntimeUserSettingsStore::FromJson(nlohmann::json value,RuntimeUserSettings& settings,std::string* error)
{
    if(!value.is_object()){SetError(error,"user settings root must be an object");return false;}
    if(!value.contains("version"))value["version"]=0;
    JsonMigrationRegistry migrations("user settings",RuntimeUserSettings::CurrentVersion);
    if(!migrations.Register(0,[](nlohmann::json&,std::string*){return true;},error)||
       !migrations.Register(1,[](nlohmann::json& json,std::string*){if(!json.contains("input")||!json["input"].is_object())json["input"]=nlohmann::json::object();json["input"]["actionMap"]=nullptr;return true;},error)||
       !migrations.Register(2,[](nlohmann::json& json,std::string*){if(!json.contains("display")||!json["display"].is_object())json["display"]=nlohmann::json::object();json["display"]["pauseWhenUnfocused"]=true;return true;},error)||
       !migrations.Migrate(value,error))return false;
    RuntimeUserSettings result=Defaults();result.version=value.value("version",0);
    const auto display=value.value("display",nlohmann::json::object());const auto graphics=value.value("graphics",nlohmann::json::object());const auto audio=value.value("audio",nlohmann::json::object());const auto input=value.value("input",nlohmann::json::object());const auto accessibility=value.value("accessibility",nlohmann::json::object());
    if(!display.is_object()||!graphics.is_object()||!audio.is_object()||!input.is_object()||!accessibility.is_object()){SetError(error,"user settings sections must be objects");return false;}
    result.display.width=display.value("width",result.display.width);result.display.height=display.value("height",result.display.height);result.display.windowMode=display.value("windowMode",result.display.windowMode);result.display.vsync=display.value("vsync",result.display.vsync);result.display.frameRateLimit=display.value("frameRateLimit",result.display.frameRateLimit);result.display.pauseWhenUnfocused=display.value("pauseWhenUnfocused",result.display.pauseWhenUnfocused);
    result.graphics.backend=graphics.value("backend",result.graphics.backend);result.graphics.qualityLevel=graphics.value("qualityLevel",result.graphics.qualityLevel);result.graphics.shadowQuality=graphics.value("shadowQuality",result.graphics.shadowQuality);result.graphics.postProcessing=graphics.value("postProcessing",result.graphics.postProcessing);
    result.audio.master=audio.value("master",result.audio.master);result.audio.music=audio.value("music",result.audio.music);result.audio.effects=audio.value("effects",result.audio.effects);result.audio.voice=audio.value("voice",result.audio.voice);
    result.input.mouseSensitivity=input.value("mouseSensitivity",result.input.mouseSensitivity);result.input.invertY=input.value("invertY",result.input.invertY);result.input.gamepadDeadZone=input.value("gamepadDeadZone",result.input.gamepadDeadZone);result.input.gamepadSensitivity=input.value("gamepadSensitivity",result.input.gamepadSensitivity);result.input.vibration=input.value("vibration",result.input.vibration);result.input.actionMap=input.value("actionMap",result.input.actionMap);
    result.accessibility.subtitles=accessibility.value("subtitles",result.accessibility.subtitles);result.accessibility.subtitleScale=accessibility.value("subtitleScale",result.accessibility.subtitleScale);result.accessibility.uiScale=accessibility.value("uiScale",result.accessibility.uiScale);result.accessibility.reduceCameraShake=accessibility.value("reduceCameraShake",result.accessibility.reduceCameraShake);result.accessibility.highContrast=accessibility.value("highContrast",result.accessibility.highContrast);result.accessibility.colorVisionMode=accessibility.value("colorVisionMode",result.accessibility.colorVisionMode);
    if(!Validate(result,error))return false;settings=std::move(result);return true;
}

void RuntimeUserSettingsStore::SetStorageRoot(fs::path root){std::lock_guard<std::mutex> lock(g_SettingsMutex);g_SettingsRoot=std::move(root);}
void RuntimeUserSettingsStore::ClearStorageRootOverride(){std::lock_guard<std::mutex> lock(g_SettingsMutex);g_SettingsRoot.clear();}
fs::path RuntimeUserSettingsStore::GetStorageRoot(){std::lock_guard<std::mutex> lock(g_SettingsMutex);return g_SettingsRoot.empty()?AssetManager::Get().GetProjectRoot()/"Saved"/"Settings":g_SettingsRoot;}
fs::path RuntimeUserSettingsStore::GetSettingsPath(){return GetStorageRoot()/"UserSettings.json";}

bool RuntimeUserSettingsStore::Load(RuntimeUserSettings& settings,bool* usedBackup,std::string* error)
{
    if(usedBackup)*usedBackup=false;const fs::path path=GetSettingsPath();std::error_code ec;
    if(!fs::exists(path,ec)&&!ec){settings=Defaults();if(error)error->clear();return true;}
    if(ReadSettings(path,settings,error))return true;std::string primaryError=error?*error:std::string{};
    if(ReadSettings(path.string()+".bak",settings,error)){if(usedBackup)*usedBackup=true;return true;}
    if(error)*error=primaryError.empty()?"user settings and backup are invalid":primaryError+"; backup is also invalid";return false;
}

bool RuntimeUserSettingsStore::Save(const RuntimeUserSettings& settings,std::string* error)
{
    if(!Validate(settings,error))return false;TransactionalWriteOptions options;options.keepBackup=true;
    options.validator=[](const fs::path& path,std::string* validationError){RuntimeUserSettings ignored;return ReadSettings(path,ignored,validationError);};
    return TransactionalFileWriter::WriteText(GetSettingsPath(),ToJson(settings).dump(2)+"\n",options,error);
}
