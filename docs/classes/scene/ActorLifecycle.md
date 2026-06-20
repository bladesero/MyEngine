# Actor and component lifecycle

`Scene` owns actors and exposes `ActorHandle` (`slot index + generation`) as the
stable runtime identity. A raw `Actor*` returned by `TryGetActor` is frame-local:
it must not be retained across `FlushCommands`.

## Structural mutation

Actor creation, destruction, reparenting, active-state changes, and component
add/remove operations are represented by the Scene command buffer. A queued
creation reserves a handle immediately but is not resolvable until the next
safe-point flush. A queued destroy marks the actor and its descendants
`PendingDestroy` immediately, excluding them from update, physics, collision,
and rendering before storage is reclaimed at the flush.

The legacy pointer-returning creation helpers remain compatibility wrappers for
editor and test code outside traversal. During traversal, structural operations
are deferred and callers must use handles.

## Construction and play callbacks

Scene and future Prefab loading use a batch transaction:

1. Reserve handles and construct every actor.
2. Deserialize every component.
3. Connect parent/child relationships.
4. Run `OnAttach` for the batch.
5. Run `OnInitialize` after the complete object graph exists.

Entering Play runs three global parent-to-child passes: `OnBeginPlay`,
`OnEnable`, then `OnStart`. A frame executes fixed updates and physics first,
then `OnUpdate`, then `OnLateUpdate`, flushing queued commands only at safe
phase boundaries. Leaving Play or destroying a live object runs
`OnEndPlay`, `OnDisable`, and `OnDetach` in that order.

`activeSelf` is serialized. `activeInHierarchy` is derived from `activeSelf`
and the parent's effective state. Enable propagates parent-to-child; disable
and destruction propagate child-to-parent.

## Determinism

Components retain insertion order. Callback dispatch sorts by
`Component::GetExecutionOrder()` and then by insertion order. Actor traversal
uses root insertion order and parent-before-child hierarchy order; reverse
lifecycle operations use the inverse order.
