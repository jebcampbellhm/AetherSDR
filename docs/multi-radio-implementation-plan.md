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

2026-10-26 20:51 UTC additional-receiver reconnect controls checkpoint:

- Added Reconnect to each receiver panel. It is unavailable while connected
  or while a connection attempt is active, and becomes available after failure
  or disconnect. Recovery uses a fresh ConnectionPanel prefilled with that
  receiver's address/type and saved route; the operator confirms the original
  endpoint and supplies fresh options through the existing credential flow.
  Recovery keeps the same session/panel and does not change listening selection
  or consume another receiver slot. A changed endpoint is refused with guidance.
- The station accepts replies only from its current, visible connection form.
  This fences superseded/canceled forms and duplicate replies after submission.
  Connection acknowledgment/error is bound to the submitting form and session,
  so another receiver cannot acknowledge its staged credentials. No credential
  options are retained in station/panel members or evidence.
- Added station_reconnect_test to the opt-in offscreen GUI harness. It uses two
  built-in demo sessions (no firmware peer, socket or audio device), exercising
  disabled/enabled controls, duplicate and queued stale submissions, fresh-form
  recovery in place, changed-endpoint refusal, unrelated status, removal, and
  preservation of the other receiver/listening selection/RX-only authority.
  The initial harness needed to await the asynchronous disconnected UI edge;
  the final test waits for that state. Removing the active-form guard failed
  the duplicate-session assertion; restored production code passed.
- All five selected GUI regressions passed: reconnect, display isolation,
  transmit permission, confirmed geometry and Retina pan pixels. Experimental
  desktop rebuilt and minimal-PATH startup returned 0 / 26.10.4. Docs build,
  boundary (0 blockers / 101 baseline warnings), capability, command-plane,
  registration, manifest and whitespace checks passed. Evidence files use the
  station-reconnect-ui prefix under build-web888-evidence, including the
  mutation-test log and startup marker. No mutations remain.
- This increment used no real radios, audio devices or normal running GUI.
  Earlier live transport reconnect evidence remains separate from these UI
  control tests. Daily executable remains 163880448 bytes / 10:48:50 local.

NEXT: final review and handoff of the experimental build, with normal desktop,
audible playback and Icom hardware limitations explicit. Keep PC audio off;
do not begin a large new feature near the deadline. Stop at 21:30 UTC and pause
the automation. Optional future work: richer additional-receiver DSP/decoder
controls, per-receiver saved display preferences and Icom hardware validation.

### Earlier queued model-update teardown checkpoint

2026-10-26 20:34 UTC queued model-update teardown checkpoint:

- Demonstrated that disconnecting a receiver panel's signals did not cancel
  already queued spectrum/capability deliveries: detached views still changed.
  The panel now checks its attached model before applying queued state, data,
  capability or lifecycle callbacks. Incoming pan geometry/levels use the same
  current-view ownership guard as outgoing controls. Late pan creation cannot
  dereference the cleared model after detach.
- Offscreen tests now queue real model signals before detach, then drain them.
  They cover spectrum/capabilities, pan geometry/levels, pan creation/removal,
  slice arrival and positive geometry while attached. The original data/caps
  test failed before the fix; removing the geometry guard failed its dedicated
  assertion. The restored code passed all four selected GUI regressions.
- Rebuilt experimental desktop; minimal-PATH startup returned 0 / 26.10.4.
  Docs build and whitespace checks passed. This adds no includes above the
  engine boundary, settings keys, dependency or test registration. Evidence:
  station-detach-before-test.log, station-detach-mutation-test.log,
  station-detach-build.log, station-detach-test.log, station-detach-docs.log,
  station-detach-startup-marker.txt in build-web888-evidence.
- No normal GUI, live receiver or audio device used; all tests are finished,
  no mutations remain, and the daily executable is preserved. Rendered/audible
  desktop and Icom hardware qualification remain outstanding.

