# Web-888 native engine prototype: local validation

Date: 2026-09-29. Base: `20d022d50566b7d5aa8fc85ddf66d00923c82117`.
Local branch: `web888-native`; uncommitted, not submitted upstream.
The receiver is in transit. No public receiver or simulated firmware server
was contacted.

The subsequent shared-IQ and software-splitting increment is documented in
[IQ validation](web888-iq-validation.md). The interactive desktop behavior
described here remains the connector default.

## Implemented scope

`Web888Backend` implements `IRadioBackend` inside aethercore, with one sound
receiver and paired waterfall using the existing shared transport. It is now
registered through the neutral backend factory and available as **Web-888
(experimental RX)** on the Connect by IP page. No new
above-seam vendor include or capability boolean was added.

Connection parameters are transient `RadioConnectRequest` values:
`host`/`port` (default 8073), or `params["web888.endpoint"]`, with an optional
`params["web888.password"]`. This is an internal prototype contract, not a
saved profile or supported command-line interface. Credentials are not saved
by the backend.

The desktop passes these options through the neutral `connectToRadioWithOptions`
entry point. It never attaches credentials to `RadioInfo`, discovered-radio
lists, saved routes or settings. Input is a direct host/IP with optional port,
not a URL/path; embedded credentials are rejected. The password field clears
after handoff and on family changes. Source-path overrides are refused because
the transport uses the system route. Native connections are explicit manual
attempts, not startup auto-connect profiles. Builds without WebSockets hide
the entry and the factory returns no backend for this family.

The prototype exposes one engine-owned slice setpoint. Initial supported modes
are AM, USB, LSB and CW. Mode changes choose a compatible initial passband;
custom filters are limited to +/-6 kHz for this increment. AGC modes map to the
shared transport's decay constants; the unrelated shared threshold UI scale
is neither advertised nor interpreted as a radio threshold. Reconnect reapplies
the AGC mode. Extra slices, IQ, DAX, TX and calibrated RF measurements are not
advertised. Tuning is not an acknowledgement from hardware.

Clean decoded 24 kHz stereo float PCM goes through the neutral slice producer.
A separate speaker producer applies linear gain and mute. Both epochs retire
before teardown; old queued events cannot enter a replacement session. The
waterfall route publishes the row's received center/span before its spectrum
bins and does not emit a duplicate waterfall feed. Receiver level observations
are forwarded only when valid and finite.

The shared client now has immutable independent-receiver admission. Native
sessions reject monitor/camping offers, HTTP redirects, and proxy endpoint
fallback. Other callers retain the previous default behavior. Failures produce
an error followed by disconnection. The optional neutral `ConnectionRecovery`
capability declares manual retry. RadioModel honors it on error and disconnect;
its timer handler also refuses a stale retry. The desktop retains the terminal
message and suppresses the reconnect display. Existing families retain host
retry by default. This fixes an integration gap in the earlier prototype:
avoiding `connectionError` alone did not prevent `onDisconnected` from retrying.

## Evidence

Windows/MSVC 19.44 and Qt 6.8.3, RelWithDebInfo, local optional-module settings
as recorded in [transport validation](web888-transport-validation.md).

- The complete `AetherSDR` desktop target builds successfully, as do aethercore
  and the test targets. The resulting executable loads and returns its isolated
  settings path through `--config path`, without opening the GUI or a receiver.
  A workspace launcher is available at `C:\1_HAM\Start-AetherSDR-Web888.cmd`.
- Both modified `KiwiSdrClient.cpp` and new `Web888Backend.cpp` also compile
  as individual objects with Qt WebSockets disabled; this is narrower than a
  successful full engine build (see the existing limitation below).
- Twelve selected tests pass. The original seven are `web888_backend_test`, `kiwi_sdr_session_test`,
  `kiwi_session_id_test`, `kiwi_sdr_protocol_test`,
  `kiwi_sdr_waterfall_scale_test`, `kiwi_sdr_waterfall_setup_test`, and
  `kiwi_sdr_waterfall_zoom_cap_test`.
