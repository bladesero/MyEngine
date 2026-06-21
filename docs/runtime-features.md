# Minimal Runtime Features

The default Editor and Player scene demonstrates the complete runtime slice:

- AngelScript gameplay components with class-based lifecycle callbacks
- Lua editor scripts for tooling and automation only
- fixed-step rigid bodies and box colliders
- skeletons, keyframe animation clips, and CPU skinning
- metallic-roughness Cook-Torrance PBR with directional shadows

## Script Syntax

Runtime gameplay scripts are AngelScript classes attached through the `Script`
component. New scripts default to the `Script` class and can implement any of
the lifecycle callbacks the engine calls.

```cpp
class Script {
  void Start() {
    Actor::SetPosition(Vec3(0, 1, 0));
    RigidBody::SetVelocity(Vec3(0, 0, 0));
  }

  void Update(float dt) {
    Actor::Translate(Vec3(1 * dt, 0, 0));
    Actor::Rotate(Vec3(0, 35 * dt, 0));
  }
}
```

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
