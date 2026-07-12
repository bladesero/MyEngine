    engine.SetDefaultNamespace("Scene");
    Check(engine.RegisterGlobalFunction("ActorHandle FromUInt64(uint64)",
        asFUNCTION(ActorHandleFromUInt64Generic), asCALL_GENERIC));
    Check(engine.RegisterGlobalFunction("ActorHandle GetSelf()",
        asFUNCTION(SceneGetSelfGeneric), asCALL_GENERIC));
    Check(engine.RegisterGlobalFunction("bool IsValid(ActorHandle)",
        asFUNCTION(SceneIsValid), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("ActorHandle FindByName(const string &in)",
        asFUNCTION(SceneFindByNameGeneric), asCALL_GENERIC));
    Check(engine.RegisterGlobalFunction("ActorHandle FindByTag(const string &in)",
        asFUNCTION(SceneFindByTagGeneric), asCALL_GENERIC));
    Check(engine.RegisterGlobalFunction("ActorHandleArray@ FindAllByName(const string &in)",
        asFUNCTION(SceneFindAllByName), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("ActorHandleArray@ FindAllByTag(const string &in)",
        asFUNCTION(SceneFindAllByTag), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("ActorHandleArray@ FindAllInLayer(uint)",
        asFUNCTION(SceneFindAllInLayer), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetName(ActorHandle)",
        asFUNCTION(SceneGetName), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetName(ActorHandle, const string &in)",
        asFUNCTION(SceneSetName), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("ActorHandle GetParent(ActorHandle)",
        asFUNCTION(SceneGetParentGeneric), asCALL_GENERIC));
    Check(engine.RegisterGlobalFunction("ActorHandleArray@ GetChildren(ActorHandle)",
        asFUNCTION(SceneGetChildren), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("ActorHandleArray@ FindAllWithComponent(const string &in)",
        asFUNCTION(SceneFindAllWithComponent), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("ActorHandleArray@ FindInRadius(const Vec3 &in, float, uint mask = 0xffffffff)",
        asFUNCTION(SceneFindInRadius), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("ActorHandle FindNearestWithComponent(const string &in, const Vec3 &in, float)",
        asFUNCTION(SceneFindNearestWithComponentGeneric), asCALL_GENERIC));
    Check(engine.RegisterGlobalFunction("float GetDistance(ActorHandle, ActorHandle)",
        asFUNCTION(SceneGetDistance), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool GetActive(ActorHandle)",
        asFUNCTION(SceneGetActive), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("uint GetLayer(ActorHandle)",
        asFUNCTION(SceneGetLayer), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("ActorHandle CreateActor(const string &in)",
        asFUNCTION(SceneCreateActorGeneric), asCALL_GENERIC));
    Check(engine.RegisterGlobalFunction("void DestroyActor(ActorHandle)",
        asFUNCTION(SceneDestroyActor), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetActive(ActorHandle, bool)",
        asFUNCTION(SceneSetActive), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetTag(ActorHandle, const string &in)",
        asFUNCTION(SceneSetTag), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetLayer(ActorHandle, uint)",
        asFUNCTION(SceneSetLayer), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void DestroyDeferred(ActorHandle)",
        asFUNCTION(SceneDestroyActor), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetParent(ActorHandle, ActorHandle)",
        asFUNCTION(SceneSetParent), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void MoveActor(ActorHandle, ActorHandle, ActorHandle)",
        asFUNCTION(SceneMoveActor), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("ActorHandle InstantiatePrefab(const string &in, const Vec3 &in, const Vec3 &in)",
        asFUNCTION(SceneInstantiatePrefabGeneric), asCALL_GENERIC));

    engine.SetDefaultNamespace("PrefabInstance");
    Check(engine.RegisterGlobalFunction("bool IsInstance(ActorHandle)",
        asFUNCTION(PrefabInstanceIsInstance), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool IsRoot(ActorHandle)",
        asFUNCTION(PrefabInstanceIsRoot), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetAssetPath(ActorHandle)",
        asFUNCTION(PrefabInstanceGetAssetPath), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("ActorHandle GetRoot(ActorHandle)",
        asFUNCTION(PrefabInstanceGetRootGeneric), asCALL_GENERIC));

    engine.SetDefaultNamespace("Components");
    Check(engine.RegisterGlobalFunction("bool Has(ActorHandle, const string &in)",
        asFUNCTION(ComponentsHas), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Add(ActorHandle, const string &in)",
        asFUNCTION(ComponentsAdd), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool Remove(ActorHandle, const string &in)",
        asFUNCTION(ComponentsRemove), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetJson(ActorHandle, const string &in)",
        asFUNCTION(ComponentsGetJson), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool SetJson(ActorHandle, const string &in, const string &in)",
        asFUNCTION(ComponentsSetJson), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("string GetPropertyJson(ActorHandle, const string &in, const string &in)",
        asFUNCTION(ComponentsGetPropertyJson), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool SetPropertyJson(ActorHandle, const string &in, const string &in, const string &in)",
        asFUNCTION(ComponentsSetPropertyJson), asCALL_CDECL));


