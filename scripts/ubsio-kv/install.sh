
#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for installing UBSIO-KV in target environment
# version: 1.0.0
# ***********************************************************************

set -e

echo "========================================================"
echo "Installing UBSIO-KV"
echo "========================================================"

BASE_DIR=$(dirname "$0")

echo "[1/2] Installing libraries..."
cp -r "${BASE_DIR}/ubsio/lib/"* /usr/lib64/
cp -r "${BASE_DIR}/zookeeper/lib/"* /usr/lib64/

echo "[2/2] Installing binaries and configs..."
mkdir -p /etc/boostio/bin
cp -r "${BASE_DIR}/ubsio/bin/"* /etc/boostio/bin/
cp -r "${BASE_DIR}/ubsio/conf/"* /etc/boostio/

echo ""
echo "========================================================"
echo "Installation completed."
echo "========================================================"
echo "Libraries:    /usr/lib64/"
echo "Binaries:     /etc/boostio/bin/"
echo "Config:      /etc/boostio/bio.conf"
