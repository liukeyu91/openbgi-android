# Pinned Debian userspace and GPG-authenticated package snapshot.
# HTTP bootstraps ca-certificates in slim; APT still verifies signed indexes and package hashes.
FROM debian@sha256:44bccdd61bf09a081b1db8c61cf49bfabf30ac7afcc970010137c0ab587b209c
RUN printf '%s\n' 'Types: deb' 'URIs: http://snapshot.debian.org/archive/debian/20250301T000000Z/' 'Suites: bookworm' 'Components: main' 'Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg' 'Check-Valid-Until: no' > /etc/apt/sources.list.d/debian.sources \
 && apt-get update && apt-get install -y --no-install-recommends make clang pkg-config libx11-dev libxext-dev libfreetype6-dev libasound2-dev libfontconfig1-dev libvorbis-dev fonts-noto-cjk fonts-dejavu-core python3 cmake ninja-build curl unzip ca-certificates openjdk-17-jdk-headless git \
 && rm -rf /var/lib/apt/lists/*
ENV LANG=C.UTF-8 LC_ALL=C.UTF-8 TZ=UTC
WORKDIR /src
