# Independent multi-radio station

## Authorized work window

2026-10-26 until 21:30 UTC (4:30 pm America/Chicago). Branch:
`feature/multi-radio-station`, based on combined daily branch commit 4b9bdfdb.
The original daily executable in `build-daily-web888-nrsp` is preserved;
experimental desktop builds must use a separate build directory. Reusing that
build directory for a targeted core test does not rebuild its desktop target.
Heartbeat: `aether-three-radio-development-until-4-30` (pause at deadline).

## Operator requirements

Show independent Web-888, nRSP-ST and Icom sessions simultaneously, with per-radio
waterfalls/slices and explicit speaker selection. Only the designated Icom may
transmit. Selection of an SDR must never move the transmit frequency implicitly.
The Icom will use Ethernet but is not online yet (operator expects 1-2 days).
Do not infer exact-model compatibility from the existing Icom backend.

Keep PC audio off during development (operator on Teams). Do not use GUI
accessibility/automation polling: previously caused audio/UI stalls. Silent
bounded receive probes and socket-free tests are authorized. No transmit or
persistent radio/admin/RF configuration changes. Web-888 remains at 12 kHz.
Receiver protection is hardware-owned; software muting is not RF protection.

## Architecture findings

`RadioSession` already owns one `RadioModel` plus CAT/TCI lifetimes. MainWindow
has a vector of sessions, but creates only one and holds a fixed reference to
that model throughout its controls. Merely appending a session will not render
it or route its controls. Both backends already work through the neutral model
and publish typed PCM plus neutral pan feeds. Numeric slice/pan IDs are local
to each radio and must be paired with the owning session in the desktop.

## Increments

1. Independent session owner with monotonic session IDs, separate focus and
   audio selection, silent default, and queued-audio selection revision fences.
   This is lifecycle infrastructure, not completed multi-radio desktop support.
2. Silent simultaneous live model probe for Web-888 and nRSP-ST: authenticate,
   receive both spectrum/audio streams, remove/reconnect one while the other
   continues, close both. No audio device, no frequency changes. Avoid keychain
   secrets in evidence.
3. Desktop session-bound receiver views and a station selector/add/remove flow.
   Reuse existing neutral pan/slice controls. Keep primary-model control paths
   explicit until each widget can bind its owning session. Never remap global
   numeric IDs between models. Verify offscreen behavioral tests, without
   attaching automation to an operator's running app.
4. Explicit transmit-radio designation, observed-TX receive muting/decoder
   discontinuity and recovery. Must gate all keying entry points, not merely
   hide a button; selection grants no transmit authority. Start with offline
   injection; Icom hardware is unavailable. Preserve existing TX arbiter.
5. Build/deploy experimental desktop separately; regression and sanitized
   evidence, with live UI/hardware limitations stated. Optional linked tuning
   comes after independent sessions are reliable.

## Latest checkpoint

2026-10-26 18:48 UTC station transmit permission increment:

- Additional RadioStation sessions are explicitly receive-only in the engine,
  regardless of their backend or label. Focus and selected audio never grant
  transmit permission. For this station, use Icom as the primary connection and
  Web-888/nRSP-ST as the two additional receivers. Arbitrary relocation of the
  full TX controls to an additional panel is not implemented.
- Opening Additional receivers now clears primary software TX permission.
  **Allow transmit on primary radio** explicitly designates a connected,
  transmit-capable primary. It does not key. Disconnect, capability loss and
  workspace destruction revoke it; reconnect requires a new selection. Hiding
  the dock preserves the selection. Normal standalone behavior before opening
  the station workspace is unchanged. User docs describe this workflow.
- TxCoordinator fences all actor admissions and terminal media dispatch with
  station permission. Role changes discard pending inputs and give microphone
  contexts a new epoch, so disable/re-enable cannot revive old media. Revocation
  closes admission before invoking stop callbacks, retains stop-only cleanup,
  rejects reentrant reopening and preserves existing stop-confirmation recovery.
  A checkbox is never a stopped-radio acknowledgment; active revocation may
  require qualified stop evidence or reconnect before new keying is admitted.
- RadioModel also fences scheduled CW across role changes. The checkbox's
  notification refreshes the main microphone context without enabling PC Audio,
  starting capture, changing devices or writing radio settings. Physical PTT,
  other radio clients and RF protection remain outside this software permission.
