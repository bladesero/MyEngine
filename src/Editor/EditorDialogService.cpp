#include "Editor/EditorDialogService.h"

#include "Core/Window.h"
#include <SDL3/SDL_dialog.h>

namespace {
const SDL_DialogFileFilter kSceneFilters[] = {{"Scene JSON","json"}};
const SDL_DialogFileFilter kImportFilters[] = {
    {"Importable Assets","obj;gltf;glb;png;jpg;jpeg;bmp;tga;hdr"},
    {"Models","obj;gltf;glb"},{"Images","png;jpg;jpeg;bmp;tga;hdr"}};
void SDLCALL DialogCallback(void* userdata, const char* const* files, int filter) {
    (void)filter;
    auto* pair = static_cast<std::pair<EditorDialogService*,EditorFileOperation>*>(userdata);
    pair->first->Complete(pair->second, files); delete pair;
}
void Show(EditorDialogService* service, EditorFileOperation operation, IWindow* window,
          const SDL_DialogFileFilter* filters, int count, bool save) {
    if (!service || !window || !window->GetSDLWindow()) return;
    auto* data = new std::pair<EditorDialogService*,EditorFileOperation>(service, operation);
    if (save) SDL_ShowSaveFileDialog(DialogCallback,data,window->GetSDLWindow(),filters,count,nullptr);
    else SDL_ShowOpenFileDialog(DialogCallback,data,window->GetSDLWindow(),filters,count,nullptr,false);
}
}
void EditorDialogService::RequestOpenScene(IWindow* window) { Show(this,EditorFileOperation::OpenScene,window,kSceneFilters,1,false); }
void EditorDialogService::RequestSaveScene(IWindow* window) { Show(this,EditorFileOperation::SaveScene,window,kSceneFilters,1,true); }
void EditorDialogService::RequestImportAsset(IWindow* window) { Show(this,EditorFileOperation::ImportAsset,window,kImportFilters,3,false); }
void EditorDialogService::RequestOpenProjectFolder(IWindow* window) {
    if (!window || !window->GetSDLWindow()) return;
    auto* data = new std::pair<EditorDialogService*,EditorFileOperation>(
        this, EditorFileOperation::OpenProjectFolder);
    SDL_ShowOpenFolderDialog(DialogCallback, data, window->GetSDLWindow(), nullptr, false);
}
void EditorDialogService::Complete(EditorFileOperation operation, const char* const* files) {
    std::lock_guard<std::mutex> lock(m_Mutex); m_Result.operation=operation; m_Result.path=(files&&files[0])?files[0]:"";
}
bool EditorDialogService::ConsumeResult(EditorDialogResult& result) {
    std::lock_guard<std::mutex> lock(m_Mutex); if(m_Result.operation==EditorFileOperation::None) return false;
    result=std::move(m_Result); m_Result={}; return true;
}
