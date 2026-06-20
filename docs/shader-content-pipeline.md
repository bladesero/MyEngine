# Shader Content Pipeline

Shaders are assets. Engine descriptors and sources live in `EngineContent/Shaders` and are addressed as `Content/Engine/Shaders/*.shader`. Project shaders live in `Content/Shaders`; `Content/Engine` is reserved.

A `.shader` source asset is JSON version 1 with either `vertex` plus `pixel` stages, or one `compute` stage. Each source path is relative to the descriptor and absolute paths or `..` are rejected. `defines` accepts `NAME` and `NAME=VALUE` strings.

In Editor builds, `ShaderManager` compiles the referenced HLSL and retains the previous GPU object when hot reload fails. Its cache identity includes the ShaderAsset ID/version, active RHI backend, and vertex-layout values.

Windows publishing stages project and engine content, compiles every stage for D3D11 (`*_5_0`) and D3D12 (`*_5_1`), and writes a versioned binary container at the original `.shader` logical path. HLSL and HLSLI source files are not archived. A compile failure aborts staging before the previous package is replaced.

Player uses the existing verified PAK hash cache. Once extracted, AssetManager loads cooked `.shader` containers from the mounted project root. There is no source fallback for cooked assets.
