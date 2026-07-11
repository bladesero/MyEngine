#include "Editor/InspectorSections.h"
#include "Editor/InspectorSectionGroups.h"

#include <memory>
#include <vector>

std::vector<std::unique_ptr<EditorInspectorSection>> CreateDefaultInspectorSections()
{
    std::vector<std::unique_ptr<EditorInspectorSection>> sections;
    RegisterAssetSceneInspectorSections(sections);
    RegisterTransformRenderInspectorSections(sections);
    RegisterGameplayInspectorSections(sections);
    RegisterAudioInspectorSections(sections);
    RegisterPhysicsInspectorSections(sections);
    RegisterScriptingInspectorSections(sections);
    RegisterUIInspectorSections(sections);
    RegisterAddComponentInspectorSections(sections);
    return sections;
}