- Added `web888_connection_test` verifies the production factory, transient
  option handoff, terminal credential refusal before any network activity,
  both retry gates, an explicitly triggered stale timer, and policy reset on
  family switches. `web888_connection_panel_test` verifies the family selector,
  custom/default ports, hostnames, invalid addresses/embedded credentials,
  and password handoff/clearing. Disabled-build runtime assertions are present
  but have not run because of the existing full-engine failure noted below.
- `backend_family_switch_test` and `backend_seam_affinity_test` now include
  Web-888; both pass. The existing `connection_panel_size_test` also passes.
- The changed `MainWindow_Session.cpp` compiles. The connection form was
  rendered and inspected, and the accessibility checker reports no findings
  for the two changed UI implementation files.
- The backend test injects client observations while suppressing HTTP preflight.
  It checks admission/readiness, invalid controls, mode passbands, AGC mapping
  and reconnect, speaker mute/gain versus clean slice audio, PCM formats,
  malformed samples, received waterfall geometry, valid meters, RX-only keying,
  epoch revocation, duplicate disconnect, queued stale PCM, cancellation during
  connected/audio signals, and reconnect from a disconnected observer.
- The shared session test feeds monitor/queue/camp tokens through the real
  client parser and verifies refusal without requesting monitoring.
- `SeamThreadAffinityProbe` covers all 52 declared seam signals, with no
  wrong-thread delivery or post-disconnect signal violations in the tested
  lifecycle. A queued callback is produced from an actual secondary thread.
- Engine boundary: 102 existing warnings, zero blockers, unchanged baseline.
  Capability boolean count: 71. Command-plane baseline: 413 in 32 files.
  Test registration, touchpoint manifest, generated seam table and frozen
  seven-test PR gate checks pass; none of their baselines were relaxed.

Build the twelve targets, set Qt and FFTW runtime directories on PATH, then run:

```text
ctest --test-dir build-web888 --output-on-failure --no-tests=error -R "^(web888_backend_test|web888_connection_test|web888_connection_panel_test|backend_family_switch_test|backend_seam_affinity_test|connection_panel_size_test|kiwi_session_id_test|kiwi_sdr_session_test|kiwi_sdr_protocol_test|kiwi_sdr_waterfall_scale_test|kiwi_sdr_waterfall_setup_test|kiwi_sdr_waterfall_zoom_cap_test)$"
```

For Qt's Windows offscreen platform, configure `QT_QPA_FONTDIR` with a usable
font directory. The first run without fonts rendered missing glyphs and failed
the pre-existing growth/scroll assertion in `connection_panel_size_test`.
All twelve pass with a font directory; a small local directory containing
Segoe UI regular/bold also passes and produces a readable form preview.
This is test environment setup, not a production font or layout change.

## Remaining qualification

The tests separate adapter behavior from transport protocol tests; injected
observations do not demonstrate actual server admission or successful tuning.
Credential persistence and live capability negotiation remain implementation
work. A capacity-aware digital capture planner/pool and manual interactive retry
policy now exist; shared desktop allocation controls, external digital-client
compatibility and broader recovery behavior remain unfinished. See the
[current implementation boundary](web888-native-implementation-plan.md) and
[capture-pool evidence](web888-capture-pool-validation.md). The twelve-test results
above describe the original interactive checkpoint; the later broad checkpoint
covers 24 tests, as recorded in [worker validation](web888-iq-worker-validation.md).
RF geometry, CW frequency accuracy, audio quality,
firmware variants, capacity and sustained CPU/memory behavior need hardware.
Linux/macOS and sanitizer qualification have not run.

The pre-existing WebSockets-disabled full-engine failure in `TciTrxMap.cpp`
remains as documented in transport validation. No successful full optional-
dependency build or runtime test is claimed.
The new neutral factory itself compiles in that configuration; the follow-on
connector test build stops at the same unchanged TCI errors. This does not
affect the successful WebSockets-enabled desktop build or the twelve passing
selected tests.
