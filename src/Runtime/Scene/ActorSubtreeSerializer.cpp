#include "Scene/ActorSubtreeSerializer.h"

#include "Scene/Actor.h"

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
                                       std::string* error)
{
    nodes.clear();
    std::function<void(const Actor&, const std::string&)> visit = [&](const Actor& actor, const std::string& parentId) {
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
            component.Serialize(desc.data);
            node.components.push_back(std::move(desc));
        });
        const std::string localId = node.localId;
        nodes.push_back(std::move(node));
        for (Actor* child : actor.GetChildren()) visit(*child, localId);
    };
    visit(root, {});
    if (nodes.empty()) { if (error) *error = "actor subtree is empty"; return false; }
    return true;
}
