# syntax=docker/dockerfile:1

# canonical: linux_toolchain -- all authoritative builds use this pinned userspace.
FROM --platform=linux/amd64 ubuntu@sha256:019e8eb29a85e74d64925745884f2ec79aa27e3feab36353d24656f4d6b89467

ARG DEBIAN_FRONTEND=noninteractive
ARG UBUNTU_SNAPSHOT=20260804T000000Z
ARG NODE_VERSION=22.23.1
ARG NODE_LINUX_X64_SHA256=9749e988f437343b7fa832c69ded82a312e41a03116d766797ac14f6f9eee578

ENV BLOB_ROYALE_TOOLCHAIN_ID=ubuntu-24.04-amd64-20260804 \
    BLOB_ROYALE_BASE_IMAGE_DIGEST=sha256:019e8eb29a85e74d64925745884f2ec79aa27e3feab36353d24656f4d6b89467

# Ubuntu's minimal image omits CA roots. Repository metadata remains archive-key
# signed while ca-certificates is bootstrapped, after which TLS verification is
# mandatory for every subsequent download.
RUN sed -i -E \
      "s#http://(archive|security).ubuntu.com/ubuntu/#https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT}/#" \
      /etc/apt/sources.list.d/ubuntu.sources \
    && apt-get -o Acquire::https::Verify-Peer=false update \
    && apt-get -o Acquire::https::Verify-Peer=false install -y --no-install-recommends \
      ca-certificates=20260601~24.04.1 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
      clang=1:18.0-59~exp2 \
      cmake=3.28.3-1build7 \
      curl=8.5.0-2ubuntu10.11 \
      g++=4:13.2.0-7ubuntu1 \
      libboost-json-dev=1.83.0.1ubuntu2 \
      libboost-log-dev=1.83.0.1ubuntu2 \
      libboost-program-options-dev=1.83.0.1ubuntu2 \
      libclang-rt-18-dev=1:18.1.3-1ubuntu1 \
      ninja-build=1.11.1-2 \
      xz-utils=5.6.1+really5.4.5-1ubuntu0.3 \
    && rm -rf /var/lib/apt/lists/*

RUN curl --fail --location --silent --show-error \
      "https://nodejs.org/download/release/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-x64.tar.xz" \
      --output /tmp/node-linux-x64.tar.xz \
    && echo "${NODE_LINUX_X64_SHA256}  /tmp/node-linux-x64.tar.xz" | sha256sum --check --strict \
    && tar --extract --xz --file /tmp/node-linux-x64.tar.xz --directory /usr/local --strip-components=1 \
    && rm /tmp/node-linux-x64.tar.xz \
    && node --version \
    && npm --version

WORKDIR /workspace

CMD ["/bin/bash"]
