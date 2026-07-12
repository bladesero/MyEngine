#pragma once

#include <cstdint>

// Persistent compatibility contract. Increment only together with a migration
// or an explicit rejection test for the preceding published version.
namespace FormatVersions {
inline constexpr int Project = 1;
inline constexpr int Scene = 1;
inline constexpr uint32_t Prefab = 1;
inline constexpr uint32_t AssetDatabase = 1;
inline constexpr int InputActionMap = 1;
inline constexpr int SaveGame = 2;
inline constexpr int CookManifest = 2;
inline constexpr int RuntimeDependencies = 1;
inline constexpr int ContentArchive = 2;
inline constexpr uint32_t ShaderDescription = 1;
inline constexpr uint32_t CookedShader = 3;
}

