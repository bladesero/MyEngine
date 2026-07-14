engine.SetDefaultNamespace("RigidBody");
Check(engine.RegisterGlobalFunction("void SetVelocity(const Vec3 &in)", asFUNCTION(BodySetVelocity), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void AddForce(const Vec3 &in)", asFUNCTION(BodyAddForce), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetAngularVelocity(const Vec3 &in)", asFUNCTION(BodySetAngularVelocity),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void AddTorque(const Vec3 &in)", asFUNCTION(BodyAddTorque), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void AddImpulse(const Vec3 &in)", asFUNCTION(BodyAddImpulse), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void AddAngularImpulse(const Vec3 &in)", asFUNCTION(BodyAddAngularImpulse),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void Teleport(const Vec3 &in, const Vec3 &in)", asFUNCTION(BodyTeleport),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetKinematicTarget(const Vec3 &in, const Vec3 &in)",
                                    asFUNCTION(BodySetKinematicTarget), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("Vec3 GetVelocity(ActorHandle)", asFUNCTION(BodyGetVelocity), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetVelocity(ActorHandle, const Vec3 &in)", asFUNCTION(BodySetVelocityHandle),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void AddForce(ActorHandle, const Vec3 &in)", asFUNCTION(BodyAddForceHandle),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetAngularVelocity(ActorHandle, const Vec3 &in)",
                                    asFUNCTION(BodySetAngularVelocityHandle), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void AddTorque(ActorHandle, const Vec3 &in)", asFUNCTION(BodyAddTorqueHandle),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void AddImpulse(ActorHandle, const Vec3 &in)", asFUNCTION(BodyAddImpulseHandle),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void AddAngularImpulse(ActorHandle, const Vec3 &in)",
                                    asFUNCTION(BodyAddAngularImpulseHandle), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void Teleport(ActorHandle, const Vec3 &in, const Vec3 &in)",
                                    asFUNCTION(BodyTeleportHandle), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetKinematicTarget(ActorHandle, const Vec3 &in, const Vec3 &in)",
                                    asFUNCTION(BodySetKinematicTargetHandle), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetUseGravity(ActorHandle, bool)", asFUNCTION(BodySetUseGravity),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool UsesGravity(ActorHandle)", asFUNCTION(BodyUsesGravity), asCALL_CDECL));

engine.SetDefaultNamespace("CharacterController");
Check(engine.RegisterGlobalFunction("void Move(const Vec3 &in)", asFUNCTION(CharacterControllerMove), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool IsGrounded()", asFUNCTION(CharacterControllerIsGrounded), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetUseGravity(bool)", asFUNCTION(CharacterControllerSetUseGravity),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void Move(ActorHandle, const Vec3 &in)", asFUNCTION(CharacterControllerMoveHandle),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool IsGrounded(ActorHandle)", asFUNCTION(CharacterControllerIsGroundedHandle),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetUseGravity(ActorHandle, bool)",
                                    asFUNCTION(CharacterControllerSetUseGravityHandle), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool Jump(float speed = -1.0f)", asFUNCTION(CharacterControllerJump),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("bool Jump(ActorHandle, float speed = -1.0f)",
                                    asFUNCTION(CharacterControllerJumpHandle), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("Vec3 GetActualVelocity(ActorHandle)",
                                    asFUNCTION(CharacterControllerGetActualVelocity), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetAirControl(ActorHandle, float)",
                                    asFUNCTION(CharacterControllerSetAirControl), asCALL_CDECL));

engine.SetDefaultNamespace("Collider");
Check(engine.RegisterGlobalFunction("bool IsTrigger(ActorHandle)", asFUNCTION(ColliderIsTrigger), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetTrigger(ActorHandle, bool)", asFUNCTION(ColliderSetTrigger),
                                    asCALL_CDECL));
Check(engine.RegisterGlobalFunction("uint GetLayer(ActorHandle)", asFUNCTION(ColliderGetLayer), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetLayer(ActorHandle, uint)", asFUNCTION(ColliderSetLayer), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("uint GetLayerMask(ActorHandle)", asFUNCTION(ColliderGetLayerMask), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("void SetLayerMask(ActorHandle, uint)", asFUNCTION(ColliderSetLayerMask),
                                    asCALL_CDECL));

engine.SetDefaultNamespace("Physics");
Check(engine.RegisterGlobalFunction(
    "RaycastHit Raycast(const Vec3 &in, const Vec3 &in, float distance = 1000.0f, uint mask = 0xffffffff)",
    asFUNCTION(PhysicsRaycast), asCALL_CDECL));
Check(engine.RegisterGlobalFunction("UInt64Array@ OverlapSphere(const Vec3 &in, float radius, uint mask = 0xffffffff)",
                                    asFUNCTION(PhysicsOverlapSphere), asCALL_CDECL));
