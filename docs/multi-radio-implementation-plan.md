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
