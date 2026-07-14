#include "Navigation/NavigationWorld.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

bool NavigationWorld::Bake(const BakeSettings& settings, const std::vector<AABB>& obstacles) {
    if (settings.cellSize <= 0.01f || settings.bounds.max.x <= settings.bounds.min.x ||
        settings.bounds.max.z <= settings.bounds.min.z)
        return false;
    m_Settings = settings;
    m_Settings.cellSize = std::max(0.05f, settings.cellSize);
    m_Width = static_cast<uint32_t>(std::ceil((settings.bounds.max.x - settings.bounds.min.x) / m_Settings.cellSize));
    m_Height = static_cast<uint32_t>(std::ceil((settings.bounds.max.z - settings.bounds.min.z) / m_Settings.cellSize));
    if (m_Width == 0 || m_Height == 0 || static_cast<uint64_t>(m_Width) * m_Height > 4000000ull) {
        m_Width = m_Height = 0;
        m_Walkable.clear();
        return false;
    }
    m_Walkable.assign(static_cast<size_t>(m_Width) * m_Height, 1);
    for (uint32_t z = 0; z < m_Height; ++z)
        for (uint32_t x = 0; x < m_Width; ++x) {
            const Vec3 center = CellCenter(x, z);
            for (const AABB& obstacle : obstacles) {
                const float r = m_Settings.agentRadius;
                if (center.x >= obstacle.min.x - r && center.x <= obstacle.max.x + r &&
                    center.z >= obstacle.min.z - r && center.z <= obstacle.max.z + r) {
                    m_Walkable[z * m_Width + x] = 0;
                    break;
                }
            }
        }
    ++m_Revision;
    return true;
}
void NavigationWorld::Clear() {
    m_Width = 0;
    m_Height = 0;
    m_Walkable.clear();
    m_Sounds.clear();
    ++m_Revision;
}
bool NavigationWorld::ToCell(const Vec3& p, int& x, int& z) const {
    x = static_cast<int>(std::floor((p.x - m_Settings.bounds.min.x) / m_Settings.cellSize));
    z = static_cast<int>(std::floor((p.z - m_Settings.bounds.min.z) / m_Settings.cellSize));
    return x >= 0 && z >= 0 && x < static_cast<int>(m_Width) && z < static_cast<int>(m_Height);
}
Vec3 NavigationWorld::CellCenter(int x, int z) const {
    return {m_Settings.bounds.min.x + (x + 0.5f) * m_Settings.cellSize, m_Settings.bounds.min.y,
            m_Settings.bounds.min.z + (z + 0.5f) * m_Settings.cellSize};
}
bool NavigationWorld::IsWalkable(const Vec3& p) const {
    int x, z;
    return ToCell(p, x, z) && m_Walkable[static_cast<size_t>(z) * m_Width + x] != 0;
}
bool NavigationWorld::FindPath(const Vec3& start, const Vec3& goal, std::vector<Vec3>& out) const {
    out.clear();
    int sx, sz, gx, gz;
    if (!ToCell(start, sx, sz) || !ToCell(goal, gx, gz))
        return false;
    const int count = static_cast<int>(m_Width * m_Height), source = sz * static_cast<int>(m_Width) + sx,
              target = gz * static_cast<int>(m_Width) + gx;
    if (!m_Walkable[source] || !m_Walkable[target])
        return false;
    struct Node {
        float score;
        int index;
        bool operator<(const Node& o) const { return score > o.score; }
    };
    std::priority_queue<Node> open;
    std::vector<float> cost(count, std::numeric_limits<float>::infinity());
    std::vector<int> parent(count, -1);
    cost[source] = 0;
    open.push({0, source});
    constexpr int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
    while (!open.empty()) {
        int current = open.top().index;
        open.pop();
        if (current == target)
            break;
        int x = current % static_cast<int>(m_Width), z = current / static_cast<int>(m_Width);
        for (int i = 0; i < 4; ++i) {
            int nx = x + dx[i], nz = z + dz[i];
            if (nx < 0 || nz < 0 || nx >= static_cast<int>(m_Width) || nz >= static_cast<int>(m_Height))
                continue;
            int next = nz * static_cast<int>(m_Width) + nx;
            if (!m_Walkable[next])
                continue;
            float nextCost = cost[current] + 1;
            if (nextCost >= cost[next])
                continue;
            cost[next] = nextCost;
            parent[next] = current;
            float h = static_cast<float>(std::abs(nx - gx) + std::abs(nz - gz));
            open.push({nextCost + h, next});
        }
    }
    if (source != target && parent[target] < 0)
        return false;
    std::vector<Vec3> reversed;
    for (int node = target; node >= 0 && node != source; node = parent[node])
        reversed.push_back(CellCenter(node % static_cast<int>(m_Width), node / static_cast<int>(m_Width)));
    reversed.push_back(start);
    out.assign(reversed.rbegin(), reversed.rend());
    out.push_back(goal);
    return true;
}
void NavigationWorld::EmitSound(const Vec3& p, float radius, ActorHandle source, float duration) {
    if (radius > 0)
        m_Sounds.push_back({p, radius, source, std::max(0.01f, duration)});
}
std::vector<NavigationWorld::SoundEvent> NavigationWorld::QuerySounds(const Vec3& p) const {
    std::vector<SoundEvent> out;
    for (const auto& sound : m_Sounds)
        if ((sound.position - p).LengthSq() <= sound.radius * sound.radius)
            out.push_back(sound);
    return out;
}
void NavigationWorld::Update(float dt) {
    for (auto& sound : m_Sounds)
        sound.remaining -= std::max(0.0f, dt);
    m_Sounds.erase(
        std::remove_if(m_Sounds.begin(), m_Sounds.end(), [](const auto& sound) { return sound.remaining <= 0; }),
        m_Sounds.end());
}
bool NavigationWorld::SetBakedData(const BakeSettings& settings, uint32_t width, uint32_t height,
                                   std::vector<uint8_t> cells) {
    if (width == 0 || height == 0 || cells.size() != static_cast<size_t>(width) * height || settings.cellSize <= 0)
        return false;
    m_Settings = settings;
    m_Width = width;
    m_Height = height;
    m_Walkable = std::move(cells);
    ++m_Revision;
    return true;
}
bool NavigationWorld::SetAreaWalkable(const AABB& bounds, bool walkable) {
    if (!IsBaked())
        return false;
    bool changed = false;
    for (uint32_t z = 0; z < m_Height; ++z)
        for (uint32_t x = 0; x < m_Width; ++x) {
            const Vec3 p = CellCenter(x, z);
            if (p.x < bounds.min.x || p.x > bounds.max.x || p.z < bounds.min.z || p.z > bounds.max.z)
                continue;
            uint8_t& cell = m_Walkable[z * m_Width + x];
            const uint8_t value = walkable ? 1 : 0;
            if (cell != value) {
                cell = value;
                changed = true;
            }
        }
    if (changed)
        ++m_Revision;
    return changed;
}
