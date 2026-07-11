    engine.SetDefaultNamespace("Actor");
    Check(engine.RegisterGlobalFunction("string GetName()",
        asFUNCTION(ActorGetName), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetPosition()",
        asFUNCTION(ReadActorPosition), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetWorldPosition()",
        asFUNCTION(ReadActorWorldPosition), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetRotation()",
        asFUNCTION(ReadActorRotation), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetParentRotation()",
        asFUNCTION(ReadParentRotation), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetForward()",
        asFUNCTION(ActorGetForward), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetRight()",
        asFUNCTION(ActorGetRight), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetParentForward()",
        asFUNCTION(ParentGetForward), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetParentRight()",
        asFUNCTION(ParentGetRight), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetPosition(const Vec3 &in)",
        asFUNCTION(ActorSetPosition), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetRotation(const Vec3 &in)",
        asFUNCTION(ActorSetRotation), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetParentRotation(const Vec3 &in)",
        asFUNCTION(ActorSetParentRotation), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Translate(const Vec3 &in)",
        asFUNCTION(ActorTranslate), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Rotate(const Vec3 &in)",
        asFUNCTION(ActorRotate), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetPosition(ActorHandle)",
        asFUNCTION(ActorGetPositionHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetWorldPosition(ActorHandle)",
        asFUNCTION(ActorGetWorldPositionHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetRotation(ActorHandle)",
        asFUNCTION(ActorGetRotationHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetScale(ActorHandle)",
        asFUNCTION(ActorGetScaleHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetPosition(ActorHandle, const Vec3 &in)",
        asFUNCTION(ActorSetPositionHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetRotation(ActorHandle, const Vec3 &in)",
        asFUNCTION(ActorSetRotationHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetScale(ActorHandle, const Vec3 &in)",
        asFUNCTION(ActorSetScaleHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Translate(ActorHandle, const Vec3 &in)",
        asFUNCTION(ActorTranslateHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Rotate(ActorHandle, const Vec3 &in)",
        asFUNCTION(ActorRotateHandle), asCALL_CDECL));

    engine.SetDefaultNamespace("Transform");
    Check(engine.RegisterGlobalFunction("Vec3 GetPosition()",
        asFUNCTION(TransformGetPosition), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetRotation()",
        asFUNCTION(TransformGetRotation), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetScale()",
        asFUNCTION(TransformGetScale), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetPosition(ActorHandle)",
        asFUNCTION(TransformGetPositionHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetRotation(ActorHandle)",
        asFUNCTION(TransformGetRotationHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetScale(ActorHandle)",
        asFUNCTION(TransformGetScaleHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetPosition(const Vec3 &in)",
        asFUNCTION(TransformSetPosition), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetRotation(const Vec3 &in)",
        asFUNCTION(TransformSetRotation), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetScale(const Vec3 &in)",
        asFUNCTION(TransformSetScale), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetPosition(ActorHandle, const Vec3 &in)",
        asFUNCTION(TransformSetPositionHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetRotation(ActorHandle, const Vec3 &in)",
        asFUNCTION(TransformSetRotationHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetScale(ActorHandle, const Vec3 &in)",
        asFUNCTION(TransformSetScaleHandle), asCALL_CDECL));

    engine.SetDefaultNamespace("Tags");
    Check(engine.RegisterGlobalFunction("string Get()",
        asFUNCTION(TagsGet), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string Get(ActorHandle)",
        asFUNCTION(TagsGetHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Set(const string &in)",
        asFUNCTION(TagsSet), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Set(ActorHandle, const string &in)",
        asFUNCTION(TagsSetHandle), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Has(const string &in)",
        asFUNCTION(TagsHas), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Has(ActorHandle, const string &in)",
        asFUNCTION(TagsHasHandle), asCALL_CDECL));


