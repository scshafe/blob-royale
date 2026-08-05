# syntax=docker/dockerfile:1

# canonical: linux_toolchain -- all authoritative builds use this pinned userspace.
FROM --platform=linux/amd64 ubuntu@sha256:019e8eb29a85e74d64925745884f2ec79aa27e3feab36353d24656f4d6b89467

ARG DEBIAN_FRONTEND=noninteractive
ARG UBUNTU_SNAPSHOT=20260804T000000Z
ARG NODE_VERSION=22.23.1
ARG NODE_LINUX_X64_SHA256=9749e988f437343b7fa832c69ded82a312e41a03116d766797ac14f6f9eee578
ARG CATCH2_VERSION=3.15.3
ARG CATCH2_COMMIT=8b08d4d79514f45f7e4ce2a607ac9c94e920d1bb
ARG CATCH2_ARCHIVE_SHA256=6789b4711430fe4fba8b5efe609038bf52c81c0cbdcdedf9b5d116794dfd5f2e
ARG SYFT_VERSION=1.44.0
ARG SYFT_LINUX_AMD64_SHA256=0e91737aee2b5baf1d255b959630194a302335d848ff97bb07921eb6205b5f5a
ARG GRYPE_VERSION=0.112.0
ARG GRYPE_LINUX_AMD64_SHA256=acb14a030010fe9bdb9594b4ae108d9d14ef2f926d936aa0916dc62c89c058ea

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
      clang-format=1:18.0-59~exp2 \
      clang-format-18=1:18.1.3-1ubuntu1 \
      clang-tidy=1:18.0-59~exp2 \
      clang-tidy-18=1:18.1.3-1ubuntu1 \
      cmake=3.28.3-1build7 \
      curl=8.5.0-2ubuntu10.11 \
      g++=4:13.2.0-7ubuntu1 \
      libboost-json-dev=1.83.0.1ubuntu2 \
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

# Release dependency evidence uses checksum-pinned scanners from their immutable upstream
# releases. Scanner databases remain a separately cached, deliberately fresh CI input.
RUN curl --fail --location --silent --show-error \
      "https://github.com/anchore/syft/releases/download/v${SYFT_VERSION}/syft_${SYFT_VERSION}_linux_amd64.tar.gz" \
      --output /tmp/syft-linux-amd64.tar.gz \
    && echo "${SYFT_LINUX_AMD64_SHA256}  /tmp/syft-linux-amd64.tar.gz" \
      | sha256sum --check --strict \
    && tar --extract --gzip --file /tmp/syft-linux-amd64.tar.gz \
      --directory /usr/local/bin syft \
    && curl --fail --location --silent --show-error \
      "https://github.com/anchore/grype/releases/download/v${GRYPE_VERSION}/grype_${GRYPE_VERSION}_linux_amd64.tar.gz" \
      --output /tmp/grype-linux-amd64.tar.gz \
    && echo "${GRYPE_LINUX_AMD64_SHA256}  /tmp/grype-linux-amd64.tar.gz" \
      | sha256sum --check --strict \
    && tar --extract --gzip --file /tmp/grype-linux-amd64.tar.gz \
      --directory /usr/local/bin grype \
    && rm /tmp/syft-linux-amd64.tar.gz /tmp/grype-linux-amd64.tar.gz \
    && syft version \
    && grype version

