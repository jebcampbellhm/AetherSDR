# Standalone Web-888 continuation handoff

> Historical originating-workstation handoff. On 2026-10-19 this branch was
> merged into `web888-native`, preserving two hardware leases, four shared SSB
> listeners and digital monitoring. Single-pan and mode-limit descriptions
> below describe the original branch, not the integrated backend.

## Scope and starting point

Continue on `fix/web888-narrow-zoom` in the `jebcampbellhm/AetherSDR` fork.
The implementation starts from upstream commit
`7864ef544ffbb465bb2136438d748f9da58aa30c`. This is an operator-authorized
fork implementation prepared for review; it does not imply upstream approval
of the new backend or its UI. No upstream PR is part of this handoff.

The main addition is a standalone Web-888 connection. Stock support used a
KiwiSDR/Web-888 profile as an external receive antenna on another radio's
slice. The existing external-profile path remains available.

## Implementation map

- `src/core/backends/web888/Web888Backend.{h,cpp}`: direct receive-only backend,
  one slice/pan, 24 kHz audio, AM/SAM/USB/LSB/CW/FM, tuning through 30.72 MHz,
  firmware identity and metering. Speaker gain/mute/pan are independent of
  live per-slice decoder export. Connection cancellation uses generation
  fencing and a bounded timeout; stale callbacks cannot revive a connection.
- `src/gui/ConnectionPanel.{h,cpp}`: separate Web-888 radio choice, numeric
  IPv4/IPv6 address, port 8073 by default, masked password, endpoint-specific
  Forget action, recent endpoint retention and truthful unavailable-control
  explanations. The final port editor is covered by a geometry regression.
- `src/core/NetworkReceiverCredentials.{h,cpp}` and
  `SettingsCredentialPolicy.h`: endpoint-bound session cache and existing OS
  keychain integration. Only accepted credentials are saved. Forget and newer
  credential edits supersede old pending replies. Passwords are not stored in
  radio profiles, exported settings, screenshots or this repository.
- `src/core/KiwiSdrClient.{h,cpp}`: Web-888 firmware can return constant rows
  at high zoom. After 20 matching flat rows, lower a session-only request
  ceiling and crop valid wider data into the unchanged viewport. Advertised
  zoom still controls wire decoding/frequency scaling. Reconnection resets
  recovery; ordinary Kiwi profiles are unaffected. This does not create extra
  FFT resolution. `KiwiSdrProtocol` also formats IPv6 socket/Origin authorities.
- `RadioCapabilities`, `AudioEngine` and `MainWindow_KiwiSdr.cpp`: optional
  generic playback policy requests a bounded 360 ms startup/refill reserve
  for Web-888. Other backends retain their existing policy. The decoder export
  remains live before the speaker reserve. Common output pan is centered for
  backend-owned speaker panning to avoid applying it twice.
- `src/gui/DisplaySessionMonitor.{h,cpp}` and `main.cpp`: Windows-only WTS
  message-window integration suspends top-level widget painting while locked,
  preserves receiver/control processing, and restores eligible surviving
  windows on unlock. Non-Windows/offscreen paths do not register a watcher.
- User setup, capability/architecture documentation, bridge docs and the
  public-safe connection screenshot are included with the implementation.

## Validation already performed

The final Windows MSVC/Qt 6.12 RelWithDebInfo build passed. Before this handoff,
the production source patch was regenerated and matched the SHA-256 of the
tested frozen patch. Only this continuation note was subsequently added.

All 30 selected checks passed: 19 `audio_engine_rates_*_test` cases,
`web888_backend_test`, eight shared conformance checks and two GUI checks.
The shared cases cover PCM lifetime, receive contract, mixer, credential
settings safety, model mute, backend affinity, seam consumer and family switch.
The GUI cases are `connection_panel_size_test` and
`display_session_monitor_test`. The native Windows test also exercised WTS
registration and the lock/unlock message callback using its own test window;
it did not change the OS lock state. IPv6 formatting and waterfall recovery
protocol tests passed. The recovery guard was mutation-checked earlier.

Architecture ratchets had no new blocking findings: capability boolean count
remains 70, command-plane baseline 296, and engine-boundary legacy findings
remain 102 with zero new blockers. Manifest, test registration, accessibility
checks on the changed GUI files, whitespace, and strict user-doc link/anchor
build passed. These are selected checks, not a claim that the full test suite
or every platform was run.

Hardware qualification used Web888_v2026.919 on IPv4, with detailed testing in
USB. It covered direct connection/keychain reuse, a fresh profile, real AF
zero/restored PCM, repeated broad/narrow zoom and finite varying 1024-bin rows.
A 7-hour-37-minute frozen-build observation collected 137,003 audio-state
samples and 457 minute snapshots. Connection and spectrum stayed live and
memory/history were bounded. It included 124 sampled Idle states across 30
automatically recovered episodes; do not describe that run as interruption-free.

Later investigation found packet loss to both receiver and local gateway,
including with the app closed. Independent client delivery gaps correlated.
After the operator updated the host Wi-Fi driver, a three-minute two-client
comparison plus a two-minute slow-AGC follow-up produced 1,484 Active/NoError
audio-state samples with no new empty-buffer events. Packet-loss checks were
clean and narrow zoom passed again. That is short follow-up evidence, not a
guarantee against future network outages. The driver update is not a source
change in this branch. Onboard receiver FT8/WSPR channels continued decoding
and were not reconfigured.

## Continuing on another machine

Follow the repository's existing dependency/build instructions in
`docs/DEVELOPER-GUIDE.md` and the platform setup scripts, then build the
`AetherSDR` target in the configured build directory. Focused tests are
registered in `tests/tests.cmake`; use CTest with their names above. User docs
build with `npm ci` and `npm run build` under `docs/user`.

No workstation dependency binaries, driver installers, private receiver
addresses/passwords, raw PCM, local test profiles or diagnostic logs are
included. Configure a receiver endpoint and password locally through the UI;
the other machine must have network access to that receiver. Accepted OS
keychain credentials do not transfer with this branch.

## Limits and useful next work

- One standalone slice/pan and receive only. This does not add transmitter,
  public-directory browsing, camping, multi-profile diversity or general
  standalone KiwiSDR support. Existing external Kiwi functionality is retained.
- AGC Off/Slow/Med/Fast controls the receiver. There is no separate host AGC.
  The current Web-888 threshold is fixed at -100 dB and AGC-off manual gain
  uses the existing protocol default; an operator-adjustable off gain is not
  exposed. Attack/release accuracy was not calibrated with stepped RF input.
- NR4 is existing AetherSDR DSP, not a new algorithm in this branch. Live
  on/off/on captures and the deterministic stereo probe verified activity;
  output attenuation is not a measured improvement in speech intelligibility.
- A slice source named `RX` is the direct backend. Selecting an older saved
  external Web-888 profile can create another receiver session. Review that
  affordance for clarity before upstream submission; do not remove existing
  external-profile functionality without considering its users.
- Only one receiver firmware and Windows hardware environment were tested.
  IPv6 URL construction is unit-tested without live IPv6 receiver hardware.
  Linux/macOS builds and actual operator-unlock rendering should be qualified.
- The 360 ms reserve intentionally adds speaker startup/recovery latency and
  cannot conceal arbitrary multi-second network outages.
- Review backend lifetime/reentrancy, credential races and Windows session
  integration carefully before submitting upstream. Preserve receive-only
  operation and the existing engine seam/architecture ratchets.

Local diagnostic tooling and detailed logs remain in the originating
workstation's `build-tools` directory, outside this repository. Treat the
validation above as historical evidence and rerun the relevant checks after
new implementation changes.
