ARG BASE_IMAGE=debian:trixie-slim
FROM ${BASE_IMAGE}

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get -o Acquire::Retries=5 update \
 && apt-get -o Acquire::Retries=5 install -y --no-install-recommends \
    bash \
    appstream \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    ffmpeg \
    fonts-noto-cjk \
    git \
    desktop-file-utils \
    libegl-dev \
    libfreetype6-dev \
    libwayland-dev \
    libx11-dev \
    libxkbcommon-dev \
    libchromaprint-tools \
    ninja-build \
    python3 \
    python3-mutagen \
    pkg-config \
    wayland-protocols \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/LoFiBox-Zero

CMD ["/bin/bash"]
