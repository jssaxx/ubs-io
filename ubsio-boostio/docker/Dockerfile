ARG BASE_IMAGE=swr.cn-north-4.myhuaweicloud.com/ubscore/ubs-io:oe2403-sp3-multiarch
FROM ${BASE_IMAGE}

ARG WORKSPACE_DIR=/workspace

USER root

# The pinned base image already provides every direct UBS IO-BoostIO build and
# runtime dependency. Do not reinstall packages or create an extra DNF layer.

WORKDIR ${WORKSPACE_DIR}

# The caller explicitly chooses the build command or UBS IO-BoostIO process.
ENTRYPOINT []
CMD ["/bin/bash"]
