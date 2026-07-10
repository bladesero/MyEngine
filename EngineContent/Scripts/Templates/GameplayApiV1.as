class Script {
  float speed = 4.0f;
  int uiClicks = 0;
  float sliderValue = 0.0f;

  void Start() {
    UI::Subscribe("ui_actor_42", "click", "OnButtonClicked");
    UI::Subscribe("ui_actor_43", "change", "OnSliderChanged");
  }

  void Update(float dt) {
    Vec2 move = Input::Axis2("Move");
    if (Input::KeyDown(26)) {
      move.y += 1.0f;
    }
    Actor::Translate(Vec3(move.x, 0.0f, move.y) * speed * dt);

    RaycastHit hit = Physics::Raycast(Actor::GetWorldPosition(), Actor::GetForward(), 20.0f);
    if (hit.hit) {
      UInt64Array@ nearby = Physics::OverlapSphere(hit.point, 2.0f);
      if (nearby.Length() > 0) {
        Actor::Rotate(Vec3(0.0f, 90.0f * dt, 0.0f));
      }
    }
  }

  void OnCollision(const CollisionEvent &in event) {
    if (event.phase == 1) {
      Actor::SetPosition(Actor::GetPosition() + event.normal * event.depth);
    }
  }

  void OnButtonClicked(const UIEvent &in event) {
    uiClicks += 1;
  }

  void OnSliderChanged(const UIEvent &in event) {
    if (event.hasValue) {
      sliderValue = event.value;
    }
  }

  void OnDestroy() {
    UI::ClearSubscriptions();
  }
}
