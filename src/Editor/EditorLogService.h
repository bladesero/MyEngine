#pragma once

#include "Editor/EditorService.h"

#include <deque>
#include <mutex>
#include <string>
#include <vector>

class EditorLogService final : public EditorService {
public:
    void OnAttach(EditorContext& context) override;
    void OnDetach() override;
    std::vector<std::string> Snapshot() const;
    void Clear();
    bool IsAutoScroll() const { return m_AutoScroll; }
    void SetAutoScroll(bool value) { m_AutoScroll = value; }
    bool ConsumeScrollRequest();
private:
    void Push(const std::string& line);
    mutable std::mutex m_Mutex;
    std::deque<std::string> m_Lines;
    bool m_AutoScroll = true;
    bool m_ScrollRequested = false;
};
