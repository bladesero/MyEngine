#include "EngineContent/Scripts/Templates/BaseActorScript.as"

class Spawner : BaseActorScript {
  string prefabPath = "Content/Prefabs/Enemy.prefab.json";
  float interval = 3.0f;
  uint64 timer = 0;
  int spawned = 0;

  void Start() {
    BaseActorScript::Start();
    timer = Timer::Every(interval, "SpawnOne");
  }

  void SpawnOne() {
    Vec3 position = Actor::GetPosition(self) + Vec3(float(spawned), 0, 0);
    ActorHandle actor = Scene::InstantiatePrefab(prefabPath, position, Vec3());
    if (actor.IsValid()) {
      spawned += 1;
      UI::SetInt("hud", "spawned", spawned);
      Debug::Log("spawned " + Scene::GetName(actor));
    } else {
      Debug::Warning("failed to spawn prefab: " + prefabPath);
    }
  }

  void OnDestroy() {
    Timer::Cancel(timer);
  }
}
