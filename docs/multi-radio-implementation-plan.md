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
