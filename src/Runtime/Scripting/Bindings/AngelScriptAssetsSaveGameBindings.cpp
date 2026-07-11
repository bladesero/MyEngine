    engine.SetDefaultNamespace("Scenes");
    Check(engine.RegisterGlobalFunction("bool Load(const string &in)",asFUNCTION(ScenesLoad),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("int GetLoadState()",asFUNCTION(ScenesGetLoadState),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetLastError()",asFUNCTION(ScenesGetLastError),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool SetPersistentJson(const string &in, const string &in)",asFUNCTION(ScenesSetPersistentJson),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetPersistentJson(const string &in)",asFUNCTION(ScenesGetPersistentJson),asCALL_CDECL));
    engine.SetDefaultNamespace("SaveGame");
    Check(engine.RegisterGlobalFunction("bool Write(const string &in, const string &in, const string &in, const string &in, const string &in)",asFUNCTION(SaveGameWrite),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string ReadJson(const string &in)",asFUNCTION(SaveGameReadJson),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Exists(const string &in)",asFUNCTION(SaveGameExists),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Remove(const string &in)",asFUNCTION(SaveGameRemove),asCALL_CDECL));

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