NEXT: review connection-form reuse and manual recovery for additional receivers;
fresh credential options must be collected for every explicit attempt. Then
finish the window handoff. Stop at 21:30 UTC and pause the automation.

### Earlier retired waterfall controls checkpoint

2026-10-26 20:25 UTC retired waterfall controls checkpoint:

- Reproduced a queued gesture from a removed receiver waterfall tuning a new
  view with the same pan ID. StationReceiverPanel now requires both the current
  pan model and the current widget for outgoing geometry, tune, filter, slice
  creation/removal and selection requests. Reusing the model itself cannot
  revive the old widget's queued input. Slice selection also checks pan ownership.
- Extended the offscreen station_display_isolation_test with an offline slice
  fixture and genuinely queued widget signals. It covers replacement models,
  reuse of the same model, positive current-view tuning and detached data paths.
  The initial regression failed before the fix; removing only the widget-identity
  comparison failed the same-model case. Restored production code passed all four
  selected GUI regressions, including geometry, Retina pan and TX permission.
- Experimental desktop rebuilt; minimal-PATH startup self-test returned
  0 / 26.10.4. Docs build, boundary (0 blockers / 101 baseline warnings),
  capability, command-plane, registration, manifest and whitespace checks passed.
  No live receiver, audio device or normal desktop GUI was opened this increment.
  Daily executable remains unchanged at 163880448 bytes / 10:48:50 local.
- Evidence under build-web888-evidence: station-retired-pan-before-test.log,
  station-retired-pan-mutation-test.log, station-retired-pan-build.log,
  station-retired-pan-test.log, station-retired-pan-docs.log and
  station-retired-pan-startup-marker.txt. No mutations remain.

NEXT: review pending model callbacks during receiver-panel detach and session
removal, then finish the window's build/validation handoff. Reconnect transport
has been checked in both directions; reconnect-in-place UI, audible/rendered
desktop qualification and real Icom qualification remain separate work.
Keep PC audio off and daily build unchanged; stop at 21:30 UTC.

### Earlier bidirectional reconnect checkpoint

2026-10-26 20:12 UTC bidirectional live reconnect checkpoint:

- Extended the opt-in radio_station_live_probe with --reconnect web|nrsp.
  It uses the desktop's positive additional-source IDs, observes availability,
  reconnects the selected model, verifies a new PCM epoch/discontinuity, removes
  nRSP-ST and verifies continuing Web-888 reception, then closes both. It opens
  no AudioEngine/device and refuses to run without its isolated settings profile.
  Reconnect mode has a 60-second deadline; the original mode retains 40 seconds.
- Both runs passed. nRSP-ST reconnect: Web-888 delivered 63488 audio frames
  during the interruption; reconnected nRSP-ST supplied 113152 selected monitor
  frames. Totals Web-888 607232 audio / 504 spectrum; nRSP-ST 369664 / 157.
  Web-888 reconnect: nRSP-ST delivered 24576 frames during the interruption;
  reconnected Web-888 supplied 72704 selected monitor frames. Totals Web-888
  543744 / 480, nRSP-ST 400896 / 170. Counts are normalized PCM, not ADC-rate or
  RF-quality measurements.
- In both directions the selected source became unavailable then available,
  old audio stayed revoked, the first new epoch was discontinuous, selection
  and removal revoked routed copies, and Off leaked none. Each run released
  both sessions. No audio device, TX, reporting, public receiver or persistent
  radio-setting change. Credentials were loaded from keychain, never printed.
  Evidence: station-reconnect-build.log, station-reconnect-nrsp.json and
  station-reconnect-web.json under build-web888-evidence.
- Only the diagnostic source changed; the experimental desktop remains the
  independent-playback build and the daily executable remains untouched.
  These are model/transport checks, not rendered desktop or audible playback
  qualification. Icom remains unavailable.

