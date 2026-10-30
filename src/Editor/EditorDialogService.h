#pragma once

#include "Editor/EditorService.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>

class IWindow;

enum class EditorFileOperation { None, OpenScene, SaveScene, ImportAsset, OpenProjectFolder };
struct EditorDialogResult {
    EditorFileOperation operation = EditorFileOperation::None;
    std::string path;
    std::string error;
};

class EditorDialogService final : public EditorService {
public:
    void RequestOpenScene(IWindow* window);
    void RequestSaveScene(IWindow* window);
    void RequestImportAsset(IWindow* window);
    void RequestOpenProjectFolder(IWindow* window);
    bool ConsumeResult(EditorDialogResult& result);
    void Complete(EditorFileOperation operation, const char* const* files);

private:
    struct State {
        std::mutex mutex;
        EditorDialogResult result;
    };
    using Completion = std::function<void(const char* const*)>;
    Completion MakeCompletion(EditorFileOperation operation);
    std::shared_ptr<State> m_State = std::make_shared<State>();
};
