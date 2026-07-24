#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Assets/Asset.h"
#include "Core/EngineMath.h"
#include "Renderer/RHI/IRenderContext.h"
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
    Vec3 position; // POSITION
    Vec3 normal = Vec3::Up();
    Vec3 tangent = Vec3::Right();
    float u = 0.0f, v = 0.0f;   // TEXCOORD0
    float u2 = 0.0f, v2 = 0.0f; // TEXCOORD1 (lightmap / second UV)
    Vec4 color = Vec4::One();   // COLOR0
    float boneIndices[4] = {0, 0, 0, 0};
    float boneWeights[4] = {1, 0, 0, 0};
};

// --------------------------------------------------------------------------
// SubMesh  –  一个 draw call 的范围 + 材质槽索引
// --------------------------------------------------------------------------
struct SubMesh {
    uint32_t indexOffset = 0; // 起始索引（或顶点，若无索引缓冲）
    uint32_t indexCount = 0;
    uint32_t vertexOffset = 0;
    int materialSlot = 0; // 对应 ModelAsset::m_Materials[i]
    std::string name;
    AABB bounds;
};

struct MeshLod {
    std::vector<uint32_t> indices;
    float screenCoverage = 1.0f;
};

struct MeshColliderData {
    AABB bounds;
    std::vector<Vec3> vertices;
    std::vector<uint32_t> indices;
};

// --------------------------------------------------------------------------
// MeshAsset
// --------------------------------------------------------------------------
class MYENGINE_RUNTIME_API MeshAsset : public Asset {
public:
    explicit MeshAsset(const std::string& path) : Asset(AssetType::Mesh, path) {}

    // ---- 上传 CPU 数据 ----------------------------------------------------
    void SetGeometry(std::vector<MeshVertex> vertices, std::vector<uint32_t> indices, std::vector<SubMesh> subMeshes) {
        m_Vertices = std::move(vertices);
        m_Indices = std::move(indices);
        m_SubMeshes = std::move(subMeshes);
        RebuildAABB();
        RebuildSubMeshBounds();
        RebuildDerivedData();
        InvalidateGpuBuffers();
        SetState(AssetState::Ready);
    }

    // ---- 访问器 -----------------------------------------------------------
    const std::vector<MeshVertex>& GetVertices() const { return m_Vertices; }
    const std::vector<uint32_t>& GetIndices() const { return m_Indices; }
    const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }
    const AABB& GetAABB() const { return m_AABB; }
    const std::vector<MeshLod>& GetLods() const { return m_Lods; }
    const MeshColliderData& GetColliderData() const { return m_ColliderData; }
    const MeshLod& GetLod(size_t level) const;

    bool HasIndices() const { return !m_Indices.empty(); }
    uint32_t VertexCount() const { return static_cast<uint32_t>(m_Vertices.size()); }
    uint32_t IndexCount() const { return static_cast<uint32_t>(m_Indices.size()); }

    // ---- GPU 缓冲区（由渲染后端填写）--------------------------------------
    void SetVertexBuffer(std::shared_ptr<GpuBuffer> vb) { m_VB = std::move(vb); }
    void SetIndexBuffer(std::shared_ptr<GpuBuffer> ib) { m_IB = std::move(ib); }
    GpuBuffer* GetVertexBuffer() const { return m_VB.get(); }
    GpuBuffer* GetIndexBuffer() const { return m_IB.get(); }
    bool IsUploaded() const { return m_VB != nullptr; }
    uint64_t GetGpuBufferBytes() const { return (m_VB ? m_VB->desc.size : 0) + (m_IB ? m_IB->desc.size : 0); }
    bool HasExternalGpuBufferReferences() const {
        return (m_VB && m_VB.use_count() > 1) || (m_IB && m_IB.use_count() > 1);
    }
    void InvalidateGpuBuffers() {
        m_VB.reset();
        m_IB.reset();
    }

    bool ReloadFrom(const Asset& source) override {
        const auto* mesh = dynamic_cast<const MeshAsset*>(&source);
        if (!mesh)
            return false;
        SetGeometry(mesh->m_Vertices, mesh->m_Indices, mesh->m_SubMeshes);
        return true;
    }

    // ---- 工厂：内置基本体 ------------------------------------------------
    static std::shared_ptr<MeshAsset> CreateTriangle(const std::string& name = "Triangle");
    static std::shared_ptr<MeshAsset> CreateQuad(const std::string& name = "Quad");
    static std::shared_ptr<MeshAsset> CreateCube(const std::string& name = "Cube");

private:
    void RebuildAABB() {
        if (m_Vertices.empty()) {
            m_AABB = {};
            return;
        }
        m_AABB.min = m_AABB.max = m_Vertices[0].position;
        for (const auto& v : m_Vertices)
            m_AABB.Expand(v.position);
    }
    void RebuildDerivedData();
    void RebuildSubMeshBounds();

    std::vector<MeshVertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<SubMesh> m_SubMeshes;
    AABB m_AABB;
    std::vector<MeshLod> m_Lods;
    MeshColliderData m_ColliderData;

    std::shared_ptr<GpuBuffer> m_VB;
    std::shared_ptr<GpuBuffer> m_IB;
};

using MeshHandle = AssetHandle<MeshAsset>;
