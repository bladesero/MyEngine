#include "Assets/AssetManager.h"

// --------------------------------------------------------------------------
// 内置纹理
// --------------------------------------------------------------------------

TextureHandle AssetManager::GetWhiteTexture()
{
    const std::string path = "__builtin__/White";
    if (IsLoaded(path)) return GetByPath<TextureAsset>(path);
    return Register(TextureAsset::CreateSolid("White", 255, 255, 255, 255));
}

TextureHandle AssetManager::GetBlackTexture()
{
    const std::string path = "__builtin__/Black";
    if (IsLoaded(path)) return GetByPath<TextureAsset>(path);
    return Register(TextureAsset::CreateSolid("Black", 0, 0, 0, 255));
}

TextureHandle AssetManager::GetNormalTexture()
{
    const std::string path = "__builtin__/FlatNormal";
    if (IsLoaded(path)) return GetByPath<TextureAsset>(path);
    // (128, 128, 255) = (0.5, 0.5, 1.0) in RGBA8 = flat +Z normal
    return Register(TextureAsset::CreateSolid("FlatNormal", 128, 128, 255, 255));
}

// --------------------------------------------------------------------------
// 内置网格
// --------------------------------------------------------------------------

MeshHandle AssetManager::GetTriangleMesh()
{
    const std::string path = "__builtin__/Triangle";
    if (IsLoaded(path)) return GetByPath<MeshAsset>(path);
    return Register(MeshAsset::CreateTriangle("Triangle"));
}

MeshHandle AssetManager::GetQuadMesh()
{
    const std::string path = "__builtin__/Quad";
    if (IsLoaded(path)) return GetByPath<MeshAsset>(path);
    return Register(MeshAsset::CreateQuad("Quad"));
}

MeshHandle AssetManager::GetCubeMesh()
{
    const std::string path = "__builtin__/Cube";
    if (IsLoaded(path)) return GetByPath<MeshAsset>(path);
    return Register(MeshAsset::CreateCube("Cube"));
}

// --------------------------------------------------------------------------
// 内置材质
// --------------------------------------------------------------------------

MaterialHandle AssetManager::GetDefaultMaterial()
{
    const std::string path = "__builtin__/Default";
    if (IsLoaded(path)) return GetByPath<MaterialAsset>(path);

    auto mat = MaterialAsset::CreateDefault("Default");
    // 绑定内置白色纹理到 BaseColorMap 槽
    mat->SetTexture("BaseColorMap", GetWhiteTexture());
    mat->SetTexture("NormalMap",    GetNormalTexture());
    return Register(std::move(mat));
}