NEXT: review receiver-panel teardown and deferred controls. In particular,
removed waterfall widgets use guarded pan pointers but some callbacks compare
only reused pan/slice IDs; test whether a queued old gesture can target a new
view after pan replacement, then fix any demonstrated defect. Prefer offscreen
fixtures; never automate a running user GUI. Work locally until 21:30 UTC, then
stop and pause the automation.

### Earlier independent speaker lifecycle checkpoint

2026-10-26 20:00 UTC independent speaker lifecycle increment:

- MonitorAudioRouter now tracks source availability separately from selection.
  The desktop's receive-output lifecycle follows the selected connected radio
  and the existing PC Audio setting. An additional receiver can request output
  while the primary is offline; primary connect/disconnect does not restart or
  stop an already selected additional receiver. TX capture remains primary-only.
  Source loss/removal and Off release live receiver output. Independent local
  playback owners retain their shared-sink demand; PC Audio OFF still wins.
- Device requests are revocable across queued delivery, newer toggles, source
  loss, shutdown and router destruction. Restart rechecks after stop callbacks.
  OFF runs full cleanup even if the main sink is already missing, covering
  remaining local tones/buffers. PC Audio OFF also revokes queued speaker PCM
  and drops new routed speaker frames, without retiring decoder producers.
  ON begins fresh speaker PCM. Prior observed-TX receive holds still apply.
- StationWorkspace forwards each additional model's connection availability
  with its source ID and rejects callbacks for removed sessions. Existing
  primary disconnect cleanup now refreshes selected receive playback instead
  of unconditionally closing it. Forced application shutdown still stops RX.
  No new settings keys, receiver commands, credentials or TX paths were added.
- Ten selected regressions passed after the final cleanup/PCM changes:
  monitor_audio_router_test, station_receive_interruption_test,
  radio_station_test, audio_engine_pcm_lifetime_test, tx_audio_context_test,
  hl2_pc_audio_lock_test, spectrum_confirmed_geometry_test,
  spectrum_pan_pixels_retina_test, station_display_isolation_test and
  station_transmit_permission_test. Device-intent tests use callback counters,
  and AudioEngine tests use memory output; no real audio device is opened.
  Removing the stale-request guard caused the queued-stop regression to fail;
  the restored guard passed. No mutations remain.
- Experimental desktop rebuilt; minimal-PATH startup self-test returned
  0 / 26.10.4. Docs build and whitespace checks passed. Boundary scan still
  has 0 blockers / 101 baseline warnings; capability, command-plane and
  manifest checks passed. Evidence: station-playback-final-build.log,
  station-playback-final-tests.log, station-playback-mutant-test.log,
  station-playback-restored-test.log, station-playback-boundary.log,
  station-playback-docs.log and station-playback-startup-marker.txt under
  build-web888-evidence. Earlier policy-build logs are superseded by the final
  ten-test run. No commands or receiver leases remain active this increment.
- Daily executable remains 163880448 bytes, 2026-10-26 10:48:50 local. PC audio
  remained off, no normal GUI was launched, and no radio was contacted in this
  increment. The preceding live receive evidence remains separate. Actual
  audible output, ordinary rendered desktop and Icom hardware remain
  unqualified. Speaker DSP controls are still shared, not per-radio processors.

NEXT: extend the bounded silent simultaneous receive probe to exercise one SDR
disconnecting/reconnecting while the other continues, including availability,
old PCM lease invalidation and selected-only routing. Keep devices closed and
always release both receivers. Then review remaining per-radio controls and
panel teardown. Work remains local on feature/multi-radio-station; stop and
pause automation at 21:30 UTC. Icom is still unavailable; never key hardware.

### Earlier receive interruption checkpoint

2026-10-26 19:33 UTC station receive interruption increment:

- RadioStation watches the primary's normalized TX status. An observed TX holds
  local receive PCM on the primary and additional receivers. Only a current
  confirmed RX readback releases the hold; optimistic unkey, disconnect reset,
  another receiver, replaced backend and removed primary cannot release it.
  New receivers inherit a hold. The station dock displays the waiting state.
  Connections and waterfalls are not stopped, and no radio setting is written.
