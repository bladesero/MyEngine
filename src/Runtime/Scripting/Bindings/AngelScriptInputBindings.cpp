engine.SetDefaultNamespace("Input");
Check(engine.RegisterGlobalFunction("bool ActionDown(const string &in)", asFUNCTION(InputActionDown), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool ActionPressed(const string &in)", asFUNCTION(InputActionPressed),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool ActionReleased(const string &in)", asFUNCTION(InputActionReleased),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("float Axis(const string &in)", asFUNCTION(InputAxis), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("Vec2 Axis2(const string &in)", asFUNCTION(InputAxis2), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool KeyDown(int)", asFUNCTION(InputKeyDown), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool KeyPressed(int)", asFUNCTION(InputKeyPressed), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool KeyReleased(int)", asFUNCTION(InputKeyReleased), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool MouseDown(int)", asFUNCTION(InputMouseDown), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool MousePressed(int)", asFUNCTION(InputMousePressed), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool MouseReleased(int)", asFUNCTION(InputMouseReleased), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("Vec2 MousePosition()", asFUNCTION(InputMousePosition), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("Vec2 MouseDelta()", asFUNCTION(InputMouseDelta), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("int GamepadCount()", asFUNCTION(InputGamepadCount), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("int PrimaryGamepadId()", asFUNCTION(InputPrimaryGamepadId), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool GamepadConnected(int)", asFUNCTION(InputGamepadConnected), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool GamepadButtonDown(int, int)", asFUNCTION(InputGamepadButtonDown),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool GamepadButtonPressed(int, int)", asFUNCTION(InputGamepadButtonPressed),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool GamepadButtonReleased(int, int)", asFUNCTION(InputGamepadButtonReleased),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("float GamepadAxis(int, int)", asFUNCTION(InputGamepadAxis), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("string GlyphSet()", asFUNCTION(InputGlyphSet), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("string GlyphFamily()", asFUNCTION(InputGlyphFamily), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("string ActionGlyphJson(const string &in)", asFUNCTION(InputActionGlyphJson),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("string SourceGlyphJson(const string &in)", asFUNCTION(InputSourceGlyphJson),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetGlyphLocale(const string &in)", asFUNCTION(InputSetGlyphLocale),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool Vibrate(int, float, float, uint)", asFUNCTION(InputVibrate), asCALL_CDECL));
Check(engine.RegisterGlobalFunction(
    "string BindingConflictsJson(const string &in, uint, const string &in, const string &in)",
    asFUNCTION(InputBindingConflictsJson), asCALL_CDECL));
Check(engine.RegisterGlobalFunction(
    "bool Rebind(const string &in, uint, const string &in, const string &in, bool = false)", asFUNCTION(InputRebind),
    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool SaveBindings()", asFUNCTION(InputSaveBindings), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool ResetBindings()", asFUNCTION(InputResetBindings), asCALL_CDECL));