- Ten selected tests passed: tx_coordinator_test, tx_operation_integration_test,
  tx_grant_manager_test, tx_audio_context_test, radio_station_test,
  monitor_audio_router_test, spectrum_confirmed_geometry_test,
  spectrum_pan_pixels_retina_test, station_display_isolation_test and the new
  station_transmit_permission_test. The latter uses real offscreen widgets and
  a socket-free recorder: explicit designation, no keying on selection, mic
  authorization renewal, capability loss, disconnect/reconnect and destruction.
  RTL-specific rows remain skipped because this build lacks RTL-SDR support.
- Two separate mutations failed as intended: removing actor permission allowed
  an unauthorized admission; removing the media epoch check revived old mic
  context after re-designation. Both guards were restored before the final build.
  A final queued-CW regression also passed in the three focused engine tests:
  pre-disable and disabled-period callbacks are dropped after re-enable, while
  fresh CW reaches only the injected recorder. No real transmitter was used.
- The rebuilt receive-only live probe passed with both SDRs: Web-888 361472
  normalized audio frames / 301 spectrum frames; nRSP-ST 232960 / 100. Selected
  monitor frames 120832 / 119808. Switching/removal revoked old queued audio,
  Off leaked none, and Web-888 delivered 119808 frames after nRSP-ST removal.
  Both leases released. No AudioEngine/device or persistent receiver changes.
- Experimental desktop rebuild, minimal-PATH startup self-test (0 / 26.10.4),
  docs build, boundary (0 blockers / 101 baseline warnings), capability,
  command-plane, registration, manifest and whitespace checks passed. Daily
  executable remains unchanged at 2026-10-26 10:48:50 local. No normal rendered
  desktop, audible playback or Icom Ethernet/TX qualification is claimed.
- Evidence in build-web888-evidence: station-tx-final-build.log,
  station-tx-final-tests.log, station-tx-tests.log, station-tx-mutation-actor.log,
  station-tx-mutation-media-epoch.log, station-tx-live.json,
  station-tx-startup-marker.txt, station-tx-docs.log and station-tx-boundary.log.
  Earlier intermediate build attempts are superseded. All commands ended and
  production guards are restored.

NEXT: observed-primary-TX receive interruption/recovery, tested by offline
normalized status injection; do not key hardware to test it. Preserve selected
speaker routing and independently fence/reset digital decoder buffers. Then
review additional-receiver audio when the primary is offline, teardown/reconnect
and remaining per-radio controls. Icom remains unavailable; keep PC audio off.

### Earlier display isolation checkpoint

2026-10-26 18:23 UTC display isolation increment:

- Additional receiver spectra now use an explicit session-only display policy.
  They seed appearance once, retain local edits, and never migrate or overwrite
  primary pan settings. Global display-toggle broadcasts exclude these views
  in both directions. This is transient isolation, not saved per-radio profiles;
  no invented flat-key slot namespace was added. User documentation states the
  restart/removal lifetime of these edits.
- Primary capability and slice-migration updates now enumerate the primary
  pan stack instead of recursively reaching additional receivers. Each receiver
  panel applies its own model's dBm ownership and absolute-bin declarations.
- The new socket-free station_display_isolation_test constructs real widgets
  with an isolated settings database. It verifies disk/store preservation,
  local settings, primary persistence, production panel policy, independent
  capability updates, identical stream IDs on different radios, and detached
  panel rejection. Removing the central save guard produced three assertion
  failures (exit 1); restoring it passed. No radio or audio device was opened.
- Fixed pre-existing portability problems in the opt-in widget harness:
  unconditional RTL references in builds without RTL, large stack-allocated
  widgets overflowing the Windows stack before main initialized, and missing
  offscreen-plugin deployment. Widget fixtures now use heap ownership; Windows
  builds copy Qt's matching offscreen plugin; registered runs have 30-second
  limits. RTL-specific rows are explicitly skipped in this build, not qualified.
  The client display source checks now read text with CRLF normalization.
- Final five tests passed: spectrum_confirmed_geometry_test,
  station_display_isolation_test, spectrum_pan_pixels_retina_test,
  client_display_settings_test and client_display_pan_settings_test. The first
  four-test run also passed before mutation testing. Earlier failed/crashed
  harness runs are not validation evidence; final logs supersede them.
- Experimental desktop rebuilt and minimal-PATH --update-selftest returned
  0 / 26.10.4. Docs build, engine boundary (0 blockers / 101 baseline warnings),
  capability, command-plane, registration, manifest and whitespace checks passed.
  Daily executable is still unchanged (2026-10-26 10:48:50 local). No normal
  desktop launch, GPU rendering qualification, audible playback or live probe
  in this increment. No leases remain open.
