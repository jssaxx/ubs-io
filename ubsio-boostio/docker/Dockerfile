ARG BASE_IMAGE=quay.io/ascend/vllm-ascend:v0.23.0-openeuler
FROM ${BASE_IMAGE}

ARG WORKSPACE_DIR=/workspace

USER root

# UBSIO-BoostIO development image. The source tree is mounted at runtime and
# remains outside the image so that the same image can build different commits.
RUN dnf install -y \
        autoconf \
        automake \
        ca-certificates \
        cmake \
        dos2unix \
        findutils \
        fuse-devel \
        gcc \
        gcc-c++ \
        git \
        gzip \
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
        procps-ng \
        rdma-core-devel \
        tar \
        time \
    && dnf clean all \
    && rm -rf /var/cache/dnf

WORKDIR ${WORKSPACE_DIR}

# Do not inherit a vLLM service entrypoint: this image is used as the BoostIO
# build/runtime environment and the caller chooses the process explicitly.
ENTRYPOINT []
CMD ["/bin/bash"]
