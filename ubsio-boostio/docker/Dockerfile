ARG BASE_IMAGE=quay.io/ascend/vllm-ascend:v0.23.0-openeuler
FROM ${BASE_IMAGE}

ARG WORKSPACE_DIR=/workspace

USER root

# UBS IO-BoostIO development image. The source tree is mounted at runtime and
# remains outside the image so that the same image can build different commits.
RUN dnf install -y \
        autoconf \
        automake \
        dos2unix \
        fuse-devel \
        hostname \
        libaio-devel \
        libboundscheck \
        libcurl-devel \
        librados-devel \
        libtool \
        maven \
        openssl-devel \
    && dnf clean all \
    && rm -rf /var/cache/dnf

WORKDIR ${WORKSPACE_DIR}

# Do not inherit a vLLM service entrypoint: this image is used as the BoostIO
# build/runtime environment and the caller chooses the process explicitly.
ENTRYPOINT []
CMD ["/bin/bash"]
