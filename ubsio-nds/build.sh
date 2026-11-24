#!/usr/bin/env bash

set -euo pipefail

usage() {
    echo "Usage: $0 [-t debug|release] [--bench ON|OFF] [--origin_runpath ON|OFF]"
}

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_type=release
build_bench=OFF
origin_runpath=OFF

while (( $# > 0 )); do
    case "$1" in
        -t|--type)
            [[ $# -ge 2 ]] || { usage; exit 1; }
            build_type="${2,,}"
            [[ "$build_type" == debug || "$build_type" == release ]] || { usage; exit 1; }
            shift 2
            ;;
        --bench|--origin_runpath)
            [[ $# -ge 2 ]] || { usage; exit 1; }
            value="${2^^}"
            [[ "$value" == ON || "$value" == OFF ]] || { usage; exit 1; }
            if [[ "$1" == --bench ]]; then
                build_bench="$value"
            else
                origin_runpath="$value"
            fi
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

source_flags=()
if [[ -n "${FETCHCONTENT_SOURCE_DIR_XDS:-}" ]]; then
    source_flags+=("-DFETCHCONTENT_SOURCE_DIR_XDS=$FETCHCONTENT_SOURCE_DIR_XDS")
fi

cmake -S "$project_dir" -B "$project_dir/Build" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DUBSIO_BUILD_NDS_BENCH="$build_bench" \
    -DUBSIO_ENABLE_ORIGIN_RUNPATH="$origin_runpath" \
    "${source_flags[@]}"
cmake --build "$project_dir/Build" --parallel "$(nproc)"
cmake --install "$project_dir/Build"
