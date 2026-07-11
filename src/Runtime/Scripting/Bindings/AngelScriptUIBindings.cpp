    engine.SetDefaultNamespace("Script");
    Check(engine.RegisterGlobalFunction("bool SetEnabled(ActorHandle, bool)",
        asFUNCTION(ScriptSetEnabled), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool IsEnabled(ActorHandle)",
        asFUNCTION(ScriptIsEnabled), asCALL_CDECL));

    engine.SetDefaultNamespace("UIElement");
    Check(engine.RegisterGlobalFunction("string GetId(ActorHandle)",
        asFUNCTION(UIElementGetId), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetId(ActorHandle, const string &in)",
        asFUNCTION(UIElementSetId), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetText(ActorHandle, const string &in)",
        asFUNCTION(UIElementSetText), asCALL_CDECL));


    engine.SetDefaultNamespace("UI");
    Check(engine.RegisterGlobalFunction("bool Subscribe(const string &in, const string &in, const string &in)",
        asFUNCTION(UISubscribe), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Unsubscribe(const string &in, const string &in)",
        asFUNCTION(UIUnsubscribe), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void ClearSubscriptions()",
        asFUNCTION(UIClearSubscriptions), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetBool(const string &in, const string &in, bool)",
        asFUNCTION(UISetBool), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetInt(const string &in, const string &in, int)",
        asFUNCTION(UISetInt), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetFloat(const string &in, const string &in, float)",
        asFUNCTION(UISetFloat), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetString(const string &in, const string &in, const string &in)",
        asFUNCTION(UISetString), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool GetBool(const string &in, const string &in)",
        asFUNCTION(UIGetBool), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("int GetInt(const string &in, const string &in)",
        asFUNCTION(UIGetInt), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("float GetFloat(const string &in, const string &in)",
        asFUNCTION(UIGetFloat), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetString(const string &in, const string &in)",
        asFUNCTION(UIGetString), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetVec2(const string &in, const string &in, const Vec2 &in)",
        asFUNCTION(UISetVec2), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetVec3(const string &in, const string &in, const Vec3 &in)",
        asFUNCTION(UISetVec3), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetJson(const string &in, const string &in, const string &in)",
        asFUNCTION(UISetJson), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec2 GetVec2(const string &in, const string &in)",
        asFUNCTION(UIGetVec2), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetVec3(const string &in, const string &in)",
        asFUNCTION(UIGetVec3), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetJson(const string &in, const string &in)",
        asFUNCTION(UIGetJson), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Notify(const string &in, const string &in)",
        asFUNCTION(UINotify), asCALL_CDECL));

    engine.SetDefaultNamespace("Events");
    Check(engine.RegisterGlobalFunction("bool Subscribe(const string &in, const string &in)",
        asFUNCTION(EventsSubscribe), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Emit(const string &in, const string &in = \"{}\")",
        asFUNCTION(EventsEmit), asCALL_CDECL));

    engine.SetDefaultNamespace("Timer");
    Check(engine.RegisterGlobalFunction("uint64 After(float, const string &in)",
        asFUNCTION(TimerAfter), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("uint64 Every(float, const string &in)",
        asFUNCTION(TimerEvery), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Cancel(uint64)",
        asFUNCTION(TimerCancel), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void CancelAll()",
        asFUNCTION(TimerCancelAll), asCALL_CDECL));

    engine.SetDefaultNamespace("Task");
    Check(engine.RegisterGlobalFunction("uint64 Delay(float, const string &in)",
        asFUNCTION(TaskDelay), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Cancel(uint64)",
        asFUNCTION(TaskCancel), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void CancelAll()",
        asFUNCTION(TaskCancelAll), asCALL_CDECL));

