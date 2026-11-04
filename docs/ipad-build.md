# Experimental iPad build

This target runs AetherSDR's radio engine inside an iPad app. It connects
directly to network radios. A hosted AetherSDR display client is separate
future work. The target is for local development, not an App Store release.

## Build requirements

- A Mac with full Xcode and the iOS SDK selected by `xcode-select`.
- CMake 3.25 or later, Ninja, Python, and Qt 6.12.0 for **both** iOS and macOS.
- Qt modules: Multimedia, WebSockets, ShaderTools, Core5Compat and their
  dependencies. The macOS kit supplies the host tools used while cross-compiling.
- iPadOS 18 or later. The device build is arm64.
- For accelerated speech, install the offline shader compiler with
  `xcodebuild -downloadComponent MetalToolchain`.

For example, `aqtinstall` can install the matching Qt kits:

```sh
python3 -m aqt install-qt mac desktop 6.12.0 clang_64 \
  -O "$PWD/build-tools/Qt" -m qtmultimedia qtwebsockets qtshadertools qt5compat
python3 -m aqt install-qt mac ios 6.12.0 ios \
  -O "$PWD/build-tools/Qt" -m qtmultimedia qtwebsockets qtshadertools qt5compat
```

The setup script fetches the repository's pinned FFTW 3.3.11 and Opus 1.5.2
sources, verifies SHA-256, and builds static libraries for the chosen SDK.
Device and simulator dependency prefixes must remain separate even if both
use arm64. It does not use Homebrew's macOS libraries for an iOS link.

## Generate and build with Xcode

```sh
export QT_IOS_ROOT="$PWD/build-tools/Qt/6.12.0/ios"
export QT_HOST_ROOT="$PWD/build-tools/Qt/6.12.0/macos"
bash scripts/build/build-ios.sh device
open build-ipad-device/AetherSDR.xcodeproj
```

Select the **AetherSDR** scheme. The default build is unsigned; its result is
`build-ipad-device/Release-iphoneos/AetherSDR.app`. The script enables Copy
Assist with Whisper and Metal. Pass `-DENABLE_ASR=OFF` for a smaller base build,
or `-DENABLE_ASR_METAL=OFF` to build CPU speech only.

Useful environment overrides:

| Variable | Purpose |
| --- | --- |
| `IOS_BUILD_DIR` | Xcode project and output directory |
| `IOS_DEPS_WORK` | Dependency source and build directory |
| `IOS_DEPS_PREFIX` | SDK-specific installed FFTW and Opus prefix |
| `IOS_ARCH` | Device `arm64`; simulator architecture matching the Qt kit |
| `QT_SIMULATOR_ROOT` | Simulator-specific Qt kit, overriding `QT_IOS_ROOT` for simulator builds |
| `FFMPEG_DIR` | Source-built FFmpeg prefix when the Qt kit links static FFmpeg |
| `IOS_CONFIGURATION` | `Release` by default |
| `BUILD_JOBS` | Compiler concurrency, default 8 |
| `IOS_CONFIGURE_ONLY=1` | Generate the project without starting a build |

Do not reuse one CMake build directory for different SDKs, architectures or Qt
kits. Simulator build directories include their architecture by default.

## Run on an iPad

Connect and unlock an iPad, enable Developer Mode, and select it in Xcode.
Configure signing using your own Apple development team and unique bundle ID:

```sh
IOS_CONFIGURE_ONLY=1 bash scripts/build/build-ios.sh device \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=YES \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE=Automatic \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=YOUR_TEAM_ID \
  -DAETHER_IOS_BUNDLE_ID=com.yourname.aethersdr
```

The default bundle identifier is `com.aethersdr.ipad`; the example overrides
it for your provisioning profile. Choose **Product → Run**. Allow **Local Network** access, then use
**Radio → Connect by IP** with the radio's address and radio type.

