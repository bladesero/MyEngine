#include "Editor/EditorLogService.h"

#include "Core/Logger.h"

void EditorLogService::OnAttach(EditorContext& context) {
    EditorService::OnAttach(context);
    Logger::SetSink([this](const std::string& line) { Push(line); });
}
void EditorLogService::OnDetach() {
    Logger::SetSink({});
    EditorService::OnDetach();
}
void EditorLogService::Push(const std::string& line) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Lines.size() >= 4096)
        m_Lines.pop_front();
    m_Lines.push_back(line);
    m_ScrollRequested = true;
}
std::vector<std::string> EditorLogService::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return {m_Lines.begin(), m_Lines.end()};
}
void EditorLogService::Clear() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Lines.clear();
}
bool EditorLogService::ConsumeScrollRequest() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const bool value = m_ScrollRequested;
    m_ScrollRequested = false;
    return value;
}
