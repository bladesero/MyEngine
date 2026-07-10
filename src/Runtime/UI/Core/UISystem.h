#pragma once

#include "Core/EngineMath.h"
#include "UI/Rml/RmlAssetLoader.h"
#include "UI/Rml/RmlContextManager.h"
#include "UI/Rml/RmlInputAdapter.h"
#include "UI/Input/UIInputSystem.h"
#include "UI/Rml/RmlRenderInterface.h"
#include "UI/Render/UIDrawList.h"
#include "UI/UIEventBridge.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <nlohmann/json.hpp>

struct Event;
class IRHIDevice;
class IRHIFrameContext;
class Scene;

class UIDataModel {
public:
    using Value = std::variant<bool, int, float, std::string, Vec2, Vec3, nlohmann::json>;

    void SetBool(const std::string& name, bool value) { m_Values[name] = value; m_Dirty = true; }
    void SetInt(const std::string& name, int value) { m_Values[name] = value; m_Dirty = true; }
    void SetFloat(const std::string& name, float value) { m_Values[name] = value; m_Dirty = true; }
    void SetString(const std::string& name, std::string value) { m_Values[name] = std::move(value); m_Dirty = true; }
    void SetVec2(const std::string& name, const Vec2& value) { m_Values[name] = value; m_Dirty = true; }
    void SetVec3(const std::string& name, const Vec3& value) { m_Values[name] = value; m_Dirty = true; }
    void SetJson(const std::string& name, nlohmann::json value) { m_Values[name] = std::move(value); m_Dirty = true; }
    const std::unordered_map<std::string, Value>& GetValues() const { return m_Values; }
    void MarkDirty() { m_Dirty = true; }
    bool ConsumeDirty() { const bool dirty = m_Dirty; m_Dirty = false; return dirty; }

private:
    std::unordered_map<std::string, Value> m_Values;
    bool m_Dirty = false;
};

class UISystem {
public:
    UISystem();
    ~UISystem();

    bool Initialize(IRHIDevice* device, IRHIFrameContext* frameContext);
    void Shutdown();
    void Resize(int width, int height);

    void Update(Scene& scene, float dt);
    bool ProcessEvent(Event& event);
    bool ProcessEvent(Scene& scene, Event& event, const UIInputViewport& viewport);
    void CollectDrawData(Scene& scene, UIDrawList& drawList);

    UIDataModel& CreateDataModel(const std::string& name);
    UIEventBridge& GetEventBridge() { return *GetActiveEventBridge(); }
    void SetEventBridge(UIEventBridge* eventBridge) { m_ExternalEventBridge = eventBridge; }
    void MarkActorTreeDirty(uint64_t canvasActorID);

private:
    void EnsureCanvasDocuments(Scene& scene);
    void LoadCanvasFonts(Scene& scene);
    void ApplyDataModels(Scene& scene);
    UIEventBridge* GetActiveEventBridge();
    static UIDataModel* ResolveDataModel(void* user, const std::string& name);

    IRHIDevice* m_Device = nullptr;
    IRHIFrameContext* m_FrameContext = nullptr;
    bool m_Initialized = false;
    int m_Width = 1;
    int m_Height = 1;
    RmlAssetLoader m_AssetLoader;
    RmlRenderInterface m_RenderInterface;
    RmlContextManager m_ContextManager;
    RmlInputAdapter m_InputAdapter;
    UIInputSystem m_UIInputSystem;
    UIEventBridge m_EventBridge;
    UIEventBridge* m_ExternalEventBridge = nullptr;
    std::unordered_map<std::string, UIDataModel> m_DataModels;
    std::unordered_map<std::string, bool> m_LoadedFonts;
    std::unordered_map<uint64_t, std::size_t> m_ActorTreeSignatures;
};
