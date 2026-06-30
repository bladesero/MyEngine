#pragma once

#include "Editor/EditorService.h"

class EditorAngelScriptDomain;

class EditorScriptHotReloadService final : public EditorService {
public:
    void SetDomain(EditorAngelScriptDomain* domain) { m_Domain = domain; }
    void OnUpdate(float deltaSeconds) override;

private:
    EditorAngelScriptDomain* m_Domain = nullptr;
    float m_Accumulator = 0.0f;
};
