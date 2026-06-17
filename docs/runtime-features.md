# Minimal Runtime Features

The default Editor and Player scene demonstrates the complete runtime slice:

- text scripts with `start` and `update` phases
- fixed-step rigid bodies and box colliders
- skeletons, keyframe animation clips, and CPU skinning
- metallic-roughness Cook-Torrance PBR with directional shadows

## Script Syntax

Scripts are compiled when attached or deserialized. Commands use three numeric
arguments:

```text
start:
set_position 0 1 0
set_velocity 0 0 0

update:
translate 1 0 0
rotate 0 35 0
```

`translate` and `rotate` values are rates per second. `set_velocity` targets a
`RigidBodyComponent` on the same actor.

## Runtime Update

`Scene::OnUpdate` updates actor components first, then advances `PhysicsWorld`
with a fixed 60 Hz step. Skinned meshes sample their current clip, generate a
bone palette, deform vertices on the CPU, and invalidate their GPU buffers for
the next main and shadow pass.
