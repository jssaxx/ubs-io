ARG BASE_IMAGE=hub.oepkgs.net/openeuler/openeuler:24.03-lts-sp3
FROM ${BASE_IMAGE}

ARG WORKSPACE_DIR=/workspace

USER root

# UBS IO-BoostIO development image. The source tree is mounted at runtime and
# remains outside the image so that the same image can build different commits.
RUN dnf install -y \
        autoconf \
        automake \
        cmake \
        dos2unix \
        fuse-devel \
        gcc \
        gcc-c++ \
        git \
        hostname \
        libaio-devel \
        libboundscheck \
        libcurl-devel \
        librados-devel \
        libtool \
        make \
        maven \
        numactl-devel \
        openssl-devel \
        rdma-core-devel \
    && dnf clean all \
    && rm -rf /var/cache/dnf

WORKDIR ${WORKSPACE_DIR}

# Keep this as a shell-based build/runtime environment; the caller chooses the
# UBS IO-BoostIO process explicitly.
ENTRYPOINT []
CMD ["/bin/bash"]
