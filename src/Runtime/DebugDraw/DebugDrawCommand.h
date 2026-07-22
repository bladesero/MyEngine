#pragma once

#include "Assets/MeshAsset.h"
#include "Core/EngineMath.h"
#include "Scene/Scene.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

enum class DebugDrawDepthMode : uint8_t {
    Test,
    Always,
};

enum class DebugDrawViewMask : uint8_t {
    None = 0,
    Authoring = 1u << 0u,
    Runtime = 1u << 1u,
    All = (1u << 0u) | (1u << 1u),
};

constexpr DebugDrawViewMask operator|(DebugDrawViewMask lhs, DebugDrawViewMask rhs) {
    return static_cast<DebugDrawViewMask>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr bool DebugDrawViewMatches(DebugDrawViewMask commandMask, DebugDrawViewMask viewMask) {
    return (static_cast<uint8_t>(commandMask) & static_cast<uint8_t>(viewMask)) != 0u;
}

enum class DebugDrawGeometryKind : uint8_t {
    Line,
    Box,
    Sphere,
    Capsule,
    MeshAsset,
    ProceduralMesh,
};

class DebugDrawMesh {
public:
    DebugDrawMesh(std::vector<Vec3> positions, std::vector<uint32_t> lineIndices)
        : m_Positions(std::move(positions)), m_LineIndices(std::move(lineIndices)) {}

    const std::vector<Vec3>& GetPositions() const { return m_Positions; }
    const std::vector<uint32_t>& GetLineIndices() const { return m_LineIndices; }
    bool IsValid() const {
        if (m_Positions.empty() || m_LineIndices.empty() || m_LineIndices.size() % 2u != 0u)
            return false;
        for (uint32_t index : m_LineIndices) {
            if (index >= m_Positions.size())
                return false;
        }
        return true;
    }

private:
    std::vector<Vec3> m_Positions;
    std::vector<uint32_t> m_LineIndices;
};

using DebugDrawMeshHandle = std::shared_ptr<const DebugDrawMesh>;

struct DebugDrawCommand {
    SceneLifetimeToken scene;
    uint64_t sceneGeneration = 0;
    DebugDrawGeometryKind geometry = DebugDrawGeometryKind::Line;
    Mat4 transform = Mat4::Identity();
    Color color{};
    DebugDrawDepthMode depthMode = DebugDrawDepthMode::Test;
    DebugDrawViewMask viewMask = DebugDrawViewMask::All;
    float durationSeconds = 0.0f;
    // Capsule instances use x=radius, y=center-line half length and w=1.
    // Other geometry leaves this at zero.
    Vec4 shapeParameters{0.0f, 0.0f, 0.0f, 0.0f};
    MeshHandle mesh;
    DebugDrawMeshHandle proceduralMesh;
    uint64_t sequence = 0;
};
