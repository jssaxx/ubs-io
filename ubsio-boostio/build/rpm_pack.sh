#!/usr/bin/env bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# script for Huawei ubs-io to build rpm packages
# version: 1.0.0
# ***********************************************************************

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
SPEC_FILE="$SCRIPT_DIR/ubs-io.spec"

BUILD_TYPE="${1:-release}"
case "$BUILD_TYPE" in
    release|debug)
        ;;
    -h|--help)
        echo "Usage: $0 [release|debug]"
        exit 0
        ;;
    *)
        echo "Invalid build type: $BUILD_TYPE. Expected release or debug." >&2
        exit 1
        ;;
esac

VERSION=$(awk '/^Version:/ {print $2; exit}' "$SPEC_FILE")
NAME=$(awk '/^Name:/ {print $2; exit}' "$SPEC_FILE")
RPM_TOPDIR="${RPM_TOPDIR:-$HOME/rpmbuild}"
SOURCE_DIR="${NAME}-${VERSION}"
SOURCE_TAR="${SOURCE_DIR}.tar.gz"

rm -rf "$RPM_TOPDIR"
mkdir -p "$RPM_TOPDIR"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

git -C "$REPO_DIR" archive --format=tar.gz --prefix="${SOURCE_DIR}/" \
    -o "$RPM_TOPDIR/SOURCES/$SOURCE_TAR" HEAD
cp -f "$SPEC_FILE" "$RPM_TOPDIR/SPECS/"

rpmbuild -ba \
    --define "_topdir $RPM_TOPDIR" \
    --define "build_type $BUILD_TYPE" \
    "$RPM_TOPDIR/SPECS/$(basename "$SPEC_FILE")"
