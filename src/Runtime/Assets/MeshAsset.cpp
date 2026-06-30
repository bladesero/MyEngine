#include "Assets/MeshAsset.h"
#include <algorithm>
#include <cmath>

void MeshAsset::SetSdfVoxelPath(std::filesystem::path path)
{
    m_SdfVoxelPath = std::move(path);
    m_SdfVoxelData.reset();
}

bool MeshAsset::LoadSdfVoxelData(std::string* error)
{
    if (m_SdfVoxelData) return true;
    if (m_SdfVoxelPath.empty()) {
        if (error) *error = "SDF/voxel sidecar path is empty";
        return false;
    }
    auto data = std::make_unique<MeshSdfVoxelData>();
    if (!MeshSdfVoxelXml::Load(m_SdfVoxelPath, *data, error)) {
        return false;
    }
    m_SdfVoxelData = std::move(data);
    return true;
}

void MeshAsset::RebuildSubMeshBounds()
{
    for (SubMesh& subMesh : m_SubMeshes) {
        bool initialized = false;
        auto expandVertex = [&](uint32_t vertexIndex) {
            if (vertexIndex >= m_Vertices.size()) return;
            const Vec3& position = m_Vertices[vertexIndex].position;
            if (!initialized) {
                subMesh.bounds.min = subMesh.bounds.max = position;
                initialized = true;
            } else {
                subMesh.bounds.Expand(position);
            }
        };

        if (!m_Indices.empty()) {
            const uint32_t end = (std::min)(
                subMesh.indexOffset + subMesh.indexCount,
                static_cast<uint32_t>(m_Indices.size()));
            for (uint32_t index = subMesh.indexOffset; index < end; ++index) {
                expandVertex(m_Indices[index]);
            }
        } else {
            const uint32_t end = (std::min)(
                subMesh.vertexOffset + subMesh.indexCount,
                static_cast<uint32_t>(m_Vertices.size()));
            for (uint32_t vertex = subMesh.vertexOffset; vertex < end; ++vertex) {
                expandVertex(vertex);
            }
        }

        if (!initialized) {
            subMesh.bounds = m_AABB;
        }
    }
}

