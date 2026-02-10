#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# script for Huawei ubs-io to build pkg
# version: 1.0.0
# ***********************************************************************

rm -rf ~/rpmbuild
rpmdev-setuptree
cp ubs-io.tar.gz ~/rpmbuild/SOURCES/

# 带cli工具包, 需要开发人员提供cli so进行调试
rpmbuild -ba ubs-io.spec

# 标准发布包, 无测试包
rpmbuild -ba ubs-io.spec --define "with_cli 0"

# debug包
rpmbuild -ba ubs-io.spec --define "build_type debug"