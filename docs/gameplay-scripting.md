# Gameplay AngelScript API v1/v2

Gameplay AngelScript runs through `ScriptComponent` in Runtime. It is separate
from the Editor AngelScript domain and is available in Player.

## Lifecycle

Script classes may implement these callbacks:

```angelscript
class Script {
  void Awake() {}
  void OnEnable() {}
  void Start() {}
  void FixedUpdate(float dt) {}
  void Update(float dt) {}
  void LateUpdate(float dt) {}
  void OnCollision(const CollisionEvent &in event) {}
  void OnDisable() {}
  void OnDestroy() {}
}
```

`CollisionEvent.phase` uses `1=Enter`, `2=Stay`, and `3=Exit`.

## Input

Use semantic project inputs through `Input::ActionDown`, `ActionPressed`,
`ActionReleased`, `Axis`, and `Axis2`. Raw input is also available through
keyboard, mouse, and gamepad wrappers:

```angelscript
bool jumping = Input::ActionPressed("Jump") || Input::KeyPressed(44);
Vec2 mouse = Input::MousePosition();
Vec2 delta = Input::MouseDelta();
float trigger = Input::GamepadAxis(Input::PrimaryGamepadId(), 5);
```

## Physics Queries

`Physics::Raycast` returns a `RaycastHit`. `Physics::OverlapSphere` returns a
read-only `UInt64Array` of actor handles.

```angelscript
RaycastHit hit = Physics::Raycast(Actor::GetWorldPosition(), Actor::GetForward(), 50.0f);
UInt64Array@ actors = Physics::OverlapSphere(Actor::GetWorldPosition(), 3.0f, 0xffffffff);
for (uint i = 0; i < actors.Length(); ++i) {
  uint64 actorHandle = actors.At(i);
}
```

## UI Events

Runtime UI events are subscribed by element id and event name. Actor-tree UI
element ids use the `ui_actor_<actorId>` format.

```angelscript
class Script {
  void Start() {
    UI::Subscribe("ui_actor_42", "click", "OnClick");
    UI::Subscribe("ui_actor_43", "change", "OnSlider");
  }

  void OnClick(const UIEvent &in event) {}
  void OnSlider(const UIEvent &in event) {
    if (event.hasValue) {
      float value = event.value;
    }
  }

  void OnDestroy() {
    UI::ClearSubscriptions();
  }
}
```

Subscriptions are owned by the `ScriptComponent` runtime and are cleared when
the component detaches, play ends, or the script recompiles.

## Actor Handles and Scene Queries

v2 adds `ActorHandle` as the stable script-side actor reference. It can be
checked, converted to/from `uint64`, and passed through Scene, Actor,
Components, Physics, and typed component facades.

```angelscript
ActorHandle self = Scene::GetSelf();
ActorHandle target = Scene::FindByName("Target");
if (Scene::IsValid(target)) {
  Actor::SetPosition(target, Actor::GetPosition(self) + Vec3(0, 1, 0));
}

ActorHandleArray@ matches = Scene::FindAllByName("Pickup");
for (uint i = 0; i < matches.Length(); ++i) {
  ActorHandle pickup = matches.At(i);
}
```

Scene mutation is queued by Runtime and flushed at scene safe points. Scripts do
not receive `Scene*`, `Actor*`, or component pointers.

Available Scene calls:

- `Scene::GetSelf`, `IsValid`, `FromUInt64`
- `Scene::FindByName`, `FindAllByName`
- `Scene::GetName`, `SetName`, `GetParent`, `GetChildren`
- `Scene::CreateActor`, `DestroyActor`, `SetActive`
- `Scene::InstantiatePrefab(path, position, rotation)`

`InstantiatePrefab` accepts project-relative content paths such as
`Content/Prefabs/Pickup.prefab.json`; absolute paths are rejected.

## Components, Assets, and Typed Facades

`Components::*` is the generic fallback for runtime component work:

```angelscript
ActorHandle actor = Scene::FindByName("Speaker");
if (!Components::Has(actor, "AudioSource")) {
  Components::Add(actor, "AudioSource");
}
Components::SetJson(actor, "AudioSource", "{\"volume\":0.5,\"loop\":true}");
```

Common components expose narrow typed facades:

- `AudioSource::Play`, `Stop`, `IsPlaying`, `SetClipPath`, `SetVolume`,
  `SetPitch`, `SetLoop`
