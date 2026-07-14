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
  `FadeVolume`, `SetPitch`, `SetLoop`, `SetStreaming`, `SetBus`, `SetPriority`,
  `SetConcurrency`
- `AudioMixer::SetMasterVolume`, `GetMasterVolume`, `SetBusVolume`,
  `GetBusVolume`, `SetBusMuted`, `GetDiagnosticsJson`; stable bus names are
  `music`, `effects`, `voice`, and `ui`
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

`Game::Pause`, `Resume`, `IsPaused`, `SetTimeScale`, and `GetTimeScale` use the
shared GameFlow contract. `Game::ShowMainMenu` and `ShowGameOver` select the
standard runtime modal roots, while `Game::GetFlowState` returns the stable
state name. Nested Settings screens restore the previously focused action.

`WorldStreaming::RegisterDistance` and `RegisterPortal` add project-relative
additive scene fragments. `SetObserver` drives distance decisions;
`SetPortalOpen`, `Retry`, and `GetStatsJson` control and diagnose streaming.
Chunk actor IDs and parent relationships are local to the fragment; parent
references may not cross a zone boundary.

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

Runtime layout adaptation is available through
`UI::SetSafeArea(left, top, right, bottom)` using normalized viewport insets.
`UI::GetDiagnosticsJson()` reports effective scale, safe dimensions,
narrow-layout state, and fallback-font failures. Persisted scale, subtitle,
contrast, and color-vision values should be applied through
`UserSettings::WriteJson` rather than duplicated in gameplay scripts.

Runtime dialogue can use the bounded subtitle facade:

```angelscript
UI::ShowSubtitle("intro-guide", "Guide", "Stay close to the light.", 3.0f, 10);
string state = UI::GetSubtitleJson();
UI::ClearSubtitles();
```

Stable cue IDs update in place, higher priority cues preempt and later resume
lower priority cues, and the queue drops its lowest-priority tail after 32
waiting cues. Persisted subtitle enable/scale values affect the generated Rml
overlay immediately. `reduceCameraShake` attenuates live
`GameplayFeedback::Shake` output to 25 percent without changing gameplay timing.

`Resources::GetStatsJson()` exposes the most recent Player resource-budget
sample: CPU asset bytes and eviction/blocking totals, queued/peak upload bytes,
pending upload tasks, live/peak GPU resource bytes, logical Descriptor count,
live Actors, and active pressure flags. World streaming
diagnostics also include `actorBudgetBlockedFrames`, allowing gameplay debug UI
to distinguish slow I/O from an Actor-cap admission stall.

## Project-authored flow screens

Projects may override the standard MainMenu, Pause, Settings, and GameOver
visuals through `Content/Config/RuntimeScreens.ui.json`. Each entry supplies a
title, a `Content/UI/*.rml` document, and optional labels for existing stable
actions. The document must contain `runtime-root`, `runtime-title`, and one
`runtime-action-<stableActionId>` element for every action on that screen.

The configuration cannot replace action IDs or change modal/GameFlow behavior;
this keeps keyboard/gamepad focus restoration, pause ownership, settings
persistence, retry, and menu transitions deterministic. Missing, malformed, or
contract-incomplete project documents fall back to the generated engine screen.
`UI::GetDiagnosticsJson()` reports the active document and fallback count.

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
- `SaveGame`: versioned atomic slot saves with metadata and last-known-good
  backups; slot names cannot contain directories. Player stores them below the
  per-project SDL user preference directory, while tests/tools may override the
  storage root and development code retains the project `Saved/SaveGames`
  fallback. Bounded autosave rings use `autosave_N.json` slots without rename
  chains; validated checkpoint IDs map to `checkpoint_ID.json`. Scripts can
  list slots, query the latest autosave, and inspect or restore backups.
  `SaveData` remains available for unstructured tool data.
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
### Runtime input customization

`Input::BindingConflictsJson(action, bindingIndex, part, source)` reports uses
of a candidate source. `part` is `source` for Button/Axis1D and `x` or `y` for
Axis2D. `Input::Rebind(...)` rejects conflicts unless its final argument is
true; call `Input::SaveBindings()` to persist the runtime map in per-user
settings or `Input::ResetBindings()` to restore the project map. `Input::GlyphSet()`
returns `keyboardMouse` or `gamepad`, and `Input::Vibrate(...)` respects the
user vibration-strength preference.

`Input::GlyphFamily()` refines gamepad presentation to `xbox`, `playstation`,
or `nintendo` using SDL's connected-controller type. The versioned
`Content/Config/InputGlyphs.glyph.json` maps canonical binding sources to a
sprite in `InputGlyphAtlas.svg` and localized labels. Player selects the OS
locale, and activity switches prompts without changing bindings:

```angelscript
string jump = Input::ActionGlyphJson("Jump");
string confirm = Input::SourceGlyphJson("Gamepad/South");
Input::SetGlyphLocale("zh-CN");
```

Runtime flow screens use these mappings for Select/Back hints. Missing locale
entries fall back by language and then to the atlas default locale; unknown
sources return a structured `valid:false` descriptor instead of guessing.
