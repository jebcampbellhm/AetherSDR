# Web-888 Narrow Zoom and upstream integration — 2026-10-19

## Sources and retained behavior

Integrated `fork/fix/web888-narrow-zoom` at `eb4e51c2` and upstream
`origin/main` at `cedd50f4` (including v26.10.3) into `web888-native`.
`fork/main` is already an ancestor of the integrated branch. Local backup
`backup/web888-before-oct19-integration` retains the starting `6e160936`.
Merge checkpoints: `1f66a5fa` and `dfb00c7d`.

The incoming branch used a single listening receiver. Integration retains our
two independently owned RX/waterfall leases, four logical shared SSB listeners,
selected-only speaker routing, digital capture allocator with two reserved
listening slots, and qualified-clock gates on timed decoding. Closing, tuning,
or recovering an interactive lease does not reconfigure digital captures.

Imported improvements include session-local flat-row zoom recovery, IPv6
endpoint formatting, a single address/port/password/Forget connection form,
endpoint-specific accepted-credential handling, 360 ms speaker startup/refill
buffering, SAM/FM in ordinary single-listener operation, firmware identity,
per-listener speaker pan, initial waterfall range and per-pan rate controls,
and Windows session-aware painting suspension. Shared IQ listeners remain
USB/LSB only. Decoder PCM stays ahead of speaker gain/mute/pan and buffering.

Resolved integration defects:

- Duplicate password UI and duplicate backend-test CMake registration.
- Wider recovery FFT rows expanding the requested narrow viewport. The backend
  now crops/interpolates against each row's actual MHz bounds, then publishes
  the requested geometry. This adds no physical FFT resolution.
- Generic engine-owned audio incorrectly implying backend-owned speaker pan.
  A separate enum policy preserves other backends' existing host panning.
- Credential-load cancellation/error handling and bounded load/session timeouts.
- Incoming UI/test assumptions of one receiver, and old fixture frequency units.

Upstream connection-dialog scrollbar sizing and the neutral backend factory
are both retained. Upstream TUNE inhibition reset and capability additions are
also retained; no transmit behavior is introduced into Web-888.

## Validation on this workstation

Windows MSVC RelWithDebInfo with pinned Qt 6.12:

- Desktop `AetherSDR` and all selected test targets built successfully.
- **47/47 CTest cases passed in 38.34 seconds.** Selection covers all Web-888
  tests (including shared SSB, capture allocation, IQ processing, synthetic
  WSPR at multiple rates, decoder routing/period/pipeline/process/WAV tests),
  Kiwi protocol/session/waterfall setup/zoom-cap/scale, 19 audio-engine rate
  scenarios, PCM compatibility/lifetime, backend family/seam checks, connection
  geometry, monitoring UI and display-session handling.
- `app_settings_safety_test credential-exodus` passed separately, including
  rejection/redaction of receiver credential fields in settings.
- Native Windows display-session registration and simulated lock/unlock
  messages passed. The test did not lock or unlock the operating system.
- Native Windows connection and monitoring form fixtures passed; their rendered
  screenshots were inspected. Offscreen captures lacked fonts, so they were
  not used as visual qualification.
- Strict engine-boundary, capability-record, command-plane, test-registration,
  touchpoint-manifest and seam-probe checks passed (existing baselines retained).
  Accessibility checks on the three affected GUI implementation files had no
  findings. `git diff --check` and the user documentation build passed.

Ignored local evidence is under `build-web888-evidence/oct19-*`, including
`oct19-regression.txt` and native form screenshots. Original branch hardware
observations in `web888-handoff.md` remain historical evidence, not live
validation of this integration.

## Limits

No receiver was contacted and no receiver settings were changed for this
integration. No new RF, decoder reception, timestamp, GPS, or sustained network
qualification is claimed. Live timed decoding remains gated on a verified
sample-to-UTC mapping. The receiver's persistent 24 kHz setting is untouched.

This local build has no QtKeychain dependency, so passwords are session-only
and the UI says so. Accepted-save/Forget races are tested with an isolated
credential-store fixture; an actual OS keychain was not exercised. Linux/macOS
builds and the full repository test suite were not run. No publication or
upstream PR is part of this integration.
