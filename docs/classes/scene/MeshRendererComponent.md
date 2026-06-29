# MeshRendererComponent

## Role

Scene component that binds one mesh to one or more material slots for the renderer.

## Responsibilities

- Holds one `MeshHandle` and a material slot array (`std::vector<MaterialHandle>`).
- Keeps `GetMaterial()` / `SetMaterial()` as slot 0 compatibility APIs.
- Resolves `SubMesh::materialSlot` through `GetMaterialForSlot(slot)`, then falls back to slot 0 and the default material.
- Serializes new scenes with `materials: [...]` and still reads the legacy `material` field into slot 0.
- Keeps embedded legacy `materialData` behavior scoped to slot 0.
