class Pickup {
  int value = 1;
  float radius = 1.5f;

  void Start() {
    Tags::Set("pickup");
    Scene::SetLayer(Scene::GetSelf(), 6);
  }

  void Update(float dt) {
    ActorHandle player = Scene::FindByTag("player");
    if (!player.IsValid()) return;
    if (Scene::GetDistance(Scene::GetSelf(), player) <= radius) {
      Events::Emit("pickup.collected", "{\"value\":" + value + "}");
      Scene::DestroyDeferred(Scene::GetSelf());
    }
  }
}
