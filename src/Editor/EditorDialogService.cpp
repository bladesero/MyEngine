#include "Editor/EditorDialogService.h"

#include "Core/Window.h"
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>

namespace {
const SDL_DialogFileFilter kSceneFilters[] = {{"Scene JSON", "json"}};
const SDL_DialogFileFilter kImportFilters[] = {{"Importable Assets", "obj;gltf;glb;png;jpg;jpeg;bmp;tga;hdr"},
                                               {"Models", "obj;gltf;glb"},
                                               {"Images", "png;jpg;jpeg;bmp;tga;hdr"}};
void SDLCALL DialogCallback(void* userdata, const char* const* files, int filter) {
    (void)filter;
    auto* completion = static_cast<std::function<void(const char* const*)>*>(userdata);
    (*completion)(files);
    delete completion;
}
void Show(std::function<void(const char* const*)> completion, IWindow* window, const SDL_DialogFileFilter* filters,
          int count, bool save) {
    if (!window || !window->GetSDLWindow())
        return;
    auto* data = new std::function<void(const char* const*)>(std::move(completion));
    if (save)
        SDL_ShowSaveFileDialog(DialogCallback, data, window->GetSDLWindow(), filters, count, nullptr);
    else
        SDL_ShowOpenFileDialog(DialogCallback, data, window->GetSDLWindow(), filters, count, nullptr, false);
}
} // namespace
void EditorDialogService::RequestOpenScene(IWindow* window) {
    Show(MakeCompletion(EditorFileOperation::OpenScene), window, kSceneFilters, 1, false);
}
void EditorDialogService::RequestSaveScene(IWindow* window) {
    Show(MakeCompletion(EditorFileOperation::SaveScene), window, kSceneFilters, 1, true);
}
void EditorDialogService::RequestImportAsset(IWindow* window) {
    Show(MakeCompletion(EditorFileOperation::ImportAsset), window, kImportFilters, 3, false);
}
void EditorDialogService::RequestOpenProjectFolder(IWindow* window) {
    if (!window || !window->GetSDLWindow())
        return;
    auto* data = new Completion(MakeCompletion(EditorFileOperation::OpenProjectFolder));
    SDL_ShowOpenFolderDialog(DialogCallback, data, window->GetSDLWindow(), nullptr, false);
}
EditorDialogService::Completion EditorDialogService::MakeCompletion(EditorFileOperation operation) {
    // SDL may complete on a different thread, after the editor/service has closed.
    // Retain only the result state; never capture the service or its window.
    return [state = m_State, operation](const char* const* files) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->result.operation = operation;
        state->result.path = (files && files[0]) ? files[0] : "";
        state->result.error = files ? "" : SDL_GetError();
        if (!files && state->result.error.empty())
            state->result.error = "The desktop file dialog is unavailable.";
    };
}
void EditorDialogService::Complete(EditorFileOperation operation, const char* const* files) {
    MakeCompletion(operation)(files);
}
bool EditorDialogService::ConsumeResult(EditorDialogResult& result) {
    std::lock_guard<std::mutex> lock(m_State->mutex);
    if (m_State->result.operation == EditorFileOperation::None)
        return false;
    result = std::move(m_State->result);
    m_State->result = {};
    return true;
}
