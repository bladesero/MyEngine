class Projectile {
  Vec3 velocity = Vec3(0, 0, 15);
  float lifetime = 3.0f;
  uint64 lifeTimer = 0;

  void Start() {
    lifeTimer = Task::Delay(lifetime, "Expire");
  }

  void Update(float dt) {
    Transform::SetPosition(Transform::GetPosition() + velocity * dt);
  }

  void Expire() {
    Scene::DestroyDeferred(Scene::GetSelf());
  }

  void OnDestroy() {
    Task::Cancel(lifeTimer);
  }
}
