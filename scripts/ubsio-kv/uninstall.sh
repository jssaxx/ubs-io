
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
    
    rm -f /usr/lib64/libbio_interceptor_server.so
    rm -f /usr/lib64/libbio_sdk.a
    rm -f /usr/lib64/libbio_sdk.so
    rm -f /usr/lib64/libbio_sdk.so.1
    rm -f /usr/lib64/libbio_sdk.so.1.0.0
    rm -f /usr/lib64/libbio_server.so
    rm -f /usr/lib64/libbio_underfs.so
    rm -f /usr/lib64/libhcom.so
    rm -f /usr/lib64/libhcom.so.0
    rm -f /usr/lib64/libhcom.so.0.0.1
    rm -f /usr/lib64/libhcom_static.a
    rm -f /usr/lib64/libnds_file.so
    rm -f /usr/lib64/libock_interceptor.so
    rm -f /usr/lib64/libock_iofwd_proxy.so
    rm -f /usr/lib64/libubsio_kvc.so
    rm -f /usr/lib64/libubsio_kvc.so.1
    rm -f /usr/lib64/libubsio_kvc.so.1.0.0
    
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
