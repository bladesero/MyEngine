#!/usr/bin/env bash
set -euo pipefail

# Build the native Linux Vulkan runtime, Player, Editor and tests.
# A Vulkan loader development package is needed for linking; a Vulkan ICD and
# an X11/Wayland session are additionally needed to run graphical applications.
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${MYENGINE_BUILD_MODE:-debug}"
xmake_bin="${XMAKE_PROGRAM_FILE:-xmake}"
if [[ "$mode" != debug && "$mode" != release ]]; then
    echo "MYENGINE_BUILD_MODE must be debug or release" >&2
    exit 2
fi
cd "$repo_root"
"$xmake_bin" f -p linux -m "$mode" --vulkan=y -y
for target in MyEnginePlayer MyEngineEditor MyEngineTests MyEngineRuntimeLinkProbe; do
    "$xmake_bin" build "$target"
done
"$xmake_bin" run MyEngineRuntimeLinkProbe
printf '%s\n' 'Build complete. Run xmake run MyEnginePlayer --backend vulkan or xmake run MyEngineEditor --backend vulkan.'