- Receive leases revoke already queued PCM and converted speaker/decoder blocks
  independently of producer and selected-speaker lifetimes. A thread-boundary
  revision stamps PCM before Qt delivery: pre-hold queued blocks and samples
  emitted during the hold cannot reappear after RX. Resumed streams retain their
  source positions and begin fresh decoder/resampler segments. The backend's
  unchanged keying-confirmation relay now has the existing generation fence.
- Web-888 local digital audio receives the same interruption lifetime; timed
  routes discard partial/waiting work and revoke issued jobs without releasing
  captures or forgetting the profile/budget. Resume requires fresh qualified
  anchors. This does not qualify stock firmware UTC or enable timed decoding.
  Profile marker cleanup follows capture lifetimes, preserving existing streams
  while a replacement awaits admission or fails. A new regression reproduced
  and fixed a premature discontinuity during such a pending replacement.
- Eighteen selected regressions passed in station-rx-final-tests.log, including
  the new station_receive_interruption_test, decoder conversion/history,
  memory-only AudioEngine buffer retirement, queued backend replacement, Web-888
  capture/decoder routing, station ownership, TX permission and offscreen view
  behavior. A subsequent focused capture-pool regression passed after the final
  profile-marker fix (station-rx-profile-fixed-test.log); the experimental
  desktop was rebuilt with that fix. The offscreen UI coverage is still the
  opt-in spectrum test target; RTL rows remain skipped in this build.
- Removing the PCM delivery revision check made the new queued-audio assertion
  fail; restoring it passed. See station-rx-mutant-test.log and
  station-rx-restored-test.log. The profile rollback test also failed before its
  fix (station-rx-profile-before-test.log). No mutations remain. Initial family
  test startup lacked Qt6Test.dll; deploying the matching pinned Qt test DLL to
  the experimental folder resolved it, and subsequent family/full runs passed.
- A bounded silent live probe passed: Web-888 360448 normalized audio frames /
  306 spectra, nRSP-ST 232960 / 100. Selected monitor frames 120832 / 119808;
  Web-888 continued for 118784 frames after nRSP-ST removal. Selection/removal
  revoked old monitor frames and Off leaked none. Both sessions released, no
  AudioEngine/device opened. This validates ordinary simultaneous receive and
  cleanup, not real TX interruption. Evidence: station-rx-live.json.
- Experimental desktop build and minimal-PATH startup self-test passed
  (0 / version 26.10.4). Docs build passed. Boundary (0 blockers / 101 baseline
  warnings), capability, command-plane, registration, manifest and whitespace
  checks passed. Build logs: station-rx-final-build.log and
  station-rx-profile-fixed-build.log; documentation log station-rx-docs.log.
  Daily executable remains 163880448 bytes, 2026-10-26 10:48:50 local. No app is
  left running, no probe/build commands remain, and nothing was published.
- Limits: this is observed-status local muting, not RF protection or a verified
  sample-exact TX boundary. Already delivered device/external-program audio
  cannot be recalled. Icom Ethernet status latency, hardware protection and
  rendered normal desktop/audible behavior remain unqualified. No real TX,
  reporting or persistent receiver settings were changed; Web-888 stays 12 kHz.

NEXT: review additional-receiver speaker/device lifecycle when the primary is
offline (currently still depends on the primary connection). Start with
MainWindow_Session.cpp wireMonitorAudio / station workspace wiring and existing
AudioEngine lifecycle, preserve PC Audio OFF and selected-only routing. Then
review teardown/reconnect and remaining per-radio controls. Use memory sinks,
socket-free fixtures and bounded silent probes; no GUI polling or real TX.
Icom remains offline. Continue only until 21:30 UTC; pause automation then.

### Earlier transmit permission checkpoint

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
