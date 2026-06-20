# PhysicsWorld and Jolt backend

`PhysicsWorld` is the engine-facing physics service. Its implementation owns Jolt's
`PhysicsSystem`, job system, temporary allocator, filters, listeners, and BodyID maps.
Jolt types are not part of component, Scene, Lua, Editor, or serialized APIs.

## Fixed-step lifecycle

The Scene retains a 1/60 second fixed step and caps accumulated frame time at 0.25
seconds. Each step runs component `FixedUpdate`, reconciles active Actors and components,
pushes explicit commands and external Transform edits, updates Jolt, writes dynamic and
character world poses back as local Transforms, and finally dispatches queued collision
events on the main thread. Scene clear and destruction remove all bodies before Actors
or Jolt globals are released.

Actors are mapped by persistent Actor ID to Jolt BodyID. Components never own Jolt
pointers. Collider, rigid-body, enabled-state, scale, layer, mask, and shape changes cause
a safe body rebuild. A Collider without an enabled RigidBody is represented by a static
query body. Box, sphere, and capsule colliders map directly to their Jolt shapes; triggers
are sensors. CharacterController uses Jolt's rigid-body `Character` and requires a capsule.
A CharacterController and enabled RigidBody on the same Actor are rejected.

## Coordinates and hierarchy

MyEngine remains left-handed, Y-up, meters, row-vector matrices, with Euler angles in
degrees. Jolt is adapted centrally by reflecting Z for positions/vectors and reflecting
quaternion X/Y. Physics output is a world pose and is converted to Actor-local space using
the inverse parent matrix. Moving bodies below a non-uniformly scaled parent are rejected.

## Filtering, queries, and callbacks

The engine's 32-bit `layer` and `layerMask` values are stored in per-body ObjectLayer
records. Pair filtering applies the mutual rule: each body's mask must include the other
body's layer. Broad-phase layers separate static and moving bodies. Raycasts use Jolt
NarrowPhaseQuery and also reconcile edit-mode bodies before the first simulation step.

Jolt contact callbacks may execute on worker threads. They only enqueue Actor IDs and
contact data under a mutex. Enter, Stay, and Exit events are derived and dispatched after
`PhysicsSystem::Update` on the calling thread; components are never called by Jolt workers.

## Persistence and public operations

Serialized data includes BodyType, velocities, damping, friction, restitution, gravity,
axis locks, and collision-detection mode. Runtime BodyIDs, contacts, and sleep state are
not serialized. Missing fields use compatible defaults and old `static`/`dynamic` strings
remain accepted. `AddImpulse`, `AddAngularImpulse`, `Teleport`, and `SetKinematicTarget`
are one-shot commands; force and torque accumulators are consumed by the next fixed step.
Sleep state is read from Jolt rather than maintained by an engine timer.
