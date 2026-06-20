#pragma once

#include "Scene/ActorHandle.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Actor;
class Scene;

enum class EditorSelectObjectType : uint8_t {
    None,
    Actor,
    Asset,
};

class EditorSelectObject {
public:
    static EditorSelectObject MakeActor(ActorHandle handle, uint64_t persistentID = 0);
    static EditorSelectObject MakeActor(uint64_t persistentID);
    static EditorSelectObject MakeAsset(std::string path);

    EditorSelectObjectType GetType() const { return m_Type; }
    bool IsNone() const { return m_Type == EditorSelectObjectType::None; }
    bool IsActor() const { return m_Type == EditorSelectObjectType::Actor; }
    bool IsAsset() const { return m_Type == EditorSelectObjectType::Asset; }
    ActorHandle GetActorHandle() const { return m_ActorHandle; }
    uint64_t GetActorID() const { return m_ActorID; }
    const std::string& GetAssetPath() const { return m_AssetPath; }

    friend bool operator==(const EditorSelectObject& left,
                           const EditorSelectObject& right);
    friend bool operator!=(const EditorSelectObject& left,
                           const EditorSelectObject& right) {
        return !(left == right);
    }

private:
    EditorSelectObjectType m_Type = EditorSelectObjectType::None;
    ActorHandle m_ActorHandle;
    uint64_t m_ActorID = 0;
    std::string m_AssetPath;
};

enum class EditorSelectionMode : uint8_t {
    Replace,
    Add,
    Toggle,
};

struct EditorSelectionChangedEvent {
    EditorSelectObject previous;
    EditorSelectObject current;
    std::vector<uint64_t> actorIDs;
};

class EditorSelection {
public:
    using ListenerID = uint64_t;
    using SelectionChangedCallback =
        std::function<void(const EditorSelectionChangedEvent&)>;

    void Select(EditorSelectObject object,
                EditorSelectionMode mode = EditorSelectionMode::Replace);
    const EditorSelectObject& GetPrimaryObject() const { return m_PrimaryObject; }
    ListenerID SubscribeSelectionChanged(SelectionChangedCallback callback);
    void UnsubscribeSelectionChanged(ListenerID listenerID);

    void SelectActorID(uint64_t actorID);
    void SelectActorHandle(ActorHandle actor);
    void SelectAssetPath(std::string path);
    void Clear();
    void Validate(Scene& scene);

    Actor* ResolveActor(Scene& scene) const;
    const Actor* ResolveActor(const Scene& scene) const;
    uint64_t GetActorID() const { return m_PrimaryObject.GetActorID(); }
    ActorHandle GetActorHandle() const { return m_PrimaryObject.GetActorHandle(); }
    const std::string& GetAssetPath() const { return m_PrimaryObject.GetAssetPath(); }
    bool HasActor() const { return m_PrimaryObject.IsActor(); }
    bool HasAsset() const { return m_PrimaryObject.IsAsset(); }

    bool IsMultiSelect() const { return m_MultiActorIDs.size() > 1; }
    const std::vector<uint64_t>& GetActorIDs() const { return m_MultiActorIDs; }
    void ToggleActorID(uint64_t actorID);
    void AddToMultiSelect(uint64_t actorID);
    void RemoveFromMultiSelect(uint64_t actorID);
    bool IsSelected(uint64_t actorID) const;
    size_t GetMultiCount() const { return m_MultiActorIDs.size(); }
    void ClearMultiSelect();

private:
    void PublishIfChanged(const EditorSelectObject& previous,
                          const std::vector<uint64_t>& previousActorIDs);

    EditorSelectObject m_PrimaryObject;
    std::vector<uint64_t> m_MultiActorIDs;
    std::unordered_map<ListenerID, SelectionChangedCallback> m_Listeners;
    ListenerID m_NextListenerID = 1;
};
