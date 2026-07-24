#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Scene/SceneSubsystems.h"

#include <cstdint>
#include <vector>

class MYENGINE_RUNTIME_API NavigationWorld final : public ISceneNavigationSubsystem {
public:
    using BakeSettings = NavMeshBakeSettings;
    using SoundEvent = NavigationSoundEvent;
    bool Bake(const BakeSettings& settings, const std::vector<AABB>& obstacles = {}) override;
    void Clear() override;
    bool IsBaked() const override { return m_Width > 0 && m_Height > 0; }
    bool FindPath(const Vec3& start, const Vec3& goal, std::vector<Vec3>& outPath) const override;
    bool IsWalkable(const Vec3& position) const override;
    void EmitSound(const Vec3& position, float radius, ActorHandle source, float duration = 0.25f) override;
    std::vector<SoundEvent> QuerySounds(const Vec3& listener) const override;
    void Update(float deltaSeconds) override;
    const BakeSettings& GetSettings() const override { return m_Settings; }
    uint32_t GetWidth() const override { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }
    const std::vector<uint8_t>& GetCells() const override { return m_Walkable; }
    bool SetBakedData(const BakeSettings& settings, uint32_t width, uint32_t height,
                      std::vector<uint8_t> cells) override;
    bool SetAreaWalkable(const AABB& bounds, bool walkable) override;
    uint64_t GetRevision() const override { return m_Revision; }

private:
    bool ToCell(const Vec3& position, int& x, int& z) const;
    Vec3 CellCenter(int x, int z) const;
    BakeSettings m_Settings;
    uint32_t m_Width = 0, m_Height = 0;
    std::vector<uint8_t> m_Walkable;
    std::vector<SoundEvent> m_Sounds;
    uint64_t m_Revision = 0;
};
