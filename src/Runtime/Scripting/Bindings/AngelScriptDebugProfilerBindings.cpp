engine.SetDefaultNamespace("Debug");
Check(engine.RegisterGlobalFunction("void Log(const string &in)", asFUNCTION(DebugLog), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void Log(const string &in, const string &in)", asFUNCTION(DebugLogCategory),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void Warning(const string &in)", asFUNCTION(DebugWarning), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void Warning(const string &in, const string &in)",
                                    asFUNCTION(DebugWarningCategory), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void Error(const string &in)", asFUNCTION(DebugError), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void Error(const string &in, const string &in)", asFUNCTION(DebugErrorCategory),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void LogOnce(const string &in, const string &in)", asFUNCTION(DebugLogOnce),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void LogThrottle(const string &in, const string &in, float)",
                                    asFUNCTION(DebugLogThrottle), asCALL_CDECL));
Check(engine.RegisterGlobalFunction(
    "void DrawLine(const Vec3 &in, const Vec3 &in, const Vec3 &in, float duration = 0.0f)", asFUNCTION(DebugDrawLine),
    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void DrawSphere(const Vec3 &in, float, const Vec3 &in, float duration = 0.0f)",
                                    asFUNCTION(DebugDrawSphere), asCALL_CDECL));
Check(engine.RegisterGlobalFunction(
    "void DrawText(const Vec3 &in, const string &in, const Vec3 &in, float duration = 0.0f)", asFUNCTION(DebugDrawText),
    asCALL_CDECL));

engine.SetDefaultNamespace("Profiler");
Check(engine.RegisterGlobalFunction("string GetScriptStatsJson()", asFUNCTION(ProfilerGetScriptStatsJson),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void ResetScriptStats()", asFUNCTION(ProfilerResetScriptStats), asCALL_CDECL));

engine.SetDefaultNamespace("Resources");
Check(engine.RegisterGlobalFunction("string GetStatsJson()", asFUNCTION(ResourcesGetStatsJson), asCALL_CDECL));
