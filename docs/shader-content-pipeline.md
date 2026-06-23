# Shader Content Pipeline

Shaders are assets. Engine descriptors and sources live in `EngineContent/Shaders` and are addressed as `Content/Engine/Shaders/*.shader`. Project shaders live in `Content/Shaders`; `Content/Engine` is reserved.

A `.shader` source asset is JSON version 1 with either `vertex` plus `pixel` stages, or one `compute` stage. Each source path is relative to the descriptor and absolute paths or `..` are rejected. `defines` accepts `NAME` and `NAME=VALUE` strings.

In Editor builds, `ShaderManager` compiles the referenced HLSL and retains the previous GPU object when hot reload fails. Its cache identity includes the ShaderAsset ID/version, active RHI backend, and vertex-layout values.

Windows publishing stages project and engine content, compiles every stage for D3D11 (`*_5_0`) and D3D12 (`*_5_0`) through the platform D3DCompile/FXC path, and writes a versioned binary container at the original `.shader` logical path. Windows cooked packages require only the `d3d11,d3d12` backends; Slang is not part of the Windows D3D shader compile path. HLSL and HLSLI source files are not archived. A compile failure aborts staging before the previous package is replaced.

macOS Metal publishing is experimental and uses Slang to produce Metal shader payloads. A macOS package must declare only the `metal` backend in `CookManifest.requiredBackends`.

Linux currently has no repository GPU backend. Platform macros may exist for future work, but there is no Linux Player shader/runtime target to cook.

Player uses the existing verified PAK hash cache. Once extracted, AssetManager loads cooked `.shader` containers from the mounted project root. There is no source fallback for cooked assets.
