class GameplayApiV2Example {
  ActorHandle spawned;
  uint64 pulseTimer = 0;
  int pulses = 0;

  void Start() {
    Events::Subscribe("pickup.collected", "OnPickupCollected");

    spawned = Scene::InstantiatePrefab(
      "Content/Prefabs/Pickup.prefab.json",
      Actor::GetPosition(Scene::GetSelf()) + Vec3(0, 0, 2),
      Vec3());

    if (!spawned.IsValid()) {
      spawned = Scene::CreateActor("Script Spawned Pickup");
      Components::Add(spawned, "AudioSource");
      AudioSource::SetVolume(spawned, 0.5f);
    }

    Timer::After(1.0f, "EmitReady");
    pulseTimer = Timer::Every(0.25f, "PulseHud");
  }

  void Update(float dt) {
    if (Scene::IsValid(spawned)) {
      Actor::Rotate(spawned, Vec3(0, 90.0f * dt, 0));
    }
  }

  void EmitReady() {
    Events::Emit("pickup.collected", "{\"source\":\"timer\"}");
  }

  void PulseHud() {
    pulses += 1;
    UI::SetInt("hud", "pulseCount", pulses);
    UI::SetString("hud", "status", "Gameplay API v2 active");

    if (pulses >= 4) {
      Timer::Cancel(pulseTimer);
    }
  }

  void OnPickupCollected(const string &in payload) {
    UI::SetBool("hud", "hasPickup", true);
    UI::SetString("hud", "lastPickupEvent", payload);

    if (Scene::IsValid(spawned) && Components::Has(spawned, "AudioSource")) {
      AudioSource::Play(spawned);
    }
  }
}
