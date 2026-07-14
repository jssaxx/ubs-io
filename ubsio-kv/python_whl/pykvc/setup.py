#!/usr/bin/env python
# coding: utf-8
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
import os
import setuptools
from setuptools import find_namespace_packages
from setuptools.dist import Distribution

os.environ['SOURCE_DATE_EPOCH'] = '315532800'


def get_version():
    version_file = os.path.join(os.path.dirname(__file__), '../../../../VERSION')
    if os.path.exists(version_file):
        with open(version_file, 'r') as f:
            return f.read().strip()
    return '1.0.0'


class BinaryDistribution(Distribution):
    def has_ext_modules(foo):
        return True


setuptools.setup(
    name="pykvc",
    version=get_version(),
    author="",
    author_email="",
    description="python api for ubsio_kvc",
    packages=find_namespace_packages(),
    url="",
    license="",
    python_requires=">=3.7",
    package_data={"pykvc": ["c2python_sdk.cpython*.so"]},
    distclass=BinaryDistribution

)