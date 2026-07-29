#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: tools/check-abi.sh [options]

Checks the source-level Runtime/shader ABI contract, architecture rules, the
Runtime shared library, and MyEngineRuntimeLinkProbe.

Options:
  --refresh          Approve the current ABI contract. On Windows, also approve
                     the generated runtime-windows-x64.exports after review.
  --no-build         Only compare/refresh the source ABI contract.
  --show             Print the generated current ABI contract.
  -m, --mode MODE    Build mode: debug or release. Default: debug.
  -h, --help         Show this help.

Examples:
  tools/check-abi.sh
  tools/check-abi.sh --show --no-build
  tools/check-abi.sh --refresh
  tools/check-abi.sh --refresh --mode release

The shader cache does not need to be deleted after a contract refresh. Its key
contains these ABI values, so stale artifacts stop matching automatically.
USAGE
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
baseline="$repo_root/xmake/abi/runtime-contract.txt"
generated_dir="$repo_root/build/.myengine/abi"
generated="$generated_dir/runtime-contract.current"
mode="debug"
refresh=0
run_build=1
show=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --refresh)
            refresh=1
            shift
            ;;
        --no-build)
            run_build=0
            shift
            ;;
        --show)
            show=1
            shift
            ;;
        -m|--mode)
            if [[ $# -lt 2 ]]; then
                echo "error: --mode requires a value" >&2
                exit 2
            fi
            mode="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 2
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

for tool in grep find sed awk sort diff tr; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: required tool not found: $tool" >&2
        exit 1
    fi
done

hash_stream() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 | awk '{print $1}'
    else
        echo "error: sha256sum or shasum is required" >&2
        return 1
    fi
}

extract_uint() {
    local file="$1"
    local name="$2"
    local value
    value="$(
        sed -nE "s/.*${name}[[:space:]]*=[[:space:]]*([0-9]+).*/\\1/p" "$file" |
            awk 'NR == 1 { print; exit }'
    )"
    if [[ -z "$value" ]]; then
        echo "error: could not extract $name from ${file#"$repo_root/"}" >&2
        exit 1
    fi
    printf '%s' "$value"
}

extract_cooker_contract() {
    local value
    value="$(
        grep -Eo 'shader-cooker-v[0-9][^|"]*' \
            "$repo_root/src/Runtime/Renderer/ShaderCooker.cpp" |
            awk 'NR == 1 { print; exit }'
    )"
    if [[ -z "$value" ]]; then
        echo "error: could not extract shader cooker contract" >&2
        exit 1
    fi
    printf '%s' "$value"
}

list_runtime_export_headers() {
    if command -v rg >/dev/null 2>&1; then
        (
            cd "$repo_root"
            rg -l 'MYENGINE_RUNTIME_API' src/Runtime -g '*.h' -g '*.hpp'
        )
        return
    fi

    while IFS= read -r -d '' file; do
        if grep -q 'MYENGINE_RUNTIME_API' "$file"; then
            printf '%s\n' "${file#"$repo_root/"}"
        fi
    done < <(
        find "$repo_root/src/Runtime" -type f \
            \( -name '*.h' -o -name '*.hpp' \) -print0
    )
}

