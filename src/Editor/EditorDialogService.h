#pragma once

#include "Editor/EditorService.h"

#include <mutex>
#include <string>

class IWindow;

enum class EditorFileOperation { None, OpenScene, SaveScene, ImportAsset, OpenProjectFolder };
struct EditorDialogResult { EditorFileOperation operation=EditorFileOperation::None; std::string path; };

class EditorDialogService final : public EditorService {
public:
    void RequestOpenScene(IWindow* window);
    void RequestSaveScene(IWindow* window);
    void RequestImportAsset(IWindow* window);
    void RequestOpenProjectFolder(IWindow* window);
    bool ConsumeResult(EditorDialogResult& result);
    void Complete(EditorFileOperation operation, const char* const* files);
private:
    std::mutex m_Mutex;
    EditorDialogResult m_Result;
};