- Evidence under build-web888-evidence: station-display-final-build.log,
  station-display-final-tests.log, station-display-mutation.log,
  station-display-startup-marker.txt, station-display-docs.log and
  station-display-boundary.log. Production guard restored; all commands ended.

NEXT: explicit station TX designation and all-entry-point fencing, then observed
TX receive interruption/recovery, using offline injected tests. The Icom remains
offline. Additional-receiver audio without an online primary, saved per-radio
display/DSP profiles, full controls and rendered desktop checks remain pending.
Keep PC audio off and preserve the working daily executable.

### Earlier selected monitor checkpoint

2026-10-26 17:50 UTC selected monitor audio increment:

- Added the `MonitorAudioRouter` model and **Listen to:** selector in Additional
  receivers. Primary radio is the initial selection; adding a receiver does not
  change it. Selecting an additional receiver chooses that receiver's active
  speaker stream, and removing it selects Off. The PC Audio switch remains
  separate and is never enabled by selecting a monitor source. No device was
  opened during development.
- All primary speaker producers now use `RadioModel::rxDemodAudioReady` through
  the router. Removed direct Flex/seam/sim speaker bypasses, preserving the
  normalized bus's single-producer choice. RadioStation also consumes that bus,
  including transports whose audio is not emitted through backendAudioFrameReady.
- Routed PCM carries an additional revocable selection lease, retained by
  converted-buffer leases. Switching, muting or destroying the route invalidates
  queued speaker copies while leaving original producer/decoder frames alive.
  First blocks after a selection change are discontinuous so the audio engine
  clears old DSP/jitter state. Playback owners mute the selected route without
  disconnect/reconnect duplication. Existing primary decoder taps remain outside
  the speaker router; this does not add extra-receiver decoder controls.
- Five socket-free tests passed: pcm_frame_test, pcm_compatibility_test,
  monitor_audio_router_test, radio_station_test and audio_engine_pcm_lifetime_test.
  The real AudioEngine test uses QBuffer rather than a device: queued old-source
  rejection, 24 kHz stereo / 48 kHz mono changes, switching back, Off buffer
  clearance, and independent decoder epoch lifetime. No sanitizer claim.
- Bounded silent live probe passed with both user receivers: Web-888 361472
  normalized audio frames / 301 spectrum frames, nRSP-ST 232960 / 99. Selected
  monitor frames: Web 120832, nRSP-ST 119808. Switching revoked the old monitor
  lease; removing nRSP-ST revoked its lease and left the selector silent. Web
  delivered another 120832 frames after removal. Both sessions were released.
  No AudioEngine/device, TX, shared settings changes or GUI automation. Probe
  output suppresses Qt wire logs and uses fixed connection-error diagnostics.
- Experimental desktop rebuilt successfully. Minimal-PATH startup self-test
  returned 0 / 26.10.4. Docs build, engine-boundary (0 blockers; 101 existing
  warnings), capability (70), command-plane (125/16), registration, manifest and
  whitespace checks passed. New model include is tagged universal and consumed
  only for speaker selection; no vendor allowance or command baseline grew.
  Working daily executable remains unchanged at 2026-10-26 10:48:50 local.
- Evidence: build-web888-evidence/station-monitor-build.log,
  station-monitor-final-build.log, station-monitor-tests.log,
  station-monitor-live.json, station-monitor-startup-marker.txt,
  station-monitor-docs.log and station-monitor-boundary.log. All commands ended.

NEXT: isolate additional SpectrumWidget display preferences from primary pan
slot 0 (currently a confirmed shared-settings risk). Use a scoped feature object
or an explicit transient preview policy; do not allocate magic flat-key pan
indices. Then verify session-bound view controls without GUI polling. Speaker
DSP/device lifecycle still follows primary controls: additional reception without
an online primary and per-radio speaker DSP profiles need separate treatment.
Normal rendered UI and audible output are not validated. Icom TX designation,
all-entry-point keying gate, observed-TX receive interruption/recovery and actual
Icom Ethernet operation remain pending. Hardware is expected in 1-2 days.

### Earlier desktop preview checkpoint

2026-10-26 17:20 UTC desktop preview increment:

