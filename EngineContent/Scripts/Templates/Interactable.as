#include "EngineContent/Scripts/Templates/BaseActorScript.as"

class Interactable : BaseActorScript {
  string prompt = "Interact";
  string eventName = "interact";
  float radius = 2.0f;

  void Update(float dt) {
    ActorHandle player = Scene::FindByName("Player");
    if (!player.IsValid()) return;

    ActorHandleArray@ nearby = Scene::FindInRadius(Actor::GetPosition(player), radius, 0xffffffff);
    for (uint i = 0; i < nearby.Length(); ++i) {
      if (nearby.At(i) == self) {
        UI::SetString("hud", "prompt", prompt);
        return;
      }
    }
  }

  void Interact() {
    Events::Emit(eventName, "{\"actor\":\"" + Scene::GetName(self) + "\"}");
  }
}
