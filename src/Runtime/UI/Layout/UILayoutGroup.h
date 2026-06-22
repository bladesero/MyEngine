#pragma once

#include <string>
#include <utility>

class UILayoutGroup {
public:
    virtual ~UILayoutGroup() = default;
    const std::string& GetTargetElementID() const { return m_TargetElementID; }
    void SetTargetElementID(std::string id) { m_TargetElementID = std::move(id); }

protected:
    std::string m_TargetElementID;
};