- `Camera::SetMain`, `IsMain`, `SetFovY`, `GetFovY`
- `Light::SetIntensity`, `GetIntensity`, `SetColor`, `GetColor`
- `UIElement::GetId`, `SetId`, `SetText`

`Assets::*` exposes project-safe asset checks only:

```angelscript
if (Assets::Exists("Content/Audio/Click.wav") &&
    Assets::GetType("Content/Audio/Click.wav") == "AudioClip") {
  AudioSource::SetClipPath("Content/Audio/Click.wav");
}
```

## Script Events and Timers

`Events::*` is scene-local and owned by the subscribing `ScriptComponent`.
Callbacks receive the JSON payload as a string.

```angelscript
class Sender {
  void Start() {
    Events::Emit("score.added", "{\"amount\":10}");
  }
}

class Receiver {
  void Start() {
    Events::Subscribe("score.added", "OnScoreAdded");
  }

  void OnScoreAdded(const string &in payload) {}
}
```

Timers are driven by scene update and cleared with the script component:

```angelscript
uint64 timer = 0;

void Start() {
  Timer::After(1.0f, "SpawnOnce");
  timer = Timer::Every(0.25f, "Pulse");
}

void Pulse() {
  Timer::Cancel(timer);
}
```

## UI Data Models

Runtime scripts can update existing UI data models used by the UI system:

```angelscript
UI::SetInt("hud", "score", 42);
UI::SetFloat("hud", "health", 0.8f);
UI::SetString("hud", "label", "Ready");

int score = UI::GetInt("hud", "score");
```

Data model access stays inside the Runtime UI facade and does not expose Rml or
UI internals to scripts.

## Includes and Reuse

Script files can include shared project or engine script code before compile:

```angelscript
#include "Content/Scripts/Common/HealthComponent.as"

class Enemy : HealthComponent {
  void Start() {
    maxHealth = 50;
  }
}
```

Supported directives are `#include "..."` and `import "..."`. Paths must be
project-relative, engine script relative, or relative to the including file.
Absolute paths and `..` escapes are rejected. When an included file changes,
file-backed `ScriptComponent` instances hot reload and preserve the previous
working runtime if recompilation fails.

## v3 API Reference

| Namespace | Purpose | Failure behavior | Player |
| --- | --- | --- | --- |
| `Scene` | Handle queries, parent/layer/active mutation, prefab creation | Invalid handle, empty array, or no-op | Yes |
| `Actor` | Current actor and handle transform helpers | Default vectors or no-op | Yes |
| `Transform` | Current actor and handle transform get/set helpers | Default vectors or no-op | Yes |
| `Tags` | Current actor and handle tag helpers | Empty string, `false`, or no-op | Yes |
| `Components` | Generic component add/remove/JSON fallback | `false` or `{}` | Yes |
| `RigidBody` | Current actor and handle physics body controls | No-op/default values | Yes |
| `CharacterController` | Current actor and handle movement controls | No-op/default values | Yes |
| `Collider` | Trigger/layer/layerMask on actor colliders | No-op/default values | Yes |
| `MeshRenderer` | Material path get/set by slot | `false` or empty string | Yes |
| `Animator` | Minimal skinned animation playback state | No-op/default values | Yes |
| `Script` | Enable/disable a script component by handle | `false` | Yes |
| `Debug` | Log plus stable debug-draw entrypoints | No-op for draw when no backend is attached | Yes |
| `SaveData` | Project-safe JSON save files under `Saved/ScriptData` | `false` or `{}` | Yes |
| `Task` | `Delay` wrapper over runtime timers | `0` or no-op | Yes |
| `UI` | Events and bool/int/float/string/Vec2/Vec3/JSON data model values | Default values or no-op | Yes |
| `PrefabInstance` | Read-only prefab instance status/path/root helpers | `false`, empty string, or invalid handle | Yes |
| `AudioListener` | Enable or disable a runtime `AudioListenerComponent` by handle | `false` for invalid handle or missing component | Yes |
| `Particle` | Play, stop, query, and burst a runtime `ParticleSystemComponent` | `false` or no-op for invalid handle or missing component | Yes |
| `Profiler` | Read-only script callback statistics JSON | `"[]"` | Yes |

Common v3 calls:

