#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: bash scripts/build.sh [-t debug|release] [--components boostio,kv,nds] [--nds-bench ON|OFF]

Examples:
  bash scripts/build.sh
  bash scripts/build.sh -t debug --components boostio,nds
  bash scripts/build.sh -t release --components kv

The selected components are built in dependency order. Libraries are copied to
output/lib and executables to output/bin. The NDS build refreshes its libraries in output/lib.
EOF
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
output_lib="$repo_root/output/lib"
output_bin="$repo_root/output/bin"
build_type=release
components=boostio,kv,nds
nds_bench=OFF

while (( $# > 0 )); do
    case "$1" in
        -t|--type)
            [[ $# -ge 2 ]] || { usage; exit 1; }
            build_type="${2,,}"
            [[ "$build_type" == debug || "$build_type" == release ]] || { usage; exit 1; }
            shift 2
            ;;
        --components)
            [[ $# -ge 2 ]] || { usage; exit 1; }
            components="$2"
            shift 2
            ;;
        --nds-bench)
            [[ $# -ge 2 ]] || { usage; exit 1; }
            nds_bench="${2^^}"
            [[ "$nds_bench" == ON || "$nds_bench" == OFF ]] || { usage; exit 1; }
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 1
            ;;
    esac
done

build_boostio=0
build_kv=0
build_nds=0
if [[ "$components" == all ]]; then
    components=boostio,kv,nds
fi
[[ -n "$components" && "$components" != ,* && "$components" != *, && "$components" != *,,* ]] || {
    echo "Invalid component list: $components" >&2
    exit 1
}
IFS=',' read -r -a selected <<< "$components"
for component in "${selected[@]}"; do
    case "$component" in
        boostio) build_boostio=1 ;;
        kv) build_kv=1 ;;
        nds) build_nds=1 ;;
        *) echo "Unknown component: $component" >&2; usage; exit 1 ;;
    esac
done
if [[ "$nds_bench" == ON && "$build_nds" -ne 1 ]]; then
    echo "--nds-bench ON requires the nds component" >&2
    exit 1
fi

copy_libraries() {
    local source_dir="$1"
    local file
    [[ -d "$source_dir" ]] || return 0
    while IFS= read -r -d '' file; do
        cp -a "$file" "$output_lib/"
    done < <(find "$source_dir" -maxdepth 1 \( -type f -o -type l \) \
        \( -name '*.so' -o -name '*.so.*' -o -name '*.a' \) -print0)
}

copy_executables() {
    local source_dir="$1"
    local file
    [[ -d "$source_dir" ]] || return 0
    while IFS= read -r -d '' file; do
        if [[ -x "$file" ]]; then
            cp -a "$file" "$output_bin/"
        fi
    done < <(find "$source_dir" -maxdepth 1 \( -type f -o -type l \) -print0)
}

require_artifact() {
    [[ -e "$1" || -L "$1" ]] || { echo "Missing build artifact: $1" >&2; exit 1; }
}

mkdir -p "$output_lib" "$output_bin"

if [[ "$build_boostio" -eq 1 ]]; then
    bash "$repo_root/ubsio-boostio/build.sh" -t "$build_type" --cli --build_kv OFF --pkg "$output_lib"
    boostio_dist="$repo_root/ubsio-boostio/dist"
    require_artifact "$boostio_dist/boostio/lib/libbio_sdk.so"
    require_artifact "$boostio_dist/boostio/bin/bio_daemon"
    require_artifact "$boostio_dist/test_tools/lib/libcli_agent.so"
    require_artifact "$boostio_dist/test_tools/bin/cli_client"
    require_artifact "$boostio_dist/test_tools/bin/cli_server"
    copy_libraries "$boostio_dist/boostio/lib"
    copy_executables "$boostio_dist/boostio/bin"
    copy_libraries "$boostio_dist/test_tools/lib"
    copy_executables "$boostio_dist/test_tools/bin"
    if [[ -d "$boostio_dist/3rdparty" ]]; then
        while IFS= read -r -d '' lib_dir; do
            copy_libraries "$lib_dir"
        done < <(find "$boostio_dist/3rdparty" -type d \( -name lib -o -name lib64 \) -print0)
    fi
fi

if [[ "$build_kv" -eq 1 ]]; then
    bash "$repo_root/ubsio-kv/build.sh" -t "$build_type" --build_python OFF --origin_runpath ON
    kv_lib="$repo_root/ubsio-kv/dist/lib"
    require_artifact "$kv_lib/libubsio_kvc.so"
    copy_libraries "$kv_lib"
fi

if [[ "$build_nds" -eq 1 ]]; then
    bash "$repo_root/ubsio-nds/build.sh" -t "$build_type" --bench "$nds_bench" --origin_runpath ON
    nds_dist="$repo_root/ubsio-nds/dist"
    require_artifact "$nds_dist/lib/libascend_nds.so"
    copy_libraries "$nds_dist/lib"
    rm -f -- "$output_lib"/libnds.so "$output_lib"/libnds.so.*
    if [[ "$nds_bench" == ON ]]; then
        require_artifact "$nds_dist/bin/test_nds"
        copy_executables "$nds_dist/bin"
    fi
fi

echo "Build complete: components=$components type=$build_type"
echo "Libraries: $output_lib"
echo "Executables: $output_bin"
