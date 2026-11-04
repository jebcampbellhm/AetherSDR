#!/bin/bash
# Build a native Apple Silicon simulator kit, separate from the device Qt kit.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
: "${QT_HOST_ROOT:?Set QT_HOST_ROOT to the matching Qt 6.12.0 macOS installation}"
WORK="${QT_SIMULATOR_WORK:-$ROOT/build-qt-ios-simulator}"
PREFIX="${QT_SIMULATOR_PREFIX:-$WORK/install}"
DEPLOYMENT="${IOS_DEPLOYMENT_TARGET:-18.0}"
JOBS="${BUILD_JOBS:-8}"
VERSION=6.12.0
mkdir -p "$WORK/sources" "$WORK/build" "$PREFIX"
WORK="$(cd "$WORK" && pwd)"
PREFIX="$(cd "$PREFIX" && pwd)"

fetch() {
    local name="$1" url="$2" expected="$3"
    local archive="$WORK/sources/$name"
    if [ ! -f "$archive" ]; then
        curl -fL --retry 3 "$url" -o "$archive.part"
        mv "$archive.part" "$archive"
    fi
    if [ "$(shasum -a 256 "$archive" | cut -d ' ' -f 1)" != "$expected" ]; then
        echo "Checksum mismatch: $archive" >&2
        exit 1
    fi
    if [ ! -d "$WORK/sources/${name%.tar.xz}" ]; then
        tar xf "$archive" -C "$WORK/sources"
    fi
}

while read -r module checksum; do
    archive="$module-everywhere-src-$VERSION.tar.xz"
    fetch "$archive" "https://download.qt.io/archive/qt/6.12/$VERSION/submodules/$archive" "$checksum"
done <<'SOURCES'
qtbase a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307
qtshadertools c7d84f436e1aaef39fdcadebf2bd71bdf24dc69497e13f2e0198873cbbfea2ab
qtsvg e4ab39534ec97987b1b9b60ef7f4d3253d5912a69f8c713a01753174a4029331
qt5compat 78cf1c283795312a7fa31c73eb7f7f556b48374adc484f10ad5a3c0bcb59264e
qtwebsockets 21690a365cd5fbfa8dea051473e98968df9f4eca0121fed7481d1215b7455a31
qtmultimedia 3143f53b64257ba2a0685c940f5dce476c75871ff86b516de45cfd54ea93a62d
SOURCES

# This version is referenced by Qt 6.12.0's FFmpeg attribution file.
fetch ffmpeg-9.0.1.tar.xz https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.xz \
    cf38e0e28c7e5605942c4a77755349b0145804a397af37eb1fb4c77cb237f635

cmake -S "$WORK/sources/qtbase-everywhere-src-$VERSION" -B "$WORK/build/qtbase" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_SYSTEM_NAME=iOS -DQT_APPLE_SDK=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT" \
    -DQT_HOST_PATH="$QT_HOST_ROOT" -DBUILD_SHARED_LIBS=OFF \
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF \
    -DFEATURE_sql=OFF -DFEATURE_dbus=OFF -DFEATURE_icu=OFF -DFEATURE_framework=OFF
cmake --build "$WORK/build/qtbase" --parallel "$JOBS"
cmake --install "$WORK/build/qtbase"

mkdir -p "$WORK/build/ffmpeg"
(
    cd "$WORK/build/ffmpeg"
    "$WORK/sources/ffmpeg-9.0.1/configure" --prefix="$WORK/ffmpeg" \
        --target-os=darwin --arch=arm64 --enable-cross-compile \
        --cc="$(xcrun --sdk iphonesimulator -f clang)" \
        --cxx="$(xcrun --sdk iphonesimulator -f clang++)" \
        --sysroot="$(xcrun --sdk iphonesimulator --show-sdk-path)" \
        --extra-cflags="-target arm64-apple-ios$DEPLOYMENT-simulator" \
        --extra-ldflags="-target arm64-apple-ios$DEPLOYMENT-simulator" \
        --enable-pic --enable-static --disable-shared --disable-programs \
        --disable-doc --disable-debug --disable-avdevice --disable-avfilter \
        --disable-autodetect --enable-audiotoolbox --enable-videotoolbox
    make -j"$JOBS"
    make install
)

for module in qtshadertools qtsvg qt5compat qtwebsockets qtmultimedia; do
    options=(-DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF)
    if [ "$module" = qtmultimedia ]; then
        options+=(-DFFMPEG_DIR="$WORK/ffmpeg" -DQT_DEPLOY_FFMPEG=OFF -DFEATURE_ffmpeg=ON)
    fi
    "$PREFIX/bin/qt-cmake" -S "$WORK/sources/$module-everywhere-src-$VERSION" \
        -B "$WORK/build/$module" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DQT_HOST_PATH="$QT_HOST_ROOT" \
        "${options[@]}"
    cmake --build "$WORK/build/$module" --parallel "$JOBS"
    cmake --install "$WORK/build/$module"
done

echo "Native simulator Qt: $PREFIX"
echo "Use QT_IOS_ROOT=$PREFIX IOS_ARCH=arm64 and -DFFMPEG_DIR=$WORK/ffmpeg when building AetherSDR."
