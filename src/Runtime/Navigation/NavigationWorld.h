#pragma once

#include "Core/EngineMath.h"
#include "Scene/ActorHandle.h"

#include <cstdint>
#include <vector>

class NavigationWorld {
public:
    struct BakeSettings {
        AABB bounds{{-10, 0, -10}, {10, 2, 10}};
        float cellSize = 0.5f;
        float agentRadius = 0.4f;
    };
    struct SoundEvent {
        Vec3 position;
        float radius = 0.0f;
        ActorHandle source;
        float remaining = 0.0f;
    };
    bool Bake(const BakeSettings& settings, const std::vector<AABB>& obstacles = {});
    void Clear();
    bool IsBaked() const { return m_Width > 0 && m_Height > 0; }
    bool FindPath(const Vec3& start, const Vec3& goal, std::vector<Vec3>& outPath) const;
    bool IsWalkable(const Vec3& position) const;
    void EmitSound(const Vec3& position, float radius, ActorHandle source, float duration = 0.25f);
    std::vector<SoundEvent> QuerySounds(const Vec3& listener) const;
    void Update(float deltaSeconds);
    const BakeSettings& GetSettings() const { return m_Settings; }
    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }
    const std::vector<uint8_t>& GetCells() const { return m_Walkable; }
    bool SetBakedData(const BakeSettings& settings, uint32_t width, uint32_t height, std::vector<uint8_t> cells);
    bool SetAreaWalkable(const AABB& bounds, bool walkable);
    uint64_t GetRevision() const { return m_Revision; }

private:
    bool ToCell(const Vec3& position, int& x, int& z) const;
    Vec3 CellCenter(int x, int z) const;
    BakeSettings m_Settings;
    uint32_t m_Width = 0, m_Height = 0;
    std::vector<uint8_t> m_Walkable;
    std::vector<SoundEvent> m_Sounds;
    uint64_t m_Revision = 0;
};
