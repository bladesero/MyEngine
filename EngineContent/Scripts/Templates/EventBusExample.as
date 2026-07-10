class EventBusExample {
  void Start() {
    Events::Subscribe("gameplay.ping", "OnPing");
    Events::Emit("gameplay.ping", "{\"source\":\"EventBusExample\"}");
  }

  void OnPing(const string &in payload) {
    Debug::LogOnce("eventbus.ping", "received gameplay.ping");
  }
}
