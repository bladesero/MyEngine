#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Scene/Component.h"

#include <cstdint>

class MYENGINE_RUNTIME_API ColliderComponent : public Component {
public:
    bool IsTrigger() const { return m_IsTrigger; }
    void SetTrigger(bool trigger) { m_IsTrigger = trigger; }
    uint32_t GetLayer() const { return m_Layer; }
    void SetLayer(uint32_t layer) { m_Layer = layer ? layer : 1u; }
    void OnOwnerLayerChanged(uint32_t layer) override { SetLayer(layer); }
    uint32_t GetLayerMask() const { return m_LayerMask; }
    void SetLayerMask(uint32_t mask) { m_LayerMask = mask; }

protected:
    void SerializeCollider(nlohmann::json& data) const {
        data["isTrigger"] = m_IsTrigger;
        data["layer"] = m_Layer;
        data["layerMask"] = m_LayerMask;
    }
    void DeserializeCollider(const nlohmann::json& data) {
        SetTrigger(data.value("isTrigger", false));
        SetLayer(data.value("layer", uint32_t{1}));
        SetLayerMask(data.value("layerMask", ~uint32_t{0}));
    }

private:
    bool m_IsTrigger = false;
    uint32_t m_Layer = 1;
    uint32_t m_LayerMask = ~uint32_t{0};
};