runtime_public_headers_hash() {
    local headers=()
    while IFS= read -r file; do
        headers+=("$file")
    done < <(
        list_runtime_export_headers | LC_ALL=C sort
    )
    if [[ ${#headers[@]} -eq 0 ]]; then
        echo "error: no Runtime export headers found" >&2
        return 1
    fi
    {
        local file
        for file in "${headers[@]}"; do
            printf 'FILE:%s\n' "$file"
            # Strip line comments and normalize whitespace. This intentionally
            # remains conservative: layout/default/signature changes in an
            # exported header require an explicit contract refresh.
            sed -E 's,//.*$,,' "$repo_root/$file"
            printf '\n'
        done
    } | tr '\n\t' '  ' | sed -E 's/[[:space:]]+/ /g' | hash_stream
}

atomic_copy() {
    local source="$1"
    local destination="$2"
    local temporary="${destination}.tmp.$$"
    mkdir -p "$(dirname "$destination")"
    cp "$source" "$temporary"
    mv "$temporary" "$destination"
}

mkdir -p "$generated_dir"

shader_asset_header="$repo_root/src/Runtime/Assets/ShaderAsset.h"
metal_artifact_header="$repo_root/src/Runtime/Renderer/MetalShaderArtifact.h"
runtime_header_count="$(
    list_runtime_export_headers |
        LC_ALL=C sort -u |
        awk 'END { print NR }'
)"
cooked_shader_abi="$(extract_uint "$shader_asset_header" kCookedShaderAbiVersion)"
metal_container_abi="$(extract_uint "$metal_artifact_header" kContainerVersion)"
metal_transformation_abi="$(extract_uint "$metal_artifact_header" kTransformationAbi)"
public_headers_hash="$(runtime_public_headers_hash)"
cooker_contract="$(extract_cooker_contract)"

{
    echo "contract_format=1"
    echo "cooked_shader_abi=$cooked_shader_abi"
    echo "metal_container_abi=$metal_container_abi"
    echo "metal_transformation_abi=$metal_transformation_abi"
    echo "runtime_export_header_count=$runtime_header_count"
    echo "runtime_public_headers_sha256=$public_headers_hash"
    echo "shader_cooker_contract=$cooker_contract"
} | LC_ALL=C sort >"$generated"

if [[ "$show" -eq 1 ]]; then
    cat "$generated"
fi

contract_changed=0
if [[ ! -f "$baseline" ]]; then
    contract_changed=1
    echo "[abi] approved contract is missing: ${baseline#"$repo_root/"}" >&2
else
    if ! diff -u "$baseline" "$generated"; then
        contract_changed=1
    fi
fi

if [[ "$contract_changed" -eq 1 ]]; then
    if [[ "$refresh" -ne 1 ]]; then
        echo "[abi] contract changed; review the diff, then run:" >&2
        echo "      tools/check-abi.sh --refresh" >&2
        exit 1
    fi
    atomic_copy "$generated" "$baseline"
    echo "[abi] refreshed approved contract: ${baseline#"$repo_root/"}"
else
    echo "[abi] source contract matches: ${baseline#"$repo_root/"}"
fi

if [[ "$run_build" -ne 1 ]]; then
    exit 0
fi

if ! command -v xmake >/dev/null 2>&1; then
    echo "error: xmake is required unless --no-build is used" >&2
    exit 1
fi

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) host_platform="windows" ;;
    Darwin*) host_platform="macosx" ;;
    Linux*) host_platform="linux" ;;
    *) host_platform="unknown" ;;
esac

cd "$repo_root"
echo "[abi] configure: mode=$mode"
xmake f -m "$mode"

echo "[abi] architecture gate"
xmake build MyEngine.Architecture

echo "[abi] build Runtime"
runtime_status=0
windows_generated="$generated_dir/runtime-windows-x64-${mode}.exports"
windows_baseline="$repo_root/xmake/abi/runtime-windows-x64.exports"
if [[ "$host_platform" == "windows" && "$refresh" -eq 1 ]]; then
    # A failed compilation must never cause --refresh to approve a stale
    # manifest left by an earlier successful build.
    rm -f "$windows_generated"
fi
xmake build MyEngineRuntime || runtime_status=$?

if [[ "$runtime_status" -ne 0 ]]; then
    if [[ "$host_platform" == "windows" && "$refresh" -eq 1 && -s "$windows_generated" ]]; then
        echo "[abi] reviewing Windows export baseline difference"
        diff -u "$windows_baseline" "$windows_generated" || true
        atomic_copy "$windows_generated" "$windows_baseline"
        echo "[abi] refreshed Windows exports: ${windows_baseline#"$repo_root/"}"
        xmake build MyEngineRuntime
        xmake build MyEngine.Architecture
    else
        echo "[abi] Runtime build/ABI verification failed" >&2
        if [[ "$host_platform" != "windows" ]]; then
            echo "[abi] Windows exports can only be regenerated under Windows/MSVC" >&2
        fi
        exit "$runtime_status"
    fi
fi

echo "[abi] Runtime link probe"
xmake build MyEngineRuntimeLinkProbe
xmake run MyEngineRuntimeLinkProbe

if [[ "$host_platform" != "windows" ]]; then
    echo "[abi] note: runtime-windows-x64.exports was not checked on $host_platform"
fi
echo "[abi] PASS"
