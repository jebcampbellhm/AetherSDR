# Web-888 transport prerequisites: local validation

Date: 2026-09-29. Base: `20d022d50566b7d5aa8fc85ddf66d00923c82117`.
Status: local changes, not submitted upstream. No hardware validation;
the operator's receiver is still in transit.

## Change and rationale

The existing millisecond-based session ID can collide when several clients
open together. RaspSDR/server `a6610195`, `rx/rx_server.cpp`, pairs streams
by timestamp and rejects duplicate same-type streams. The new allocator uses
a process-wide atomic counter shared by both receiver families. Its randomized
62-bit starting point reduces collisions between independent processes, but
is not a cross-process reservation guarantee. Bit 62 stays set, bit 63 stays
clear for the server's signed `%lld` parser, and exhaustion never wraps.

The new immutable `KiwiSdrClient::Streams::SoundOnly` option opens only SND;
Web-888 uses the firmware's `no_wf` path. Existing constructors still request
paired SND/W/F. The choice cannot change while connected: server partner
lifetimes mean closing a waterfall is not a safe way to detach a UI pan.

Session-generation checks also prevent a queued secure retry from reopening
transports after a replacement connection starts, and stop a cancelled sound
startup from opening its waterfall partner. The startup tests exercise both
cases through the production lifecycle, with transport opening injected.

No new backend, connector, slice allocator, speaker mixer or IQ path is
advertised by this change. Those remain separate plan milestones.

## Executed checks

Built `aethercore` and six targets with MSVC 19.44, Qt 6.8.3, CMake 3.31 and
Ninja on Windows. Local configuration disabled RADE, D-STAR and GPU spectrum;
this is not a release build or a full GUI build.

| Test | Result | Claim |
|---|---|---|
| `kiwi_session_id_test` | Pass | Same-tick-independent allocation, 80,000 allocations across eight threads, signed bounds and permanent exhaustion |
| `kiwi_sdr_session_test` | Pass | Paired IDs/paths, reconnect identity, family paths, TLS URL/Origin headers, sound-only availability, cancellation and obsolete queued retries |
| `kiwi_sdr_protocol_test` | Pass | Existing parser/formatting regression coverage |
| `kiwi_sdr_waterfall_scale_test` | Pass | Existing waterfall scale regression coverage |
| `kiwi_sdr_waterfall_setup_test` | Pass | Existing Web-888 setup replay and family isolation |
| `kiwi_sdr_waterfall_zoom_cap_test` | Pass | Existing zoom ceiling versus fixed-point geometry coverage |

Session tests override only HTTP preflight and transport opening; no fake
radio, socket listener or live device is involved. URL/header assertions do
not prove TLS handshake success or server admission.

The first test run needed the bundled FFTW DLL directory on PATH. Qt tests
also needed permission to create their isolated temporary settings directories.
With those runtime requirements met, all six passed.

The modified `KiwiSdrClient.cpp` also compiled with WebSockets disabled.
A separate temporary copy of the allocator was deliberately changed to reuse
the same ID; the real `kiwi_session_id_test` rejected it at the first repeated
allocation. Production source was not mutated. This demonstrates that the
duplicate-ID regression is detected, rather than merely compiling a test.

Engine-boundary findings remain at the baseline's 102 tracked warnings and
zero new blocking findings. Capability records remain at 71 booleans and
command-plane findings at 413 existing raw commands across 32 files. The
touchpoint manifest and test registration checks pass. Run Python checks in
UTF-8 mode on Windows (`python -X utf8`); the default encoding gave a false
stale-manifest result without source differences. No baseline was relaxed.

A separate configuration with `CMAKE_DISABLE_FIND_PACKAGE_Qt6WebSockets=TRUE`
exposed an existing engine build failure in unchanged `TciTrxMap.cpp`: the
`TciProtocol` namespace/functions are unavailable in that configuration. This
is not a successful full WebSockets-disabled build, and the disabled-build
runtime refusal test has not run. That shared TCI fix should be tracked
separately before claiming whole-project optional-dependency qualification.

## Reproduction

Configure using the repository's Windows build instructions and Qt 6.8.3.
Build the six targets above. Put the Qt `bin` and repository
`third_party/fftw3/bin` directories on PATH before running:

```text
ctest --test-dir build-web888 --output-on-failure --no-tests=error -R "^(kiwi_session_id_test|kiwi_sdr_session_test|kiwi_sdr_protocol_test|kiwi_sdr_waterfall_scale_test|kiwi_sdr_waterfall_setup_test|kiwi_sdr_waterfall_zoom_cap_test)$"
```

Linux/macOS builds, sanitizer runs, firmware admission, audio, RF geometry and
maximum device capacity are not established by these local results.
