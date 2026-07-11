    engine.SetDefaultNamespace("AudioSource");
    Check(engine.RegisterGlobalFunction("bool Play()",
        asFUNCTION(AudioSourcePlay), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Play(ActorHandle)",
        asFUNCTION(AudioSourcePlayHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Stop()",
        asFUNCTION(AudioSourceStop), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Stop(ActorHandle)",
        asFUNCTION(AudioSourceStopHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool IsPlaying()",
        asFUNCTION(AudioSourceIsPlaying), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool IsPlaying(ActorHandle)",
        asFUNCTION(AudioSourceIsPlayingHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetClipPath(const string &in)",
        asFUNCTION(AudioSourceSetClipPath), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetClipPath(ActorHandle, const string &in)",
        asFUNCTION(AudioSourceSetClipPathHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetVolume(float)",
        asFUNCTION(AudioSourceSetVolume), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetVolume(ActorHandle, float)",
        asFUNCTION(AudioSourceSetVolumeHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetPitch(float)",
        asFUNCTION(AudioSourceSetPitch), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetPitch(ActorHandle, float)",
        asFUNCTION(AudioSourceSetPitchHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetLoop(bool)",
        asFUNCTION(AudioSourceSetLoop), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetLoop(ActorHandle, bool)",
        asFUNCTION(AudioSourceSetLoopHandle), asCALL_CDECL));

    engine.SetDefaultNamespace("AudioListener");
    Check(engine.RegisterGlobalFunction("bool SetEnabled(ActorHandle, bool)",
        asFUNCTION(AudioListenerSetEnabled), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool IsEnabled(ActorHandle)",
        asFUNCTION(AudioListenerIsEnabled), asCALL_CDECL));

    engine.SetDefaultNamespace("Particle");
    Check(engine.RegisterGlobalFunction("bool Play(ActorHandle)",
        asFUNCTION(ParticlePlay), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Stop(ActorHandle)",
        asFUNCTION(ParticleStop), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool IsPlaying(ActorHandle)",
        asFUNCTION(ParticleIsPlaying), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Emit(ActorHandle, uint)",
        asFUNCTION(ParticleEmit), asCALL_CDECL));

    engine.SetDefaultNamespace("Camera");
    Check(engine.RegisterGlobalFunction("void SetMain(ActorHandle, bool)",
        asFUNCTION(CameraSetMain), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool IsMain(ActorHandle)",
        asFUNCTION(CameraIsMain), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetFovY(ActorHandle, float)",
        asFUNCTION(CameraSetFovY), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("float GetFovY(ActorHandle)",
        asFUNCTION(CameraGetFovY), asCALL_CDECL));

    engine.SetDefaultNamespace("Light");
    Check(engine.RegisterGlobalFunction("void SetIntensity(ActorHandle, float)",
        asFUNCTION(LightSetIntensity), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("float GetIntensity(ActorHandle)",
        asFUNCTION(LightGetIntensity), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetColor(ActorHandle, const Vec3 &in)",
        asFUNCTION(LightSetColor), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetColor(ActorHandle)",
        asFUNCTION(LightGetColor), asCALL_CDECL));

    engine.SetDefaultNamespace("MeshRenderer");
    Check(engine.RegisterGlobalFunction("string GetMaterialPath(ActorHandle, int slot = 0)",
        asFUNCTION(MeshRendererGetMaterialPath), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool SetMaterialPath(ActorHandle, int, const string &in)",
        asFUNCTION(MeshRendererSetMaterialPath), asCALL_CDECL));

    engine.SetDefaultNamespace("Animator");
    Check(engine.RegisterGlobalFunction("void SetPlaying(ActorHandle, bool)",
        asFUNCTION(AnimatorSetPlaying), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool IsPlaying(ActorHandle)",
        asFUNCTION(AnimatorIsPlaying), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("float GetTime(ActorHandle)",
        asFUNCTION(AnimatorGetTime), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetTime(ActorHandle, float)",
        asFUNCTION(AnimatorSetTime), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetBlendWeight(ActorHandle, float)",
        asFUNCTION(AnimatorSetBlendWeight), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("float GetBlendWeight(ActorHandle)",
        asFUNCTION(AnimatorGetBlendWeight), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Play(ActorHandle, const string &in, float transition = 0.0f)",
        asFUNCTION(AnimatorPlay), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetFloat(ActorHandle, const string &in, float)",
        asFUNCTION(AnimatorSetFloat), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("float GetFloat(ActorHandle, const string &in)",
        asFUNCTION(AnimatorGetFloat), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetBool(ActorHandle, const string &in, bool)",
        asFUNCTION(AnimatorSetBool), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool GetBool(ActorHandle, const string &in)",
        asFUNCTION(AnimatorGetBool), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetTrigger(ActorHandle, const string &in)",
        asFUNCTION(AnimatorSetTrigger), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetCurrentState(ActorHandle)",
        asFUNCTION(AnimatorGetCurrentState), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("float GetNormalizedTime(ActorHandle)",
        asFUNCTION(AnimatorGetNormalizedTime), asCALL_CDECL));

    engine.SetDefaultNamespace("ThirdPersonCamera");
    Check(engine.RegisterGlobalFunction("bool SetTarget(ActorHandle, ActorHandle)",
        asFUNCTION(ThirdPersonCameraSetTarget), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void AddOrbit(ActorHandle, float, float)",
        asFUNCTION(ThirdPersonCameraAddOrbit), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetDistance(ActorHandle, float)",
        asFUNCTION(ThirdPersonCameraSetDistance), asCALL_CDECL));

    engine.SetDefaultNamespace("Combat");
    Check(engine.RegisterGlobalFunction("bool Damage(ActorHandle, float, ActorHandle source = ActorHandle())", asFUNCTION(CombatDamage), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Heal(ActorHandle, float)", asFUNCTION(CombatHeal), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("float GetHealth(ActorHandle)", asFUNCTION(CombatGetHealth), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("float GetMaxHealth(ActorHandle)", asFUNCTION(CombatGetMaxHealth), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool IsDead(ActorHandle)", asFUNCTION(CombatIsDead), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool BeginAttack(ActorHandle, float damage = -1.0f)", asFUNCTION(CombatBeginAttack), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void EndAttack(ActorHandle)", asFUNCTION(CombatEndAttack), asCALL_CDECL));

    engine.SetDefaultNamespace("Interaction");
    Check(engine.RegisterGlobalFunction("ActorHandle FindNearest(const ActorHandle &in, float)", asFUNCTION(InteractionFindNearestGeneric), asCALL_GENERIC));
    Check(engine.RegisterGlobalFunction("ActorHandle FindNearest(float)", asFUNCTION(InteractionFindNearestSelfGeneric), asCALL_GENERIC));
    Check(engine.RegisterGlobalFunction("bool Use(const ActorHandle &in, const ActorHandle &in)", asFUNCTION(InteractionUseRef), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Use(const ActorHandle &in)", asFUNCTION(InteractionUseSelfRef), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetPrompt(const ActorHandle &in)", asFUNCTION(InteractionGetPromptRef), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetNearestName(float)",asFUNCTION(InteractionGetNearestName),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetNearestPrompt(float)",asFUNCTION(InteractionGetNearestPrompt),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string UseNearest(float)",asFUNCTION(InteractionUseNearest),asCALL_CDECL));

    engine.SetDefaultNamespace("Feedback");
    Check(engine.RegisterGlobalFunction("bool Shake(ActorHandle, float, float)",asFUNCTION(FeedbackShake),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Flash(ActorHandle, float, float)",asFUNCTION(FeedbackFlash),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool SlowMotion(ActorHandle, float, float)",asFUNCTION(FeedbackSlowMotion),asCALL_CDECL));


    engine.SetDefaultNamespace("Game");
    Check(engine.RegisterGlobalFunction("void Pause()",asFUNCTION(GamePause),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Resume()",asFUNCTION(GameResume),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool IsPaused()",asFUNCTION(GameIsPaused),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetTimeScale(float)",asFUNCTION(GameSetTimeScale),asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("float GetTimeScale()",asFUNCTION(GameGetTimeScale),asCALL_CDECL));


