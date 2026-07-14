    engine.SetDefaultNamespace("Scenes");
    Check(engine.RegisterGlobalFunction("bool Load(const string &in)",asFUNCTION(ScenesLoad),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("int GetLoadState()",asFUNCTION(ScenesGetLoadState),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetLastError()",asFUNCTION(ScenesGetLastError),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool SetPersistentJson(const string &in, const string &in)",asFUNCTION(ScenesSetPersistentJson),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetPersistentJson(const string &in)",asFUNCTION(ScenesGetPersistentJson),asCALL_CDECL));
    engine.SetDefaultNamespace("WorldStreaming");
    Check(engine.RegisterGlobalFunction("bool RegisterDistance(const string &in, const string &in, const Vec3 &in, const Vec3 &in, float, float, int priority = 0)",asFUNCTION(WorldStreamingRegisterDistance),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool RegisterPortal(const string &in, const string &in, bool open = false, int priority = 0)",asFUNCTION(WorldStreamingRegisterPortal),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetObserver(const Vec3 &in)",asFUNCTION(WorldStreamingSetObserver),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool SetPortalOpen(const string &in, bool)",asFUNCTION(WorldStreamingSetPortalOpen),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Retry(const string &in)",asFUNCTION(WorldStreamingRetry),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetStatsJson()",asFUNCTION(WorldStreamingGetStatsJson),asCALL_CDECL));
    engine.SetDefaultNamespace("SaveGame");
    Check(engine.RegisterGlobalFunction("bool Write(const string &in, const string &in, const string &in, const string &in, const string &in)",asFUNCTION(SaveGameWrite),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string ReadJson(const string &in)",asFUNCTION(SaveGameReadJson),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string ReadBackupJson(const string &in)",asFUNCTION(SaveGameReadBackupJson),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string ListSlotsJson()",asFUNCTION(SaveGameListSlotsJson),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string WriteAutosave(const string &in, const string &in, const string &in, const string &in, uint)",asFUNCTION(SaveGameWriteAutosave),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string WriteCheckpoint(const string &in, const string &in, const string &in, const string &in)",asFUNCTION(SaveGameWriteCheckpoint),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string LatestAutosaveJson()",asFUNCTION(SaveGameLatestAutosaveJson),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool RestoreBackup(const string &in)",asFUNCTION(SaveGameRestoreBackup),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Exists(const string &in)",asFUNCTION(SaveGameExists),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Remove(const string &in)",asFUNCTION(SaveGameRemove),asCALL_CDECL));

    engine.SetDefaultNamespace("UserSettings");
    Check(engine.RegisterGlobalFunction("string ReadJson()",asFUNCTION(UserSettingsReadJson),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool WriteJson(const string &in)",asFUNCTION(UserSettingsWriteJson),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Reset()",asFUNCTION(UserSettingsReset),asCALL_CDECL));

    engine.SetDefaultNamespace("Assets");
    Check(engine.RegisterGlobalFunction("bool Exists(const string &in)",
        asFUNCTION(AssetsExists), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetType(const string &in)",
        asFUNCTION(AssetsGetType), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string ResolveProjectPath(const string &in)",
        asFUNCTION(AssetsResolveProjectPath), asCALL_CDECL));


    engine.SetDefaultNamespace("SaveData");
    Check(engine.RegisterGlobalFunction("bool Exists(const string &in)",
        asFUNCTION(SaveDataExists), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string ReadJson(const string &in)",
        asFUNCTION(SaveDataReadJson), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool WriteJson(const string &in, const string &in)",
        asFUNCTION(SaveDataWriteJson), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Delete(const string &in)",
        asFUNCTION(SaveDataDelete), asCALL_CDECL));


