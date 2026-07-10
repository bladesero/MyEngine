class ThirdPersonAdventure {
  ActorHandle self;
  ActorHandle camera;
  ActorHandle nearby;
  int score = 0;
  bool hasKey = false;
  bool loadingAfterDeath = false;
  uint64 spawnTimer = 0;
  int spawnCount = 0;
  int comboIndex = 0;
  bool attacking = false;
  bool attackQueued = false;
  bool comboWindowOpen = false;
  int interactionInputCount = 0;
  string lastInteractionName = "";

  void Start() {
    self = Scene::GetSelf();
    camera = Scene::FindByName("GameplayCamera");
    if (camera.IsValid()) ThirdPersonCamera::SetTarget(camera, self);
    UI::SetBool("hud", "paused", false);
    UI::SetString("hud", "interactionPrompt", "");
    UI::Subscribe("pause-toggle", "click", "TogglePause");
    spawnTimer = Timer::Every(8.0f, "SpawnEnemy");
  }

  void Update(float dt) {
    Vec2 move = Input::Axis2("Move");
    CharacterController::Move(Vec3(move.x, 0, move.y) * 4.0f);
    if (Input::ActionPressed("Jump")) CharacterController::Jump();
    if (Input::ActionPressed("Attack")) {
      QueueAttack();
    }
    if (Input::ActionPressed("Pause")) TogglePauseState();
    if (Combat::IsDead(self) && !loadingAfterDeath) {
      loadingAfterDeath = Scenes::Load("Content/Scenes/AdventureSample.scene.json");
      return;
    }
    lastInteractionName = Interaction::GetNearestName(2.5f);
    UI::SetString("hud", "interactionPrompt", Interaction::GetNearestPrompt(2.5f));
    bool interactPressed = Input::ActionPressed("Interact");
    if (interactPressed) interactionInputCount += 1;
    if (lastInteractionName != "" && interactPressed) UseInteraction(lastInteractionName);
    UI::SetFloat("hud", "health", Combat::GetHealth(self));
    UI::SetFloat("hud", "maxHealth", Combat::GetMaxHealth(self));
    UI::SetInt("hud", "score", score);
    UI::SetInt("hud", "sceneLoadState", Scenes::GetLoadState());
  }

  void QueueAttack() {
    if (!attacking) {
      StartAttack(0);
      return;
    }
    attackQueued = true;
  }

  void StartAttack(int index) {
    comboIndex = index;
    attacking = true;
    attackQueued = false;
    comboWindowOpen = false;
    if (index == 0) Animator::SetTrigger(self, "Attack1");
    else if (index == 1) Animator::SetTrigger(self, "Attack2");
    else Animator::SetTrigger(self, "Attack3");
  }

  void FinishAttack() {
    Combat::EndAttack(self);
    if (attackQueued && comboIndex < 2) {
      StartAttack(comboIndex + 1);
      return;
    }
    attacking = false;
    attackQueued = false;
    comboWindowOpen = false;
    comboIndex = 0;
  }

  void UseInteraction(const string &in name) {
    if (name == "LockedDoor" && !hasKey) {
      UI::SetString("hud", "interactionPrompt", "A key is required");
      return;
    }
    string usedName = Interaction::UseNearest(2.5f);
    if (usedName == "") return;
    if (name == "AncientKey") {
      hasKey = true;
      score += 100;
      UI::SetBool("hud", "hasKey", true);
      Events::Emit("adventure.key.collected", "{}");
    } else if (name == "Checkpoint") {
      SaveCheckpoint("courtyard");
      UI::SetString("hud", "interactionPrompt", "Checkpoint saved");
    } else if (name == "LockedDoor") {
      score += 250;
    } else if (name == "LevelExit") {
      Scenes::Load("Content/Scenes/AdventureComplete.scene.json");
    }
  }

  void SpawnEnemy() {
    ActorHandle spawned = Scene::InstantiatePrefab("Content/Prefabs/AdventureEnemy.prefab.json",
      Vec3(7, 1, 2), Vec3(0, 180, 0));
    if (spawned.IsValid()) spawnCount += 1;
    UI::SetInt("hud", "spawned", spawnCount);
  }

  void TogglePause(const UIEvent &in event) {
    TogglePauseState();
  }

  void TogglePauseState() {
    if (Game::IsPaused()) Game::Resume(); else Game::Pause();
    UI::SetBool("hud", "paused", Game::IsPaused());
  }

  void SaveCheckpoint(const string &in checkpoint) {
    SaveGame::Write("profile.json", checkpoint,
      "{\"health\":100}", "[]", "{}");
  }

  void OnAnimationEvent(const string &in name, const string &in payload) {
    if (name == "Attack.HitStart") Combat::BeginAttack(self);
    else if (name == "Attack.HitEnd") Combat::EndAttack(self);
    else if (name == "Attack.ComboWindowOpen") comboWindowOpen = true;
    else if (name == "Attack.ComboWindowClose") comboWindowOpen = false;
    else if (name == "Attack.End") FinishAttack();
  }

  void OnDestroy() {
    Timer::Cancel(spawnTimer);
  }
}
