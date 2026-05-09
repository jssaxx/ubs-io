
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

echo "[1/3] Installing dependencies..."

if ! rpm -q libaio &> /dev/null; then
    echo "  Installing libaio..."
    yum install -y libaio
else
    echo "  libaio already installed."
fi

if ! rpm -q libboundscheck &> /dev/null; then
    echo "  Installing libboundscheck..."
    yum install -y libboundscheck
else
    echo "  libboundscheck already installed."
fi

echo "[2/3] Installing libraries..."
cp -r "${BASE_DIR}/ubsio/lib/"* /usr/lib64/
cp -r "${BASE_DIR}/zookeeper/lib/"* /usr/lib64/

echo "[3/3] Installing binaries and configs..."
mkdir -p /etc/boostio/bin
cp -r "${BASE_DIR}/ubsio/bin/"* /etc/boostio/bin/
cp -r "${BASE_DIR}/ubsio/conf/"* /etc/boostio/

echo ""
echo "========================================================"
echo "Installation completed."
echo "========================================================"
echo "Libraries:    /usr/lib64/"
echo "Binaries:     /etc/boostio/bin/"
echo "Config:       /etc/boostio/bio.conf"
