#!/bin/bash
# Build QtKeychain against the same Qt and SDK as the iPad app.
set -euo pipefail
: "${QT_IOS_ROOT:?Set QT_IOS_ROOT to the Qt installation for the selected SDK}"
: "${QT_HOST_ROOT:?Set QT_HOST_ROOT to the matching macOS Qt installation}"
: "${IOS_DEPS_PREFIX:?Set IOS_DEPS_PREFIX to the dependency prefix for the selected SDK}"
SDK="${IOS_SDK:-iphoneos}"
ARCH="${IOS_ARCH:-arm64}"
DEPLOYMENT="${IOS_DEPLOYMENT_TARGET:-18.0}"
VERSION=0.16.0
COMMIT=aa6da344e1a20b9194e12bace3665caeea6b6304
PATCH="$(cd "$(dirname "$0")" && pwd)/qtkeychain-ios-qos.patch"
WORK="${IOS_DEPS_WORK:-$IOS_DEPS_PREFIX/build}/qtkeychain"
mkdir -p "$WORK"
if [ ! -d "$WORK/src/.git" ]; then
    git clone --depth 1 --branch "$VERSION" https://github.com/frankosterfeld/qtkeychain.git "$WORK/src"
fi
if [ "$(git -C "$WORK/src" rev-parse HEAD)" != "$COMMIT" ]; then
    echo "QtKeychain source does not match the pinned $VERSION commit" >&2
    exit 1
fi
# Connecting waits for a Keychain result. The default background queue can
# delay it long enough to make the password look missing.
if ! git -C "$WORK/src" apply --reverse --check "$PATCH" 2>/dev/null; then
    git -C "$WORK/src" apply "$PATCH"
fi
cmake -S "$WORK/src" -B "$WORK/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$QT_IOS_ROOT/lib/cmake/Qt6/qt.toolchain.cmake" \
    -DQT_HOST_PATH="$QT_HOST_ROOT" -DCMAKE_OSX_SYSROOT="$SDK" \
    -DCMAKE_OSX_ARCHITECTURES="$ARCH" -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$IOS_DEPS_PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_WITH_QT6=ON -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TRANSLATIONS=OFF -DBUILD_TESTING=OFF
cmake --build "$WORK/build" --parallel "${BUILD_JOBS:-8}"
cmake --install "$WORK/build"
touch "$IOS_DEPS_PREFIX/lib/cmake/Qt6Keychain/.aether-ios-interactive-qos"
