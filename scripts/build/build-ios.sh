#!/bin/bash
# Generate an Xcode project and build the experimental direct-radio iPad app.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TARGET="${1:-device}"
if [ "$#" -gt 0 ]; then shift; fi
case "$TARGET" in
    device) SDK=iphoneos; DEFAULT_ARCH=arm64; DESTINATION='generic/platform=iOS' ;;
    simulator) SDK=iphonesimulator; DEFAULT_ARCH="$(uname -m)"; DESTINATION='generic/platform=iOS Simulator' ;;
    *) echo "Usage: $0 [device|simulator] [additional CMake options]" >&2; exit 1 ;;
esac
if [ "$TARGET" = simulator ] && [ -n "${QT_SIMULATOR_ROOT:-}" ]; then
    QT_IOS_ROOT="$QT_SIMULATOR_ROOT"
fi
: "${QT_IOS_ROOT:?Set QT_IOS_ROOT to the Qt 6.12 installation for the selected SDK}"
: "${QT_HOST_ROOT:?Set QT_HOST_ROOT to the matching Qt 6.12 macOS installation}"
ARCH="${IOS_ARCH:-$DEFAULT_ARCH}"
DEPLOYMENT="${IOS_DEPLOYMENT_TARGET:-18.0}"
BUILD_SUFFIX="$TARGET"
if [ "$TARGET" = simulator ]; then BUILD_SUFFIX="$TARGET-$ARCH"; fi
BUILD_DIR="${IOS_BUILD_DIR:-$ROOT/build-ipad-$BUILD_SUFFIX}"
DEPS_WORK="${IOS_DEPS_WORK:-$ROOT/build-ios-deps/$SDK-$ARCH}"
DEPS_PREFIX="${IOS_DEPS_PREFIX:-$DEPS_WORK/install}"
CONFIGURATION="${IOS_CONFIGURATION:-Release}"
EXTRA_OPTIONS=(-DENABLE_ASR=ON)
if [ -n "${FFMPEG_DIR:-}" ]; then EXTRA_OPTIONS+=(-DFFMPEG_DIR="$FFMPEG_DIR"); fi

if [ ! -f "$DEPS_PREFIX/lib/libfftw3f.a" ] || [ ! -f "$DEPS_PREFIX/lib/libopus.a" ]; then
    IOS_SDK="$SDK" IOS_ARCH="$ARCH" IOS_DEPLOYMENT_TARGET="$DEPLOYMENT" \
        IOS_DEPS_WORK="$DEPS_WORK" IOS_DEPS_PREFIX="$DEPS_PREFIX" \
        bash "$ROOT/scripts/setup/setup-ios-deps.sh"
fi

# Also disable signing during CMake's initial compiler probes. A development
# build can override this cache entry and set a team through the trailing args.
"$QT_IOS_ROOT/bin/qt-cmake" -S "$ROOT" -B "$BUILD_DIR" -G Xcode \
    -DCMAKE_OSX_ARCHITECTURES="$ARCH" -DCMAKE_OSX_SYSROOT="$SDK" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT" \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DQT_HOST_PATH="$QT_HOST_ROOT" -DAETHER_IOS_DEPS="$DEPS_PREFIX" \
    "${EXTRA_OPTIONS[@]}" "$@"

if [ "${IOS_CONFIGURE_ONLY:-0}" != 1 ]; then
    xcodebuild -project "$BUILD_DIR/AetherSDR.xcodeproj" -scheme AetherSDR \
        -configuration "$CONFIGURATION" -destination "$DESTINATION" \
        -jobs "${BUILD_JOBS:-8}" build
fi
echo "Xcode project: $BUILD_DIR/AetherSDR.xcodeproj"
echo "App bundle: $BUILD_DIR/$CONFIGURATION-$SDK/AetherSDR.app"
