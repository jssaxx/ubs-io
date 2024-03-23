#!/bin/bash
CURRENT_PATH=$(cd "$(dirname "$0")"; pwd)

cd "${CURRENT_PATH:?}"
hdt build \
  && hdt run "--args=\"--gtest_output=xml:report.xml\"" \
  && hdt report