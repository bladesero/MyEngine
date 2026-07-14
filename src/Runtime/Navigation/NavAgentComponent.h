#pragma once

#include "Scene/Component.h"

#include <vector>

class NavAgentComponent final : public Component {
public:
    const char* GetTypeName() const override { return "NavAgent"; }
    int GetExecutionOrder() const override { return 20; }
    void OnUpdate(float deltaSeconds) override;
    bool SetDestination(const Vec3& destination);
    void Stop();
    const Vec3& GetDestination() const { return m_Destination; }
    bool HasPath() const { return m_PathIndex < m_Path.size(); }
    bool ReachedDestination() const { return m_Reached; }
    void SetSpeed(float value);
    float GetSpeed() const { return m_Speed; }
    void SetStoppingDistance(float value);
    float GetStoppingDistance() const { return m_StoppingDistance; }
    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    Vec3 m_Destination = Vec3::Zero();
    std::vector<Vec3> m_Path;
    size_t m_PathIndex = 0;
    float m_Speed = 3.0f, m_StoppingDistance = 0.15f;
    bool m_Reached = true;
    uint64_t m_PathRevision = 0;
};
