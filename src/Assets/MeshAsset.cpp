#include "Assets/MeshAsset.h"
#include <cmath>

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
