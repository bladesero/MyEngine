#pragma once

#include "Assets/Asset.h"
#include "Core/Math.h"
#include "Renderer/IRenderContext.h"
#include <vector>
#include <string>

// ==========================================================================
// MeshAsset  –  几何体资产
//
// 以平铺数组形式存储顶点属性，支持可选索引缓冲区。
// GpuBuffer 句柄在上传 GPU 后填充，用于渲染。
// ==========================================================================

// --------------------------------------------------------------------------
// 顶点格式（对齐到 4 字节）
// --------------------------------------------------------------------------
struct MeshVertex {
    Vec3  position;          // POSITION
    Vec3  normal   = Vec3::Up();
    Vec3  tangent  = Vec3::Right();
    float u = 0.0f, v = 0.0f;  // TEXCOORD0
    float u2= 0.0f, v2= 0.0f;  // TEXCOORD1 (lightmap / second UV)
};

// --------------------------------------------------------------------------
// SubMesh  –  一个 draw call 的范围 + 材质槽索引
// --------------------------------------------------------------------------
struct SubMesh {
    uint32_t indexOffset  = 0;  // 起始索引（或顶点，若无索引缓冲）
    uint32_t indexCount   = 0;
    uint32_t vertexOffset = 0;
    int      materialSlot = 0;  // 对应 ModelAsset::m_Materials[i]
    std::string name;
};

// --------------------------------------------------------------------------
// AABB
// --------------------------------------------------------------------------
struct AABB {
    Vec3 min = Vec3::Zero();
    Vec3 max = Vec3::Zero();

    Vec3   Center()  const { return (min + max) * 0.5f; }
    Vec3   Extents() const { return (max - min) * 0.5f; }
    float  Radius()  const { return Extents().Length(); }

    void Expand(const Vec3& p) {
        if (p.x < min.x) min.x = p.x; if (p.x > max.x) max.x = p.x;
        if (p.y < min.y) min.y = p.y; if (p.y > max.y) max.y = p.y;
        if (p.z < min.z) min.z = p.z; if (p.z > max.z) max.z = p.z;
    }
};

// --------------------------------------------------------------------------
// MeshAsset
// --------------------------------------------------------------------------
class MeshAsset : public Asset {
public:
    explicit MeshAsset(const std::string& path)
        : Asset(AssetType::Mesh, path) {}

    // ---- 上传 CPU 数据 ----------------------------------------------------
    void SetGeometry(std::vector<MeshVertex>  vertices,
                     std::vector<uint32_t>    indices,
                     std::vector<SubMesh>     subMeshes)
    {
        m_Vertices  = std::move(vertices);
        m_Indices   = std::move(indices);
        m_SubMeshes = std::move(subMeshes);
        RebuildAABB();
        SetState(AssetState::Ready);
    }

    // ---- 访问器 -----------------------------------------------------------
    const std::vector<MeshVertex>& GetVertices()  const { return m_Vertices;  }
    const std::vector<uint32_t>&   GetIndices()   const { return m_Indices;   }
    const std::vector<SubMesh>&    GetSubMeshes() const { return m_SubMeshes; }
    const AABB&                    GetAABB()      const { return m_AABB;      }

    bool HasIndices()  const { return !m_Indices.empty(); }
    uint32_t VertexCount() const { return static_cast<uint32_t>(m_Vertices.size()); }
    uint32_t IndexCount()  const { return static_cast<uint32_t>(m_Indices.size()); }

    // ---- GPU 缓冲区（由渲染后端填写）--------------------------------------
    void SetVertexBuffer(std::shared_ptr<GpuBuffer> vb) { m_VB = std::move(vb); }
    void SetIndexBuffer (std::shared_ptr<GpuBuffer> ib) { m_IB = std::move(ib); }
    GpuBuffer* GetVertexBuffer() const { return m_VB.get(); }
    GpuBuffer* GetIndexBuffer()  const { return m_IB.get(); }
    bool IsUploaded() const { return m_VB != nullptr; }

    // ---- 工厂：内置基本体 ------------------------------------------------
    static std::shared_ptr<MeshAsset> CreateTriangle(const std::string& name = "Triangle");
    static std::shared_ptr<MeshAsset> CreateQuad    (const std::string& name = "Quad");
    static std::shared_ptr<MeshAsset> CreateCube    (const std::string& name = "Cube");

private:
    void RebuildAABB() {
        if (m_Vertices.empty()) return;
        m_AABB.min = m_AABB.max = m_Vertices[0].position;
        for (const auto& v : m_Vertices) m_AABB.Expand(v.position);
    }

    std::vector<MeshVertex> m_Vertices;
    std::vector<uint32_t>   m_Indices;
    std::vector<SubMesh>    m_SubMeshes;
    AABB                    m_AABB;

    std::shared_ptr<GpuBuffer> m_VB;
    std::shared_ptr<GpuBuffer> m_IB;
};

using MeshHandle = AssetHandle<MeshAsset>;
