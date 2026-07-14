# M5 runtime performance capture validation - 2026-07-12

## Contracts exercised

- Runtime budget evaluator: warmup, minimum samples, frame percentiles, GPU p95,
  working-set growth, dropped fixed ticks, deterministic violations, raw samples,
  and JSON serialization.
- Player capture: real D3D11 frame/scheduler/process samples, schema-v1 report,
  build provenance, scene/backend/resolution context, and transactional write.
- Release package: D3D11 and D3D12 performance reports validated from the
  published Player alongside RHI conformance and existing failure paths.

## Evidence

- `tools/smoke.ps1`: PASS, 163 tests.
- Direct D3D11 Player capture, 3 seconds: PASS, 98 post-warmup samples,
  p50 frame `1.7703 ms`, p95 frame `2.2480 ms`, raw sample count matched the
  summary, and provenance/scene/backend fields were present.
- `tools/release-smoke.ps1 -SoakSeconds 5 -ReloadIterations 1`: PASS for both
  stable backends, performance report validation, synthetic device loss,
  reload, package corruption, missing dependencies, and invalid config.

Both D3D11 and D3D12 reported NVIDIA GeForce RTX 4070 SUPER, driver
`32.0.15.9579`, vendor `4318`, and device `10115`. Both loaded the checked-in
`desktop-60fps` / `desktop-discrete` version-1 profile. GPU timing availability
is recorded per raw sample and summarized as `gpuSampleCount`; the GPU budget is
evaluated only when timing samples exist.

After adding profile migration/cook-closure coverage, all 164 tests passed.
Published content increased from 54 to 56 files and the release gate proved the
report source was `Content/Config/Performance.profile.json`, not the builtin
fallback.

## Four-scenario production gate

Version 2 adds checked-in `cold-start`, `warm-gameplay`, `scene-transition`, and
`resource-stress` profiles. The last two repeatedly reload the startup scene
under reduced resource budgets. Player records requested/completed reloads and
fails the report when work remains incomplete. `tools/performance-gates.ps1`
also rejects pending GPU uploads, descriptor allocation failures, resource
violations, raw-sample mismatches, or adapter/hardware-class drift.

The direct D3D11 scene-transition run completed 12 of 12 reloads with 955 raw
samples, `2.677 ms` p95 frame time, zero pending upload tasks/bytes, and no
performance or resource violations.

The checked-in quick gate subsequently passed all four D3D11 scenarios: 151,
201, 463, and 566 raw samples respectively; the transition/stress cases
completed 12/12 and 20/20 reloads. The final repository smoke passed 177 tests,
and the isolated release smoke passed publishing plus D3D11/D3D12 launch,
performance-report, device-loss, reload, corruption, missing-dependency, and
invalid-config checks.

Profile version 3 turns startup and transition responsiveness into explicit
budgets: Player observes the first SceneManager `Ready` transition, measures
every requested reload with a monotonic clock, records initial/max/average
latency, and fails the report when either profile limit is exceeded. The local
four-scenario gate validates these measurements in addition to completion and
resource residue.

The D3D11 v3 quick run measured initial Ready at `1054.28 ms` (cold start),
`1017.65 ms` (warm gameplay), `1001.41 ms` (scene transition), and `1010.46 ms`
(resource stress). Maximum individual reload latency was `268.11 ms` for the
12-transition profile and `269.16 ms` for the 20-transition constrained-budget
profile; both remained below their checked-in limits. The subsequent smoke
passed 177/177 tests and the isolated D3D11/D3D12 release publish gate passed.
