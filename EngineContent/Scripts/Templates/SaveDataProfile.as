class SaveDataProfile {
  string path = "profile/state.json";
  int checkpoint = 0;

  void Start() {
    string json = SaveData::ReadJson(path);
    if (json != "{}") Debug::Log("save", "loaded " + path);
  }

  void SaveCheckpoint() {
    SaveData::WriteJson(path, "{\"checkpoint\":" + checkpoint + "}");
  }
}