void MeshAsset::RebuildDerivedData()
{
    m_Lods.clear();
    if (!m_Indices.empty()) {
        m_Lods.push_back({ m_Indices, 1.0f });
        const size_t triangleCount = m_Indices.size() / 3;
        for (size_t stride : { size_t{2}, size_t{4} }) {
            if (triangleCount <= 1) break;
            MeshLod lod;
            lod.screenCoverage = 1.0f / static_cast<float>(stride);
            lod.indices.reserve(((triangleCount + stride - 1) / stride) * 3);
            for (size_t triangle = 0; triangle < triangleCount; triangle += stride) {
                const size_t firstIndex = triangle * 3;
                lod.indices.insert(lod.indices.end(), {
                    m_Indices[firstIndex],
                    m_Indices[firstIndex + 1],
                    m_Indices[firstIndex + 2]
                });
            }
            if (lod.indices.size() < m_Lods.back().indices.size()) {
                m_Lods.push_back(std::move(lod));
            }
        }
    }

    m_ColliderData = {};
    m_ColliderData.bounds = m_AABB;
    if (m_Vertices.empty()) return;
    const Vec3& lo = m_AABB.min;
    const Vec3& hi = m_AABB.max;
    m_ColliderData.vertices = {
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z},
        {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
        {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z},
        {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
    };
    m_ColliderData.indices = {
        0,2,1, 0,3,2, 4,5,6, 4,6,7,
        0,1,5, 0,5,4, 3,7,6, 3,6,2,
        1,2,6, 1,6,5, 0,4,7, 0,7,3,
    };
}

// --------------------------------------------------------------------------
// Helper: push a quad (2 triangles) into index buffer
// --------------------------------------------------------------------------
static void PushQuadIndices(std::vector<uint32_t>& idx,
                             uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    // a-b-c, a-c-d
    idx.push_back(a); idx.push_back(b); idx.push_back(c);
    idx.push_back(a); idx.push_back(c); idx.push_back(d);
}

// --------------------------------------------------------------------------
// Triangle
// --------------------------------------------------------------------------
std::shared_ptr<MeshAsset> MeshAsset::CreateTriangle(const std::string& name)
{
    auto mesh = std::make_shared<MeshAsset>("__builtin__/" + name);
    mesh->SetName(name);

    std::vector<MeshVertex> verts = {
        { { 0.0f,  0.5f, 0.0f}, Vec3::Forward(), Vec3::Right(), 0.5f, 0.0f },
        { { 0.5f, -0.5f, 0.0f}, Vec3::Forward(), Vec3::Right(), 1.0f, 1.0f },
        { {-0.5f, -0.5f, 0.0f}, Vec3::Forward(), Vec3::Right(), 0.0f, 1.0f },
    };

    SubMesh sm;
    sm.name        = name;
    sm.indexOffset = 0;
    sm.indexCount  = 3;

    mesh->SetGeometry(verts, {0,1,2}, {sm});
    return mesh;
}

// --------------------------------------------------------------------------
// Quad (XY plane, facing +Z)
// --------------------------------------------------------------------------
std::shared_ptr<MeshAsset> MeshAsset::CreateQuad(const std::string& name)
{
    auto mesh = std::make_shared<MeshAsset>("__builtin__/" + name);
    mesh->SetName(name);

    Vec3 n = Vec3{0,0,1};
    Vec3 t = Vec3::Right();

    std::vector<MeshVertex> verts = {
        { {-0.5f,  0.5f, 0.0f}, n, t, 0.f, 0.f },
        { { 0.5f,  0.5f, 0.0f}, n, t, 1.f, 0.f },
        { { 0.5f, -0.5f, 0.0f}, n, t, 1.f, 1.f },
        { {-0.5f, -0.5f, 0.0f}, n, t, 0.f, 1.f },
    };

    std::vector<uint32_t> idx;
    PushQuadIndices(idx, 0,1,2,3);

    SubMesh sm; sm.name = name; sm.indexOffset = 0; sm.indexCount = 6;
    mesh->SetGeometry(verts, idx, {sm});
    return mesh;
}

// --------------------------------------------------------------------------
// Cube (unit cube centred at origin, 6 faces × 4 verts each)
// --------------------------------------------------------------------------
std::shared_ptr<MeshAsset> MeshAsset::CreateCube(const std::string& name)
{
    auto mesh = std::make_shared<MeshAsset>("__builtin__/" + name);
    mesh->SetName(name);

    std::vector<MeshVertex> verts;
    std::vector<uint32_t>   idx;
    verts.reserve(24); idx.reserve(36);

    // Lambda to add one face: normal, tangent, 4 corners (CCW, normals outward)
    auto addFace = [&](Vec3 n, Vec3 t, Vec3 c0, Vec3 c1, Vec3 c2, Vec3 c3) {
        uint32_t base = static_cast<uint32_t>(verts.size());
        const float uvs[4][2] = {{0,1},{1,1},{1,0},{0,0}};
        Vec3 corners[4] = {c0, c1, c2, c3};
        for (int i = 0; i < 4; ++i) {
            MeshVertex mv;
            mv.position = corners[i];
            mv.normal   = n;
            mv.tangent  = t;
            mv.u = uvs[i][0]; mv.v = uvs[i][1];
            verts.push_back(mv);
        }
        PushQuadIndices(idx, base, base+1, base+2, base+3);
    };

    // +Z front
    addFace({0,0,1},{1,0,0}, {-0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f});
    // -Z back
    addFace({0,0,-1},{-1,0,0},{ 0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f});
    // +X right
    addFace({1,0,0},{0,0,-1}, { 0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f});
    // -X left
    addFace({-1,0,0},{0,0,1}, {-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f});
    // +Y top
    addFace({0,1,0},{1,0,0},  {-0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f,-0.5f},{-0.5f, 0.5f,-0.5f});
    // -Y bottom
    addFace({0,-1,0},{1,0,0}, {-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},{-0.5f,-0.5f, 0.5f});

    SubMesh sm; sm.name = name; sm.indexOffset = 0;
    sm.indexCount = static_cast<uint32_t>(idx.size());
    mesh->SetGeometry(verts, idx, {sm});
    return mesh;
}
