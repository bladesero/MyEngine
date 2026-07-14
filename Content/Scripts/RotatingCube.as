class RotatingCube {
  float speed = 35.0f;
  Vec3 axis = Vec3(0, 1, 0);

  void Update(float dt) {
    Actor::Rotate(axis * speed * dt);
  }
}
