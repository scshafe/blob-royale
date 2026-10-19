# syntax=docker/dockerfile:1

# canonical: stack_server_image -- the simulation server as the runner-lane stack runs it.
#
# Built by the host from this repository at the merged commit (deploy/stack/stack.toml). It is the
# same server `./scripts/verify-linux release` ships, built from source in one stage and run in a
# second one with its production configuration and maps baked in: nothing is bind-mounted, and
# changing a balance number is a commit. The browser client is the other image (deploy/web).
# Build:  docker build -t blob-royale .
# Run:    docker run --rm --network host --read-only blob-royale

FROM --platform=linux/amd64 ubuntu@sha256:019e8eb29a85e74d64925745884f2ec79aa27e3feab36353d24656f4d6b89467 AS build

ARG DEBIAN_FRONTEND=noninteractive
ARG UBUNTU_SNAPSHOT=20260804T000000Z

# The same snapshot and package versions as ci/linux/Dockerfile, minus the test and scan tooling.
RUN sed -i -E \
      "s#http://(archive|security).ubuntu.com/ubuntu/#https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT}/#" \
      /etc/apt/sources.list.d/ubuntu.sources \
    && apt-get -o Acquire::https::Verify-Peer=false update \
    && apt-get -o Acquire::https::Verify-Peer=false install -y --no-install-recommends \
      ca-certificates=20260601~24.04.1 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
      cmake=3.28.3-1build7 \
      g++=4:13.2.0-7ubuntu1 \
      libboost-json-dev=1.83.0.1ubuntu2 \
      ninja-build=1.11.1-2 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt CMakePresets.json main.cpp ./
COPY src src
COPY benchmarks benchmarks

# BUILD_TESTING=OFF is what linux-gcc-benchmark does too: no Catch2, no test targets.
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
      -DBUILD_TESTING=OFF \
    && cmake --build /build --target blob-royale --parallel 2 \
    && strip /build/blob-royale

FROM --platform=linux/amd64 ubuntu@sha256:019e8eb29a85e74d64925745884f2ec79aa27e3feab36353d24656f4d6b89467

ARG DEBIAN_FRONTEND=noninteractive
ARG UBUNTU_SNAPSHOT=20261001T000000Z

LABEL org.opencontainers.image.title="Blob Royale server" \
      org.opencontainers.image.description="Authoritative simulation server with its production configuration" \
      org.opencontainers.image.source="https://github.com/scshafe/blob-royale"

# The runtime packages of ci/linux/runtime.Dockerfile, at the same pins. No curl: the stack's
# healthcheck speaks HTTP through bash's /dev/tcp.
RUN sed -i -E \
      "s#http://(archive|security).ubuntu.com/ubuntu/#https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT}/#" \
      /etc/apt/sources.list.d/ubuntu.sources \
    && apt-get -o Acquire::https::Verify-Peer=false update \
    && apt-get -o Acquire::https::Verify-Peer=false install -y --no-install-recommends \
      ca-certificates=20260601~24.04.1 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
      libc6=2.39-0ubuntu8.9 \
      libgcc-s1=14.2.0-4ubuntu2~24.04.1 \
      libstdc++6=14.2.0-4ubuntu2~24.04.1 \
      libboost-json1.83.0=1.83.0-2.1ubuntu3.2 \
      libssl3t64=3.0.13-0ubuntu3.16 \
      openssl=3.0.13-0ubuntu3.16 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build --chown=0:0 --chmod=0555 /build/blob-royale /usr/local/bin/blob-royale
COPY --chown=0:0 --chmod=u=rX,go=rX deploy/config/blob-royale.cfg /etc/blob-royale/blob-royale.cfg
COPY --chown=0:0 --chmod=u=rX,go=rX maps /usr/share/blob-royale/maps
COPY --chown=0:0 --chmod=u=rX,go=rX THIRD_PARTY_NOTICES.md /usr/share/blob-royale/legal/THIRD_PARTY_NOTICES.md
COPY --chown=0:0 --chmod=u=rX,go=rX third_party/licenses /usr/share/blob-royale/legal/licenses

USER 65532:65532
EXPOSE 8000/tcp

ENTRYPOINT ["/usr/local/bin/blob-royale", "--config", "/etc/blob-royale/blob-royale.cfg"]