- `StationWorkspace` is a CanonCanvas in a dock reached through **File →
  Additional receivers…**. Its Add receiver action reuses ConnectionPanel and
  adds up to two independent RadioStation sessions without replacing the
  primary MainWindow model. Close on a receiver removes it; closing the dock
  hides it without stopping reception. No automatic connections.
- `StationReceiverPanel` binds neutral model pan feeds by the owning model plus
  pan/waterfall ID. It offers per-radio slice selection, frequency, mode, filter
  edges, add/remove slice and add waterfall. Spectrum click/drag/wheel tuning
  and pan geometry requests route only through that model. Selection reconciles
  to the model's active-slice state. Additional panels are silent and have no
  wired TX intent; the primary main-window controls are unchanged.
- Full overlay menu is disabled with a reason pending proper session binding;
  do not expose placeholder controls as working. Individual waterfall close,
  reconnect-in-place, per-receiver display-setting namespaces and full AGC/DSP
  controls are not yet complete. Existing SpectrumWidget preferences currently
  inherit the default pan slot: audit/isolate this before calling the UI ready.
- Built `build-multi-radio-station/AetherSDR.exe` separately, retaining the old
  daily executable (unchanged timestamp 2026-10-26 10:48:50 local). Full build,
  Qt DLL deployment, radio_station_test, docs build, engine-boundary (0 blockers,
  101 baseline warnings), capability, command-plane, registration, manifest and
  whitespace checks passed. Minimal-PATH `--update-selftest` returned 0 / version
  26.10.4. No normal UI launch, rendered desktop validation, audio device or live
  probe was performed this increment. Earlier model-level live evidence below
  remains valid but must not be presented as rendered desktop support.
- Scripts/logs: station-desktop-build.cmd (fresh build),
  station-desktop-final.cmd (incremental build/deploy/test),
  station-desktop-final-build.log, station-desktop-tests.log,
  station-desktop-deploy.log, station-startup-marker.txt and station-docs.log in
  build-web888-evidence. All running build/test commands have finished.
- New UI-to-model includes are deliberate: the receiver panel consumes the
  existing neutral RadioSession/PanadapterModel/SliceModel surfaces; workspace
  owns the new universal RadioStation. Manifest regenerated and tagged, no
  vendor include or raw-wire allowance added.

NEXT bounded increment: review the preview's lifetime/control routing and
settings isolation; add behavioral tests for per-radio view binding if feasible
without GUI automation. Then integrate explicit selected-only audio with the
existing AudioEngine and primary feed, muted by default and tested silently;
never mix primary and additional feeds accidentally. Add TX designation and
observed-TX recovery/interruption after route isolation. Icom is Ethernet but
not online for 1-2 days. UI and real Icom qualification still pending.

### Earlier foundation checkpoint

2026-10-26 16:46 UTC: RadioStation source and socket-free tests implemented.
It owns multiple existing RadioSession objects, keeps focus/audio independent,
and fences queued audio across selection/removal. Model destruction is deferred
when removal originates in a model signal. No desktop integration or TX
designation existed at this checkpoint. The core test passed; engine boundary, capability,
command-plane and test-registration checks passed; manifest stayed current.

The silent simultaneous model-level live probe passed: Web-888 361472 audio
frames / 308 spectrum frames; nRSP-ST 232960 / 99. After nRSP-ST removal,
Web-888 delivered another 119808 audio frames. Both sessions were released.
The probe opens no AudioEngine/device and changes no shared radio settings.
Logs: `build-web888-evidence/station-live.json`, station-build.log and
station-tests.log. Source targets: radio_station_test and the opt-in
radio_station_live_probe. Build script: station-build.cmd.

Next: wire independent session-bound desktop receiver views. Preserve existing
working daily executable and avoid retargeting its fixed primary RadioModel
reference globally. Investigate reuse of PanadapterStack/PanadapterWidget and
existing SliceControlRequest routes with explicit owner model per view. Add a
small station window/dock and Add receiver flow if that permits safe reuse;
do not present separate executables as completed single-window support.
Icom is Ethernet and offline for 1-2 days per operator; no need to ask again.

The preceding single-radio nRSP-ST probe succeeded at its operator-specified
address, including fresh-process saved-keychain reconnect: 15 seconds yielded
352768 audio frames and 150 spectrum frames; 5-second saved-password run yielded
113152 audio frames and 48 spectrum frames. Neither opened a PC audio device,
changed shared settings, or left a lease open. Those results do not establish
multi-radio or desktop connection behavior.