# Catch2 is fetched, verified, and compiled once in this cacheable image layer.
# Project presets consume only the installed package, so project configuration
# never accesses the network or recompiles third-party sources.
RUN install -d \
      /opt/blob-royale-deps/catch2 \
      /opt/blob-royale-deps/catch2-install \
    && curl --fail --location --silent --show-error \
      "https://github.com/catchorg/Catch2/archive/${CATCH2_COMMIT}.tar.gz" \
      --output /tmp/catch2.tar.gz \
    && echo "${CATCH2_ARCHIVE_SHA256}  /tmp/catch2.tar.gz" | sha256sum --check --strict \
    && tar --extract --gzip --file /tmp/catch2.tar.gz \
      --directory /opt/blob-royale-deps/catch2 --strip-components=1 \
    && printf '%s\n' "${CATCH2_VERSION}" \
      > /opt/blob-royale-deps/catch2/.blob-royale-source-version \
    && printf '%s\n' "${CATCH2_COMMIT}" \
      > /opt/blob-royale-deps/catch2/.blob-royale-source-commit \
    && printf '%s\n' "${CATCH2_ARCHIVE_SHA256}" \
      > /opt/blob-royale-deps/catch2/.blob-royale-source-sha256 \
    && cmake \
      -S /opt/blob-royale-deps/catch2 \
      -B /tmp/catch2-build \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
      -DCMAKE_INSTALL_PREFIX=/opt/blob-royale-deps/catch2-install \
      -DCMAKE_INSTALL_LIBDIR=lib \
      -DBUILD_TESTING=OFF \
      -DCATCH_DEVELOPMENT_BUILD=OFF \
      -DCATCH_INSTALL_DOCS=OFF \
      -DCATCH_INSTALL_EXTRAS=ON \
    && cmake --build /tmp/catch2-build --parallel 1 \
    && cmake --install /tmp/catch2-build \
    && test -r /opt/blob-royale-deps/catch2-install/lib/cmake/Catch2/Catch2Config.cmake \
    && test -r /opt/blob-royale-deps/catch2-install/lib/cmake/Catch2/Catch.cmake \
    && rm -rf /tmp/catch2-build /tmp/catch2.tar.gz

# These names are the Chromium dependency closure declared by exact
# @playwright/test 1.62.1 for Ubuntu 24.04. The immutable Ubuntu snapshot pins
# their resolved versions; browser bytes remain a separately cached download.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
      fonts-freefont-ttf=20211204+svn4273-2 \
      fonts-ipafont-gothic=00303-21ubuntu1 \
      fonts-liberation=1:2.1.5-3 \
      fonts-noto-color-emoji=2.047-0ubuntu0.24.04.1 \
      fonts-tlwg-loma-otf=1:0.7.3-1 \
      fonts-unifont=1:15.1.01-1build1 \
      fonts-wqy-zenhei=0.9.45-8 \
      libasound2t64=1.2.11-1ubuntu0.3 \
      libatk-bridge2.0-0t64=2.52.0-1build1 \
      libatk1.0-0t64=2.52.0-1build1 \
      libatspi2.0-0t64=2.52.0-1build1 \
      libcairo2=1.18.0-3build1 \
      libcups2t64=2.4.7-1.2ubuntu7.14 \
      libdbus-1-3=1.14.10-4ubuntu4.1 \
      libdrm2=2.4.125-1ubuntu0.1~24.04.2 \
      libfontconfig1=2.15.0-1.1ubuntu2 \
      libfreetype6=2.13.2+dfsg-1ubuntu0.1 \
      libgbm1=25.2.8-0ubuntu0.24.04.2 \
      libglib2.0-0t64=2.80.0-6ubuntu3.8 \
      libnspr4=2:4.35-1.1build1 \
      libnss3=2:3.98-1ubuntu0.2 \
      libpango-1.0-0=1.52.1+ds-1build1 \
      libx11-6=2:1.8.7-1build1 \
      libxcb1=1.15-1ubuntu2 \
      libxcomposite1=1:0.4.5-1build3 \
      libxdamage1=1:1.1.6-1build1 \
      libxext6=2:1.3.4-1build2 \
      libxfixes3=1:6.0.0-2build1 \
      libxkbcommon0=1.6.0-1build1 \
      libxrandr2=2:1.5.2-2build1 \
      xfonts-cyrillic=1:1.0.5+nmu1 \
      xfonts-scalable=1:1.0.3-1.3 \
      xvfb=2:21.1.12-1ubuntu1.6 \
    && rm -rf /var/lib/apt/lists/*

# Git defines the tracked-plus-unignored source set used by deterministic release evidence. Keep
# it after the expensive compiler/test/browser layers so changes to evidence tooling do not force
# third-party rebuilds.
RUN apt-get update \
    && apt-get install -y --no-install-recommends git=1:2.43.0-1ubuntu7.3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["/bin/bash"]
