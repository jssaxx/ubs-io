
#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for uninstalling UBSIO-KV and related components
# version: 1.0.0
# ***********************************************************************

set -e

echo "========================================================"
echo "Uninstalling UBSIO-KV and Related Components"
echo "========================================================"
echo ""

remove_libraries() {
    echo "[1/2] Removing libraries..."
    
    rm -rf /usr/lib64/boostio/
    rm -f /usr/lib64/libubsio_kvc.so*
    rm -f /usr/lib64/libbio_*
    rm -f /usr/lib64/libcli_agent.so
    rm -f /usr/lib64/libock_i*
    rm -f /usr/lib64/libtracepoint.*
    rm -f /usr/lib64/libzookeeper_mt.so*
    rm -f /usr/lib64/libsdk_diagnose.so
    rm -f /usr/lib64/libserver_diagnose.so
    rm -f /usr/lib64/libhcom*
    
    echo "  Libraries removed."
}

remove_configs() {
    echo "[2/2] Removing configurations..."
    
    rm -rf /etc/boostio/
    
    echo "  Configurations removed."
}

main() {
    remove_libraries
    remove_configs
    
    echo ""
    echo "========================================================"
    echo "Uninstall completed."
    echo "========================================================"
}

main "$@"
