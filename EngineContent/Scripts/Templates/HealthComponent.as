#include "EngineContent/Scripts/Templates/BaseActorScript.as"

class HealthComponent : BaseActorScript {
  int maxHealth = 100;
  int health = 100;

  void Start() {
    BaseActorScript::Start();
    health = maxHealth;
    Events::Subscribe("damage", "OnDamage");
  }

  void OnDamage(const string &in payload) {
    int amount = 10;
    health -= amount;
    UI::SetInt("hud", "health", health);

    if (health <= 0) {
      Events::Emit("actor.dead", "{\"name\":\"" + Scene::GetName(self) + "\"}");
      Scene::SetActive(self, false);
    }
  }
}
