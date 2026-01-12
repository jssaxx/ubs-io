#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# version: 1.0.0
# ***********************************************************************
set -e

BUILD_TYPE=$1
VERSION_COUNT=${2:-1}

if [ -z "$BUILD_TYPE" ]; then
  echo 'Error: missing BUILD_TYPE parameter'
  exit 1
fi

parse_os_version()
{
    if [ -f "/etc/openEuler-release" ]; then
        OS_RELEASE_CONTENT=$(</etc/openEuler-release)
        if [[ $OS_RELEASE_CONTENT =~ ([0-9]+)\.([0-9]+).*-?SP-?([0-9]+) ]]; then
            printf "oe%s%ssp%s" "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}"
        elif [[ $OS_RELEASE_CONTENT =~ ([0-9]+)\.([0-9]+) ]]; then
            printf "oe%s%s" "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
        fi
    else
        echo "unknown_os"
    fi
}

DEPRESS_PATH=$(cd -P -- "$(dirname -- "$0")"/.. && pwd -P)
CUSTOM_OS=$(parse_os_version)
SRC_TAR="BoostIO_1.0.0_$(uname -s)-$(arch)_${BUILD_TYPE,,}.tar.gz"
SRC_TAR_PATH="${DEPRESS_PATH}/dist/${SRC_TAR}"
INSTALL_DIR_NAME="home"

if [ "$CUSTOM_OS" == "unknown_os" ]; then
  echo "error: unable to get os version"
  exit 1
fi

if [ ! -f "${SRC_TAR_PATH}" ]; then
    echo "error: can not find src file ${SRC_TAR_PATH}"
    exit 1
fi

# 清理并重建 RPM 环境
rm -rf ~/rpmbuild
rpmdev-setuptree
cp "${SRC_TAR_PATH}" ~/rpmbuild/SOURCES/
FULL_RELEASE="${VERSION_COUNT}.${CUSTOM_OS}"

# 生成 spec 文件
cat <<EOF > ~/rpmbuild/SPECS/boostio_rpm.spec
%define _os ${CUSTOM_OS}
%define _full_release ${FULL_RELEASE}
%define _build_name_fmt %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm
%define __os_install_post %{nil}
%define debug_package %{nil}

Name:       ubs_io-boostio
Version:    1.0.0
Release:    %{_full_release}
Summary:    BoostIO rpm
License:    Proprietary
Provides:   Huawei Technologies Co., Ltd
Group:      BoostIO
Source0:    ${SRC_TAR}
BuildArch:  %{_arch}

%description
This is BoostIO rpm

%prep
%setup -q -n boostio

%install
mkdir -p "%{buildroot}/${INSTALL_DIR_NAME}/"
cp -rvf %{SOURCE0} %{buildroot}/${INSTALL_DIR_NAME}/
tar -xzf %{SOURCE0} -C "%{buildroot}/${INSTALL_DIR_NAME}/"

%files
/${INSTALL_DIR_NAME}/*

%postun
rm -rf /${INSTALL_DIR_NAME}/boostio

%clean
rm -rf %{buildroot}
EOF

export QA_SKIP_RPATHS=1
rpmbuild -bb ~/rpmbuild/SPECS/boostio_rpm.spec

echo "rpm package in"
find ~/rpmbuild/RPMS/ -name "*.rpm" -printf "=> %p\n"