Automatic discovery uses UDP broadcast/multicast. Apple requires an approved
`com.apple.developer.networking.multicast` entitlement on physical iPads.
Once your development profile has it, configure with
`-DAETHER_IOS_MULTICAST_DISCOVERY=ON`; this uses
`packaging/ios/Discovery.entitlements`. The default direct-IP build does not
request that restricted entitlement. See Apple's
[local network privacy guide](https://developer.apple.com/documentation/technotes/tn3179-understanding-local-network-privacy).

No physical-radio or transmit certification is implied by a successful build.
Keep the app in the foreground; background radio/audio operation is not part
of this target.

### Code signing and Finder metadata

A file-provider-managed Documents folder can add Finder metadata to generated
bundles. Apple code signing rejects Finder-info and resource-fork attributes;
see [Apple QA1940](https://developer.apple.com/library/archive/qa/qa1940/_index.html).
Prefer a build directory outside a synced folder when signing. If this error
occurs, remove those attributes from the generated app and retry signing:

```sh
xattr -dr com.apple.FinderInfo build-ipad-device/Release-iphoneos/AetherSDR.app
xattr -dr com.apple.ResourceFork build-ipad-device/Release-iphoneos/AetherSDR.app
```

An ad-hoc signing and strict verification check passed on a cleaned copy of
this build. Installing on an iPad still requires the development-team signing
and provisioning described above.

## Run in Simulator

On Apple Silicon, build the native **arm64 simulator** kit. The setup script
builds Qt 6.12.0 Base, ShaderTools, SVG, Core5Compat, WebSockets and Multimedia,
plus static FFmpeg 9.0.1, from checksum-pinned source archives. Qt's matching
macOS kit supplies the host tools. The device kit remains separate.

```sh
export QT_HOST_ROOT="$PWD/build-tools/Qt/6.12.0/macos"
export QT_SIMULATOR_WORK="$PWD/build-qt-ios-simulator"
export QT_SIMULATOR_PREFIX="$QT_SIMULATOR_WORK/install"
bash scripts/setup/setup-qt-ios-simulator.sh
export QT_SIMULATOR_ROOT="$QT_SIMULATOR_PREFIX"
export FFMPEG_DIR="$QT_SIMULATOR_WORK/ffmpeg"
bash scripts/build/build-ios.sh simulator
open build-ipad-simulator-arm64/AetherSDR.xcodeproj
xcrun simctl install booted build-ipad-simulator-arm64/Release-iphonesimulator/AetherSDR.app
xcrun simctl launch booted com.aethersdr.ipad
```

The build helper defaults to the Mac's native architecture and builds matching
FFTW and Opus libraries. Native ARM64 simulator builds work with ARM64-only
iOS runtimes and do not use Rosetta. ARM64 device libraries cannot substitute
for ARM64 simulator libraries; they target different platforms.

The downloadable Qt iOS kit has an x86_64 simulator slice. To keep using that
kit explicitly, unset `QT_SIMULATOR_ROOT` and `FFMPEG_DIR`, set `QT_IOS_ROOT`
to that kit, and use `IOS_ARCH=x86_64`. That optional path needs a universal
runtime and Rosetta on Apple Silicon.

Use **Radio → On This Network → AetherSDR Demo** to check the UI and local
audio pipeline without a radio. For semantic UI tests, launch with
`SIMCTL_CHILD_AETHER_AUTOMATION=1` and `SIMCTL_CHILD_AETHER_AUTOMATION_NO_TX=1`.
The existing automation bridge exposes widget state, receiver state and DSP
readbacks; see [automation-bridge.md](automation-bridge.md).

## Add-ons

| Add-on | iPad integration |
| --- | --- |
| NR2 | Shared FFTW-based receive DSP |
| RN2 | Bundled RNNoise, CPU inference |
| NR4 | Bundled libspecbleach with iOS FFTW float library |
| NNR | Bundled WDSP neural models, CPU inference |
| MNR | Shared MMSE-Wiener implementation using Apple Accelerate |
| Copy Assist / Whisper | Local CPU and Metal backends; model download on demand |
| Opus | Static iOS codec for compressed network audio |
| ONNX VAD, speaker labels, sherpa speech models | Disabled; need an iOS ONNX/sherpa dependency and packaging port |
| DFNR / DeepFilterNet | Disabled; existing prebuilt libraries target desktop platforms |
| NVIDIA BNR | No iPad NVIDIA runtime |
| D-STAR / ThumbDV helper | Disabled; iOS does not support the desktop helper-process/serial design |
| USB RTL-SDR, CAT pseudo-terminals, DAX virtual driver | Desktop integration is unavailable on iOS |
| RADE, MQTT | Disabled in this initial target; not yet validated on iPad |

Whisper models remain in the app sandbox. Start evaluation with Tiny or Base;
actual latency, thermals and battery use must be measured on the destination
iPad. Simulator performance is not an iPad benchmark. The default speech
backend processes received audio locally after its model has downloaded.

The tablet UI is enabled automatically on iOS. A desktop build can preview it
with `AETHER_TABLET_UI=1`; this does not emulate iOS networking or lifecycle.

## Validation and known limits

Both the arm64 device target and native arm64 Simulator target build with Xcode.
The native simulator kit and DSP dependencies contain 65 ARM64-only static
archives. The running app reports ARM64 on iOS Simulator; it does not use
Rosetta. Native validation uses the initialized iOS 26.4 simulator; a fresh
iOS 26.5 simulator stalled in Apple's CoreLocation migration during first boot.
The native simulator has exercised the demo connection, live Metal spectrum
and waterfall, tablet controls, three reconnects, NR2/RN2/NR4/MNR/NNR audio
processing, and local Whisper Tiny speech recognition using the bundled demo
recording. A 60-second Tiny check with NR4 active showed a growing transcript
and zero backlog at all seven samples.

The earlier x86_64 simulator also exercised Whisper Base. Base fell behind continuous
speech under Rosetta. Its seven-minute Tiny follow-up kept NR4 active, with an
observed queue of 0–1.8 seconds, ending at 0.5 seconds. This is a functional
check, not a physical-iPad performance or transcription-accuracy benchmark.
Whisper rejects the simulator's
virtual GPU at its capability probe and uses CPU inference. A compiled device
Metal backend is not evidence of its speed or reliability on physical hardware.

Qt 6.12.0 has an iOS accessibility destruction bug that can crash when receive
filter buttons are rebuilt. `IOSAccessibility.cpp` installs a version-scoped
workaround that preserves live accessibility updates and invalidates the iOS
cache using a surviving application interface. Its regression test exercises
button destruction and name-change events. The Qt 6.12 development branch
handles destruction separately; re-evaluate the workaround when updating Qt.

Physical iPad signing, direct-radio audio, microphone/transmit behavior,
multicast discovery, multitasking, thermal load and battery use remain device
validation tasks. Desktop-only integrations listed above remain unavailable.
The main toolbar has also been checked in a desktop tablet preview at landscape
and narrow widths. Legacy spectrum overlays and advanced setup pages still
need adaptation for narrow multitasking windows. The demo backend does not
echo tuning-step changes; validate that control against the intended radio.
NR2 performs its existing FFTW optimization on first use, which can take
several minutes, especially in a translated simulator.

### Direct Web-888 and nRSP-ST integration

The iPad work incorporates the radio feature delta from
`integration/web888-nrsp` at `4b9bdfdb653a6872956a356a54e3aa060d8d5225`,
relative to its upstream `9efd0fb0` base. The iPad branch also includes upstream
`main` through `231895b0` (26.11.1), with the tablet port retained.

The nRSP-ST receive transport uses the bundled zstd 1.5.7 decompressor and
Apple Security for RSA verification on both macOS and iPadOS. The native
receivers retain their typed pan center/bandwidth controls.
Web-888's external decoder process wrapper explicitly reports unavailable on
iPadOS; receiving, native DSP and capture planning remain compiled in.
Credentials remain session-only when QtKeychain is disabled.

Qt 6.12's iOS FFmpeg finder appends an xcframework suffix to an already cached
static archive during reconfiguration. The app clears only cached static
FFmpeg archive lookup entries before Qt discovery, allowing repeat native
simulator builds while preserving the official device kit's xcframeworks.

Desktop update helper execution is unavailable on iPadOS. The shared Linux
and macOS update helpers fail closed when Qt process support is absent;
iPad builds are installed through Xcode signing and device provisioning.

After an Xcode SDK upgrade, reconfigure in a fresh build directory or clear
cached CMake library/include paths that reference the removed SDK before
building. The Qt and native dependency prefixes remain architecture-specific.
