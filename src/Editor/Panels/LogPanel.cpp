#include "Editor/EditorPanels.h"

#include "Editor/EditorContext.h"
#include "Editor/EditorLayout.h"
#include "Editor/EditorLogService.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

LogPanel::LogPanel():EditorPanel("log","Log Output"){}
void LogPanel::OnImGui(){if(IsVisible())DrawContent();}
void LogPanel::DrawContent(){
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* context=GetContext();auto* logs=context?context->GetService<EditorLogService>():nullptr;if(!logs)return;const auto* viewport=ImGui::GetMainViewport();const auto rect=EditorLayout::Compute(viewport->WorkPos.x,viewport->WorkPos.y,viewport->WorkSize.x,viewport->WorkSize.y).log;ImGui::SetNextWindowPos({rect.x,rect.y});ImGui::SetNextWindowSize({rect.width,rect.height});ImGui::Begin("Log Output");
    if(ImGui::Button("Clear"))logs->Clear();ImGui::SameLine();bool autoScroll=logs->IsAutoScroll();if(ImGui::Checkbox("Auto-scroll",&autoScroll))logs->SetAutoScroll(autoScroll);ImGui::Separator();ImGui::BeginChild("##LogScroll",{0,0},false,ImGuiWindowFlags_HorizontalScrollbar);
    for(const std::string& line:logs->Snapshot()){const bool error=line.find("ShaderCompileError")!=std::string::npos||line.find("[Error]")!=std::string::npos;if(error)ImGui::PushStyleColor(ImGuiCol_Text,{1,0.42f,0.38f,1});ImGui::TextUnformatted(line.c_str());if(error)ImGui::PopStyleColor();}if(logs->IsAutoScroll()&&logs->ConsumeScrollRequest())ImGui::SetScrollHereY(1);ImGui::EndChild();ImGui::End();
#endif
}
