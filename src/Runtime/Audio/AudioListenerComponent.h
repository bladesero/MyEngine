#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Scene/Component.h"

class MYENGINE_RUNTIME_API AudioListenerComponent final : public Component {
public:
    const char* GetTypeName() const override { return "AudioListener"; }
    int GetExecutionOrder() const override { return 900; }
    void OnLateUpdate(float deltaSeconds) override;
    void SetPrimary(bool value) { m_Primary = value; }
    bool IsPrimary() const { return m_Primary; }
    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    bool m_Primary = true;
};
