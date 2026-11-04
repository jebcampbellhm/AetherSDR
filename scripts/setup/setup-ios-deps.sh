#!/bin/bash
# Build pinned FFTW and Opus libraries for one iOS SDK/architecture. Keep device
# and simulator prefixes separate: arm64 alone does not identify a platform.
set -euo pipefail
SDK="${IOS_SDK:-iphoneos}"
ARCH="${IOS_ARCH:-arm64}"
DEPLOYMENT="${IOS_DEPLOYMENT_TARGET:-18.0}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="${IOS_DEPS_WORK:-$ROOT/build-ios-deps/$SDK-$ARCH}"
PREFIX="${IOS_DEPS_PREFIX:-$WORK/install}"
VERSION=3.3.11
SHA=5630c24cdeb33b131612f7eb4b1a9934234754f9f388ff8617458d0be6f239a1
case "$SDK" in iphoneos|iphonesimulator) ;; *) echo "Unsupported SDK: $SDK" >&2; exit 1;; esac
case "$ARCH" in arm64|x86_64) ;; *) echo "Unsupported architecture: $ARCH" >&2; exit 1;; esac
mkdir -p "$WORK" "$PREFIX"
ARCHIVE="$WORK/fftw-$VERSION.tar.gz"
if [ ! -f "$ARCHIVE" ]; then
    curl -fL --retry 3 "https://fftw.org/fftw-$VERSION.tar.gz" -o "$ARCHIVE"
fi
ACTUAL="$(shasum -a 256 "$ARCHIVE" | cut -d ' ' -f 1)"
if [ "$ACTUAL" != "$SHA" ]; then
    echo "FFTW archive checksum mismatch" >&2
    exit 1
fi
if [ ! -d "$WORK/fftw-$VERSION" ]; then
    tar xzf "$ARCHIVE" -C "$WORK"
fi
for PRECISION in double single; do
    FLOAT=OFF
    if [ "$PRECISION" = single ]; then FLOAT=ON; fi
    cmake -S "$WORK/fftw-$VERSION" -B "$WORK/$PRECISION" -G Ninja \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SDK" \
        -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF \
        -DENABLE_FLOAT="$FLOAT" -DDISABLE_FORTRAN=ON
    cmake --build "$WORK/$PRECISION" --parallel "${BUILD_JOBS:-8}"
    cmake --install "$WORK/$PRECISION"
done

# Same source/hash as setup-opus.ps1; enables compressed network RX audio.
OPUS_VERSION=1.5.2
OPUS_SHA=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
OPUS_ARCHIVE="$WORK/opus-$OPUS_VERSION.tar.gz"
if [ ! -f "$OPUS_ARCHIVE" ]; then
    curl -fL --retry 3 "https://downloads.xiph.org/releases/opus/opus-$OPUS_VERSION.tar.gz" -o "$OPUS_ARCHIVE"
fi
if [ "$(shasum -a 256 "$OPUS_ARCHIVE" | cut -d ' ' -f 1)" != "$OPUS_SHA" ]; then
    echo "Opus archive checksum mismatch" >&2
    exit 1
fi
if [ ! -d "$WORK/opus-$OPUS_VERSION" ]; then
    tar xzf "$OPUS_ARCHIVE" -C "$WORK"
fi
cmake -S "$WORK/opus-$OPUS_VERSION" -B "$WORK/opus" -G Ninja \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SDK" \
    -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF -DOPUS_BUILD_TESTING=OFF \
    -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF
cmake --build "$WORK/opus" --parallel "${BUILD_JOBS:-8}"
cmake --install "$WORK/opus"
echo "iOS DSP dependencies: $PREFIX"
