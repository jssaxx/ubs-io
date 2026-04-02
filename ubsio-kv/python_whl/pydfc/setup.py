#!/usr/bin/env python
# coding: utf-8
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
import os
import setuptools
from setuptools import find_namespace_packages
from setuptools.dist import Distribution

os.environ['SOURCE_DATE_EPOCH'] = '315532800'


class BinaryDistribution(Distribution):
    def has_ext_modules(foo):
        return True


setuptools.setup(
    name="pydfc",
    version="1.0.0",
    author="",
    author_email="",
    description="python api for dfc",
    packages=find_namespace_packages(),
    url="",
    license="",
    python_requires=">=3.7",
    package_data={"pydfc": ["c2python_sdk.cpython*.so"]},
    distclass=BinaryDistribution

)