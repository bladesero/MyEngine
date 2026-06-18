#pragma once

#include <memory>
#include <vector>

class EditorInspectorSection;
std::vector<std::unique_ptr<EditorInspectorSection>> CreateDefaultInspectorSections();
