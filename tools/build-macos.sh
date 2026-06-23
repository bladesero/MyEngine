#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: tools/build-macos.sh [options] [target...]

Options:
  -m, --mode MODE     Build mode: debug or release. Default: debug
  -t, --run-tests     Run MyEngineTests after building.
  -h, --help          Show this help.

Targets:
  If no target is given, builds:
    MyEngineEditor MyEnginePlayer MyEngineTests

Examples:
  tools/build-macos.sh
  tools/build-macos.sh --mode release
  tools/build-macos.sh --run-tests
  tools/build-macos.sh MyEngineEditor
USAGE
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="debug"
run_tests=0
targets=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -m|--mode)
            if [[ $# -lt 2 ]]; then
                echo "error: --mode requires a value" >&2
                exit 2
            fi
            mode="$2"
            shift 2
            ;;
        -t|--run-tests)
            run_tests=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            while [[ $# -gt 0 ]]; do
                targets+=("$1")
                shift
            done
            ;;
        -*)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
        *)
            targets+=("$1")
            shift
            ;;
    esac
done

case "$mode" in
    debug|release) ;;
    *)
        echo "error: unsupported mode '$mode' (expected debug or release)" >&2
        exit 2
        ;;
esac

if [[ ${#targets[@]} -eq 0 ]]; then
    targets=(MyEngineEditor MyEnginePlayer MyEngineTests)
fi

cd "$repo_root"

echo "[build-macos] configure: xmake f -p macosx -m $mode"
xmake f -p macosx -m "$mode"

for target in "${targets[@]}"; do
    echo "[build-macos] build: $target"
    xmake build "$target"
done

if [[ "$run_tests" -eq 1 ]]; then
    echo "[build-macos] run: MyEngineTests"
    xmake run MyEngineTests
fi

echo "[build-macos] done"
