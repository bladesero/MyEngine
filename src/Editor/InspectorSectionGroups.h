#pragma once

#include "Editor/EditorInspectorSection.h"

#include <memory>
#include <vector>

void RegisterAssetSceneInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections);
void RegisterTransformRenderInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections);
void RegisterGameplayInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections);
void RegisterAudioInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections);
void RegisterPhysicsInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections);
void RegisterScriptingInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections);
void RegisterUIInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections);
void RegisterAddComponentInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections);
