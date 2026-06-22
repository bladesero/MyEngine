# Minimal Runtime Features

The default Editor and Player scene demonstrates the complete runtime slice:

- AngelScript gameplay components with class-based lifecycle callbacks
- Lua editor scripts for tooling and automation only
- fixed-step rigid bodies and box colliders
- skeletons, keyframe animation clips, and CPU skinning
- metallic-roughness Cook-Torrance PBR with directional shadows

## Script Syntax

Runtime gameplay scripts are AngelScript classes stored as `.as` assets under
`Content/Scripts` and attached through the `Script` component. A script asset
can contain multiple classes; the editor lists each class as an addable script
component entry while the scene still serializes a single `Script` component.

```cpp
class RotatingCube {
  float speed = 35.0f;
  Vec3 axis = Vec3(0, 1, 0);

  void Start() {
    Actor::SetPosition(Vec3(0, 1, 0));
    RigidBody::SetVelocity(Vec3(0, 0, 0));
  }

  void Update(float dt) {
    Actor::Translate(Vec3(1 * dt, 0, 0));
    Actor::Rotate(axis * speed * dt);
  }
}
```

Public `bool`, integer, floating point, `string`, `Vec2`, and `Vec3` fields are
reflected into the `ScriptComponent` parameter UI and serialized under the
component's `properties` object. Unsupported fields remain available to the
script at runtime but are not shown in the inspector.

Bound runtime APIs are exposed through narrow namespaces such as `Actor`,
`RigidBody`, `Input`, and `Physics`. Runtime scripts do not receive raw engine,
renderer, editor, or physics backend pointers.

Lua scripts under `Content/Editor/Scripts` are editor automation tools. They
can run editor commands and manipulate the edit scene through `EditorContext`,
but they are not executed by the player runtime and are excluded from player
publishing by default.

## Runtime Update

`Scene::OnUpdate` updates actor components first, then advances `PhysicsWorld`
with a fixed 60 Hz step. Skinned meshes sample their current clip, generate a
bone palette, deform vertices on the CPU, and invalidate their GPU buffers for
the next main and shadow pass.