```angelscript
ActorHandle enemy = Scene::FindByName("Enemy");
ActorHandleArray@ bodies = Scene::FindAllWithComponent("RigidBody");
ActorHandleArray@ nearby = Scene::FindInRadius(Actor::GetPosition(), 5.0f, 0xffffffff);

Scene::SetParent(enemy, Scene::GetSelf());
Scene::SetLayer(enemy, 4);
RigidBody::AddImpulse(enemy, Vec3(0, 3, 0));
Collider::SetTrigger(enemy, true);

SaveData::WriteJson("profile/state.json", "{\"checkpoint\":2}");
Task::Delay(0.5f, "OnReady");
UI::SetVec3("hud", "target", Actor::GetPosition(enemy));
UI::SetJson("hud", "items", "[\"key\",\"coin\"]");
```

## v4 Platform Hardening

The v4 additions focus on long-lived project scripts rather than raw API size:

- `ScriptAsset` records include dependencies, discovered classes, and compile
  diagnostics. File-backed `ScriptComponent` hot reload watches both the main
  script and included files.
- `Transform::*` and `Tags::*` are the preferred readable facade for transform
  and tag operations. Existing `Actor::*` transform calls remain compatible.
- `Scene::FindByTag`, `FindAllByTag`, `FindAllInLayer`,
  `FindNearestWithComponent`, `GetDistance`, and `DestroyDeferred` provide
  safe handle-only queries and mutation.
- `Profiler::GetScriptStatsJson()` returns per-callback call counts, exception
  counts, total milliseconds, and max milliseconds as JSON. Scripts can read
  it, but cannot mutate profiler state except `Profiler::ResetScriptStats()` in
  tests/tools.
- `Debug::Log(category, message)`, `LogOnce(key, message)`, and
  `LogThrottle(key, message, seconds)` are safe in Player. Debug draw calls are
  stable no-ops until a runtime debug-draw backend is attached.

Example:

```angelscript
class Pickup {
  void Start() {
    Tags::Set("pickup");
    Scene::SetLayer(Scene::GetSelf(), 6);
  }

  void Update(float dt) {
    ActorHandle player = Scene::FindByTag("player");
    if (!player.IsValid()) return;
    if (Scene::GetDistance(Scene::GetSelf(), player) < 1.5f) {
      Events::Emit("pickup.collected", "{\"value\":1}");
      Scene::DestroyDeferred(Scene::GetSelf());
    }
  }
}
```

## Action Adventure Runtime APIs

The action-adventure slice adds runtime-only gameplay namespaces:

- `Combat`: health, damage, healing, attack windows, and death queries.
- `Interaction`: nearest interactable query, prompt text, and controlled use.
  `GetNearestName`, `GetNearestPrompt`, and `UseNearest` avoid passing runtime
  objects through script; `UseNearest` also honors the component's
  `destroyOnUse` setting at the scene mutation safe point.
- `Particle`, `AudioListener`, and `Feedback`: reusable particle assets,
  listener selection, camera shake, hit flash, and temporary time scaling.
- `Navigation`, `Perception`, and `Enemy`: NavMesh path requests, dynamic
  blocked areas, sound events, sight checks, and the lightweight enemy FSM.
- `Scenes`: asynchronous project-relative scene file reads, main-thread scene
  construction, and persistent transition JSON. A failed request leaves the
  current scene alive.
- `SaveGame`: versioned atomic saves under `Saved/SaveGames`; slot names cannot
  contain directories. `SaveData` remains available for unstructured tool data.
- `Game`: pause/resume and time-scale control used by runtime menus.

`EngineContent/Scripts/Templates/ThirdPersonAdventure.as` demonstrates movement,
jumping, a three-step buffered combo, animation-event attack windows,
timer-driven prefab spawning, interaction prompts,
key/door flow, HUD DataModel updates, pause, checkpoint saves, death reload, and
scene transition. `Content/Scenes/AdventureSample.scene.json` is the runnable
sample scene and `Content/Scenes/AdventureComplete.scene.json` is its exit target.

The combo contract uses `Attack.HitStart`, `Attack.HitEnd`,
`Attack.ComboWindowOpen`, `Attack.ComboWindowClose`, and `Attack.End` animation
events. Enemy AI sight combines detection range, serialized field-of-view
degrees, and physics occlusion. Particle billboard vertices carry independently
interpolated RGBA lifetime color. The audio runtime selects the earliest active
primary listener deterministically, retains listener state in silent mode, and
uses linear attenuation between each source's minimum and maximum distance.
