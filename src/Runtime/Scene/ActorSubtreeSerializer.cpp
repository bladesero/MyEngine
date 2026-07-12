#include "Scene/ActorSubtreeSerializer.h"

#include "Scene/Actor.h"
#include "Scene/TypeRegistry.h"

#include <array>
#include <functional>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_map>

namespace {
std::string MakeLocalId() {
    static std::mt19937_64 random{std::random_device{}()};
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << random()
           << std::setw(16) << random();
    return stream.str();
}
}

bool ActorSubtreeSerializer::Serialize(const Actor& root, std::vector<PrefabNode>& nodes,
                                       std::string* error,
                                       const std::unordered_set<const Actor*>* excludedRoots,
                                       std::unordered_map<const Actor*, std::string>* actorLocalIds)
{
    nodes.clear();
    std::function<void(const Actor&, const std::string&)> visit = [&](const Actor& actor, const std::string& parentId) {
        if (excludedRoots && excludedRoots->count(&actor)) return;
        PrefabNode node;
        node.localId = actor.GetPrefabLocalId().empty() ? MakeLocalId() : actor.GetPrefabLocalId();
        node.parentLocalId = parentId;
        node.name = actor.GetName();
        node.activeSelf = actor.IsActiveSelf();
        node.editorFlags = actor.GetEditorFlags();
        node.transform = actor.GetTransform();
        actor.ForEachComponent([&](Component& component) {
            ComponentCreateDesc desc;
            desc.type = component.GetTypeName();
            desc.enabled = component.IsEnabled();
            if (!TypeRegistry::Get().Serialize(component, desc.data, desc.version, error))
                component.Serialize(desc.data);
            node.components.push_back(std::move(desc));
        });
        const std::string localId = node.localId;
        if (actorLocalIds) (*actorLocalIds)[&actor] = localId;
        nodes.push_back(std::move(node));
        for (Actor* child : actor.GetChildren()) visit(*child, localId);
    };
    visit(root, {});
    if (nodes.empty()) { if (error) *error = "actor subtree is empty"; return false; }
    return true;
}
