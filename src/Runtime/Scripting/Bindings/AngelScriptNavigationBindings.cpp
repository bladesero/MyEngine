engine.SetDefaultNamespace("Navigation");
Check(engine.RegisterGlobalFunction("bool SetDestination(ActorHandle, const Vec3 &in)",
                                    asFUNCTION(NavigationSetDestination), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void Stop(ActorHandle)", asFUNCTION(NavigationStop), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool HasPath(ActorHandle)", asFUNCTION(NavigationHasPath), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool ReachedDestination(ActorHandle)", asFUNCTION(NavigationReached),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("string FindPathJson(const Vec3 &in, const Vec3 &in)",
                                    asFUNCTION(NavigationFindPathJson), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool SetAreaBlocked(const Vec3 &in, const Vec3 &in, bool)",
                                    asFUNCTION(NavigationSetAreaBlocked), asCALL_CDECL));
engine.SetDefaultNamespace("Perception");
Check(engine.RegisterGlobalFunction("void EmitSound(const Vec3 &in, float, ActorHandle)",
                                    asFUNCTION(PerceptionEmitSound), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool CanSee(ActorHandle, ActorHandle, float)", asFUNCTION(PerceptionCanSee),
                                    asCALL_CDECL));
engine.SetDefaultNamespace("Enemy");
Check(engine.RegisterGlobalFunction("bool SetTarget(ActorHandle, ActorHandle)", asFUNCTION(EnemySetTarget),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("int GetState(ActorHandle)", asFUNCTION(EnemyGetState), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void Stagger(ActorHandle, float duration = 0.25f)", asFUNCTION(EnemyStagger),
                                    asCALL_CDECL));
