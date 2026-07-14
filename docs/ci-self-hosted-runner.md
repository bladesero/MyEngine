# Self-hosted GPU runner security runbook

The `myengine-gpu` runner executes release packages on real D3D11/D3D12
hardware. Treat every process launched by a workflow as untrusted and keep the
runner outside developer and production trust zones.

## GitHub-side controls

1. Create the `myengine-gpu-production` environment.
2. Restrict deployment branches to `master` only.
3. Require a reviewer and prevent self-review where the repository plan allows
   those protection rules.
4. Do not store repository, signing, cloud, or deployment secrets in this
   environment. The GPU smoke gate does not require them.
5. Protect `master` and require review for changes to:
   - `.github/workflows/**`
   - `tools/release-smoke.ps1`
   - `tools/publish.ps1`
6. For an organization-owned runner, place it in a dedicated runner group and
   grant access only to `bladesero/MyEngine` and
   `bladesero/MyEngine/.github/workflows/windows-ci.yml@master`.

The workflow also verifies the repository and exact `master` ref. These checks
are defense in depth; GitHub-side environment and runner-group policy are the
security boundary because a workflow on an untrusted ref can edit its own
checks.

## Host controls

- Use a dedicated Windows installation or disposable VM, never a developer
  workstation.
- Run the Actions service as a non-administrator account with no interactive
  logon, browser profile, SSH key, PAT, signing key, network share, or reusable
  credential.
- Deny unsolicited inbound traffic and isolate the host from the LAN. Allow
  outbound traffic only to GitHub and the package/tool endpoints required by a
  clean release build.
- Enable Windows Update, Microsoft Defender, tamper protection, and audit logs.
- Forward the runner `_diag` logs, Windows event logs, and security alerts to a
  system the runner account cannot modify.
- Prefer an ephemeral/JIT runner and destroy the instance after one job. When
  physical GPU constraints require a persistent host, restore a trusted system
  image after every job. Deleting `_work` alone is not a security reset.

## Before enabling the runner

- Confirm the runner has only the labels `self-hosted`, `Windows`, `X64`, and
  `myengine-gpu` required by the workflow.
- Confirm the environment rejects a manual run from any ref other than
  `master`.
- Confirm checkout uses no persisted credential and all referenced Actions are
  pinned to full commit SHAs.
- Confirm the runner account cannot read a developer home directory or reach
  another workstation over SMB, WinRM, or RDP.

## Baseline and incident procedure

For the first accepted baseline, record the workflow URL, commit SHA, runner
image version, GPU and driver versions, D3D11/D3D12 results, soak duration, and
the archived artifact names. Keep that evidence outside the runner.

Real driver/device-removal validation is a separately supervised lab gate. Run
the published package without the synthetic injection flag, then use the lab's
approved external driver/device fault method:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\gpu-device-loss-lab.ps1 `
  -Package <published-package> -Backend d3d12 -TimeoutSeconds 120
```

Repeat for D3D11. The script rejects exit code 0, synthetic native code `-1`,
and the synthetic diagnostic marker. It preserves GPU/driver identity, Player
hash, exit code, diagnostic hash, and timestamps under `lab-evidence`. Never
automate disabling the display adapter that owns an interactive desktop; use an
isolated test host and an approved vendor/driver fault mechanism.

The protected manual workflow exposes the same gate behind the default-off
`run_real_device_loss_lab` input. When enabled, watch the job until the
supervised fault step starts, induce the approved external fault within the
configured window, and retain the always-uploaded evidence artifact. Run D3D11
and D3D12 as separate workflow executions so each result has an unambiguous
backend, run ID, and diagnostic.

If a job runs from an unexpected ref, modifies services or scheduled tasks,
creates a user, produces a Defender alert, or makes unexpected network
connections:

1. Disable or remove the runner in GitHub immediately.
2. Preserve externally forwarded logs and the workflow/run identifiers.
3. Rotate any credential that was reachable from the host.
4. Reimage the host from a trusted source; do not clean and reuse the affected
   installation.
5. Register a new runner and repeat the baseline gate.
