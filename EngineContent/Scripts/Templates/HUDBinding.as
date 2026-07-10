class HUDBinding {
  int score = 0;
  float health = 1.0f;

  void Start() {
    Events::Subscribe("pickup.collected", "OnPickup");
    Events::Subscribe("health.changed", "OnHealth");
    Publish();
  }

  void OnPickup(const string &in payload) {
    score += 1;
    Publish();
  }

  void OnHealth(const string &in payload) {
    health = 0.5f;
    Publish();
  }

  void Publish() {
    UI::SetInt("hud", "score", score);
    UI::SetFloat("hud", "health", health);
    UI::Notify("hud", "score");
  }
}
