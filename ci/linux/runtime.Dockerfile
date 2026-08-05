# syntax=docker/dockerfile:1

# canonical: linux_runtime_image -- minimal shipped userspace, separate from build/test tooling.
FROM --platform=linux/amd64 ubuntu@sha256:019e8eb29a85e74d64925745884f2ec79aa27e3feab36353d24656f4d6b89467

ARG DEBIAN_FRONTEND=noninteractive
ARG UBUNTU_SNAPSHOT=20260804T000000Z
ARG BLOB_ROYALE_RELEASE_ID

LABEL org.opencontainers.image.title="Blob Royale server" \
      org.opencontainers.image.description="Read-only deterministic simulation server" \
      org.opencontainers.image.version="${BLOB_ROYALE_RELEASE_ID}"

RUN sed -i -E \
      "s#http://(archive|security).ubuntu.com/ubuntu/#https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT}/#" \
      /etc/apt/sources.list.d/ubuntu.sources \
    && apt-get -o Acquire::https::Verify-Peer=false update \
    && apt-get -o Acquire::https::Verify-Peer=false install -y --no-install-recommends \
      ca-certificates=20260601~24.04.1 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
      libc6=2.39-0ubuntu8.8 \
      libgcc-s1=14.2.0-4ubuntu2~24.04.1 \
      libstdc++6=14.2.0-4ubuntu2~24.04.1 \
      libboost-json1.83.0=1.83.0-2.1ubuntu3.2 \
    && rm -rf /var/lib/apt/lists/*

RUN test -n "${BLOB_ROYALE_RELEASE_ID}"

# Release assembly makes every project file non-writable. The image deliberately owns them as
# root, while the process runs as an unprivileged numeric identity with no write path into them.
COPY --chown=0:0 blob-royale /usr/local/bin/blob-royale
COPY --chown=0:0 examples /usr/share/blob-royale/examples
COPY --chown=0:0 legal /usr/share/blob-royale/legal

USER 65532:65532
EXPOSE 8000/tcp

ENTRYPOINT ["/usr/local/bin/blob-royale"]
