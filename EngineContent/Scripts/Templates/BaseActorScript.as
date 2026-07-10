class BaseActorScript {
  ActorHandle self;
  bool started = false;

  void Awake() {
    self = Scene::GetSelf();
  }

  void Start() {
    started = true;
  }

  void Log(const string &in message) {
    Debug::Log(Scene::GetName(self) + ": " + message);
  }
}
