# M5 Runtime TaskService validation - 2026-07-12

Implemented the bounded Runtime task ownership foundation and migrated
SceneManager scene read/parse preparation plus AssetManager asynchronous loads
from `std::async`.

Validated contracts:

- One-worker deterministic high/normal/low priority ordering and FIFO behavior.
- Stable task-name retention.
- Cooperative queued/running cancellation with `TaskCancelled` propagation.
- Worker exception propagation through typed `TaskHandle<T>`.
- Shutdown cancellation, queue drain, join, and exact task statistics.
- SceneManager valid load, invalid path, replacement/cancellation, and existing
  incremental instantiation behavior.
- AssetManager typed async load/failure behavior and SceneManager preload handle
  polling through `TaskHandle<std::shared_ptr<Asset>>`.
- AssetManager shutdown ordering: its scope is cancelled and joined before the
  cache mutex is acquired, while deduplicated requests cannot complete before
  their in-flight handle is published.
- Project and engine shader cooking use per-publish low-priority task scopes;
  normal completion and every early return join all compiler work.

`xmake build MyEngineTests` and `xmake run MyEngineTests` completed successfully
in release configuration after both Runtime and publisher migrations; all 165
tests passed. No subsystem-owned `std::async` remains under `src/`. Remote
workflow/self-hosted runner execution was intentionally deferred by user
direction.
