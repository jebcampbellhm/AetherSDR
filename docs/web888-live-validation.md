# Web-888 live transport validation — 2026-10-16

Operator decision (2026-10-16): keep the receiver at 24 kHz until the 36 kHz defect is fixed. This supersedes the earlier temporary restoration to 36 kHz. Continue dual-waterfall, independent listening and shared-IQ multi-stream development at 24 kHz.

Receiver firmware reports v2026.919 (2026-09-19 build), HF 32 MHz mode,
13 RX channels and two exclusive waterfalls. No RF antenna is connected.
These are transport and sample-count checks, not RF sensitivity, frequency
accuracy, audio quality, or successful FT8/WSPR decoding qualification.

## Rate comparison

Each 60-second run used the production client for two independent USB PCM
listeners with waterfalls (40m and 20m), plus a sound-only 30m IQ capture
feeding two software audio slices: FT8 and WSPR. This uses three RX channels
and two waterfalls. The manual probe does not execute decoders or report spots.

| Admin setting | Advertised IQ samples/s | Observed IQ samples/s | Raw IQ samples | Samples per software audio output | PCM sequence gaps |
|---|---:|---:|---:|---:|---:|
| 12 kHz | 12000.002268 | 12000.253 | 716288 | 715775 | 0 / 0 |
| 24 kHz | 24000.004565 | 23998.923 | 1432576 | 716032 | 0 / 0 |
| 36 kHz | 36014.074626 | 39999.357 | 2387968 | 795507 | 0 / 0 |

Observed rate is the sample count after the first block divided by monotonic
elapsed time between first and last IQ callbacks. It is an arrival-rate estimate,
not a calibrated sample-clock measurement. Small differences reflect delivery
jitter; the approximately 11.1% difference at 36 kHz is much larger.
Each run had one initial IQ/audio discontinuity and no subsequent discontinuity.
Both waterfalls continued throughout all runs. Startup negotiation explains why
counts are smaller than a full 60 seconds at the steady-state rate.

The public firmware source inspected locally at commit
`4dfecc4626d206e077f6f6908bd9b97ac4607cb8` of `RaspSDR/server` is consistent
with this result: HF code computes `rx_decim = int(61440000 / snd_rate)` and
`fpga_rx_decim = rx_decim / 256`, while reporting clock / rx_decim. At 36 kHz
this reports clock / 1706, but the FPGA integer factor 6 gives clock / 1536,
or 40000 samples/s. At 12 and 24 kHz both divisors agree. This is a source-based
explanation consistent with the installed firmware's startup log, not proof
that the installed binary is exactly that source revision.

Use 24 kHz for subsequent functional decoding work once selected by the operator.
Do not silently substitute an arrival-rate estimate for wire metadata. The
36 kHz coverage column remains a planning target, not a qualified configuration.
The 24 kHz FT8/WSPR all-band profile needs 12 captures, so it cannot coexist
with two independent listening captures on a 13-channel receiver. Select a
smaller band profile or defer full coverage until the rate defect is resolved.

The user authorized testing 12 and 24 kHz with admin restarts. The original
36 kHz setting was restored and verified in the admin UI and a further 30-second
run (advertised 36014.074544, observed 39987.775 samples/s). All diagnostic RX
connections were closed after each run. No firmware update, network, identity,
autorun or reporting configuration was changed.

## Client fix and repeatability

Live server logs rejected `SET genattn=0` and `SET gen=0 mix=-1`. The shared
client now sends these only to Kiwi-family receivers. A socket-free session
regression verifies both receiver families and retains the common identity
command. It failed before the production fix and passed after it.

`web888_live_probe` is an explicit, excluded-from-default-build target and is
not registered with CTest. It requires `--endpoint host:port`; `--seconds`
defaults to 45 and permits 10..3600. Supply the user password only through the
transient `AETHER_WEB888_PASSWORD` environment variable, never in source or a
saved command script. It uses a temporary settings profile. Its exit code
indicates diagnostic completion, not RF or decoder qualification; inspect
sample counts, rates, waterfall counts and discontinuities.

Local evidence is under ignored `build-web888-evidence/`: `live-20261016-12k.log`,
`live-20261016-24k.log`, `live-20261016-timing.log` (36 kHz),
`live-20261016-restored36k.log`, and `live-20261016-generator-before.log` /
`live-20261016-generator-after.log`. No raw IQ or audio recording was retained.

## Local build validation

Qt 6.12/MSVC RelWithDebInfo AetherSDR and the manual probe built successfully.
All 27 selected offline receive, transport, shared IQ, decoder and connection
regressions passed in 66.46 seconds. Engine-boundary and command-plane checks
reported zero new blocking findings; capability records and strict test
registration passed. Whitespace validation passed after removing extra EOF
blank lines. Existing build warnings include FFTW_DLL redefinition and a bool
comparison in CenterLockRebindTracker; these were outside this bounded fix.
Build/test evidence: `live-20261016-full-build.log` and
`live-20261016-full-tests.log` in the local evidence directory.
## 24 kHz independent-session exercise

After the operator chose to keep 24 kHz, the saved admin selection was verified
after a server restart. A 120-second production-transport exercise retuned the
second listener from 20m to 15m, disconnected it for ten seconds, reconnected
it, and changed the first waterfall zoom while the 30m IQ capture continued.
The second listener returned to Connected and resumed PCM/WF delivery.

Final counts: 2,872,832 raw IQ samples at an observed 23,998.125 samples/s versus
advertised 24,000.004424; 1,435,904 samples per FT8/WSPR software audio output;
only the initial IQ/output discontinuity. Listener 0 received 2,873,344 PCM
frames and 2,729 waterfall rows, listener 1 received 2,628,096 PCM frames and
2,496 waterfall rows, with zero reported sequence gaps in both sessions.
The second listener's lower counts reflect its deliberate outage. All leases
closed at completion; exit 0. Evidence: `live-20261016-24k-lifecycle.log`.
This confirms transport continuity during those actions, not RF tuning accuracy
or a completed multi-pan desktop UI. The probe now fails a run if any required
PCM, waterfall, IQ or software audio output delivered no data.

## Dual-listener backend checkpoint — 2026-10-16 07:45 UTC

Web888Backend now owns up to two independent paired listening/waterfall leases
through the existing createPanadapter/removePanadapter hooks. Each lease has
its own tune, mode, filter, AGC, gain, meter, actual waterfall geometry and
lifetime guard. Pending admission counts toward the two-lease limit. A failed
or removed secondary does not disconnect the primary; reopening allocates a
new ID without renumbering survivors. Only the selected slice feeds the shared
speaker outlet, while both clean slice PCM streams remain available. This is
selection, not simultaneous speaker mixing. The primary pan remains the session
anchor and cannot be independently removed in this increment.

The desktop connection metadata and existing explanatory text advertise the
implemented limit of two. No new neutral interface, capability boolean, GUI
vendor branch or GUI-to-engine include was introduced. The 13-channel hardware
limit is not advertised as implemented interactive capacity.

Socket-free backend regressions cover pending admission, independent tuning,
per-slice PCM, active-speaker selection, per-pan frame IDs, invalid pan IDs,
secondary removal/PCM retirement, stale queued callbacks, secondary refusal,
pending cancellation and primary survival. Qt 6.12/MSVC desktop and diagnostic
builds succeeded; all 27 selected regressions passed in 66.87 seconds. Engine
boundary, capability records, command-plane, strict test registration and UTF-8
touchpoint-manifest checks pass; the generated manifest has no changes.

A 75-second live test used Web888Backend itself (not two standalone clients)
at the retained 24 kHz receiver setting. It created two panes, retuned and
selected the second, removed it, then reopened a replacement. Primary slice 0
received 1,793,024 PCM frames / 1,700 waterfall rows; secondary 1 received
947,712 / 903 before removal; replacement 2 received 594,944 / 565. All leases
were closed at completion; diagnostic exit 0. Evidence is in ignored
build-web888-evidence/dual-20261016-live-backend.log and
 dual-20261016-full-{build,tests,benchmark}.log. The manual probe selects this
path with --backend and requires at least 70 seconds.

Limits: no antenna-connected RF/decoder qualification; no rendered desktop
multi-pan verification yet; no shared-pool integration for these interactive
leases yet. The next bounded increment should verify the neutral model/UI
paths and integrate explicit interactive reservations with digital capture
accounting before enabling combined monitoring controls. Previous single-slice
statements in this document describe the earlier checkpoints.


## Reopened-pan mapping fix — 2026-10-16

Review of the production RadioModel integration found that spectrumFrameReady
uses a reusable neutral display index, while slice IDs remain monotonic. The
initial dual-listener implementation incorrectly emitted replacement slice 2
as spectrum index 2 after the model had reused display index 1. The backend
now keeps pan IDs and spectrum indices at 0/1 independently of slice IDs;
commands for pan 1 resolve its currently owned secondary slice. Queued frames
still pass the per-lease lifetime guard before any delivery.

A new regression failed before the fix (replacement slice had pan ID 2) and
passes after it. Additional socket-free coverage forwards observations from
the injectable backend into a fully wired production Web-888 RadioModel,
verifying pan removal, replacement geometry and slice-to-pan association.
The lightweight setBackendForTest helper omits pan-removal wiring, so the
integration fixture deliberately uses rebuildBackendForTest instead. This
qualifies model routing, not rendered GPU waterfall behavior.

The desktop build and 27 selected regressions passed (66.68 seconds); the
subsequently expanded model regression also built and passed. Strict engine
boundary, test-registration, UTF-8 touchpoint-manifest and whitespace checks
passed. No enforcement baseline or model production code was changed.

A further 75-second live backend run at unchanged 24 kHz received:
primary slice 0: 1,793,536 PCM frames / 1,705 waterfall rows;
secondary slice 1: 947,200 / 898; replacement slice 2: 594,944 / 564.
The diagnostic now attributes display-slot rows to their current slice owner.
Both leases closed at completion; exit 0. Local ignored evidence is under
build-web888-evidence/pan-slot-20261016-* (before/after regression, full build
and tests, and live log). No RF antenna, decoder qualification, reporting or
admin changes. Shared interactive/digital pool accounting remains next work.


## Integrated monitoring capacity checkpoint — 2026-10-16 08:40 UTC

Web888Backend now owns its digital capture pool. The engine-only
applyDigitalProfile entry point shares the current endpoint and transient
credential, reserves two listening slots on every profile request, and leaves
active-plus-pending replacement admission to the tested pool. Closing the
secondary listener does not release its reservation to digital captures.
Stopping digital monitoring preserves interactive reception; disconnecting the
backend stops both active and pending digital captures before releasing listeners.
Decoder audio has its own typed outlet, separate from interactive slice PCM.
No GUI control or neutral backend interface was added in this increment.

Socket-free tests verify disconnected refusal, two-slot reservation before any
capture opens, pending-to-active admission, second-listener creation within the
reservation, independent listening tuning, rejection of an over-budget profile
replacement without invalidating current digital audio, reservation after pan
closure, digital-only stop, and pending capture cleanup on full disconnect.
The desktop and probe built; all 27 selected regressions passed in 66.32 seconds.
Strict boundary, capability, command-plane, registration, manifest and whitespace
checks passed without enforcement-baseline changes.

The --backend manual probe now exercises the integrated pool with a three-RX
budget: two listening/WF leases plus one 30m IQ capture split to FT8 and WSPR
software audio. A 75-second run at unchanged 24 kHz retuned/selected the secondary,
removed it and reopened its replacement. It received 1,793,024 primary PCM frames
and 1,693 WF rows; secondary 946,176 / 901; replacement 594,944 / 559. Each digital
output received 892,928 mono samples. All leases closed; active and pending
digital counts were zero after disconnect, exit 0. This run verifies delivery
and cleanup, not calibrated RF, UTC alignment, decoded messages or GPU rendering.
Evidence: ignored build-web888-evidence/monitor-20261016-* logs.

Remaining work: rendered desktop verification, neutral user-facing monitoring
controls, connection of these digital outputs to timed decoder routing, and
longer combined-load qualification. The caller supplies the local receiver budget;
it does not prove exclusive server ownership or account for other clients.
Firmware admission still decides availability. Keep the receiver at 24 kHz.

## Backend timed-routing checkpoint — 2026-10-16 09:12 UTC

The backend-owned digital pool now feeds Web888DecoderRouter after publishing
its typed audio outlet. Engine callers can configure bounded decoder buffers,
arm a route from that outlet with an explicitly qualified post-filter sample UTC
anchor, and take completed jobs. Configuration alone does not arm a route;
packet arrival time is never substituted for sample UTC. The callback checks
session generation and audio lifetime again after observers return. Capture
failure disarms its routes, and digital stop clears the router before capture
teardown, invalidating outstanding jobs.

The socket-free backend/pool fixture verifies no unarmed job, explicit fixture
anchoring, a complete 180,000-sample FT8 period, continued routing through
interactive tuning/removal and refused profile replacement, and held-job
invalidation on stop. Desktop and manual probe builds passed. All 27 selected
tests passed in 77.32 seconds; strict boundary, capability, command-plane,
registration, manifest and whitespace checks passed. The synthetic 12-capture,
25-slice benchmark reported zero drops (5 represented seconds, 5.647 seconds
wall time, 1.682 seconds DSP time); this is not sustained live-load qualification.
Evidence: ignored build-web888-evidence/routing-20261016-* logs.

No receiver connection or settings change was needed for this increment.
Keep 24 kHz. This provides timed job assembly, not automatic decoder execution
or qualified live timestamps. Rendered desktop verification, neutral monitoring
controls, sample UTC/filter-delay qualification and downstream decoder execution
remain pending. No RF, decoded-message, GPS or reporting qualification is claimed.

## Timed-routing lifecycle review — 2026-10-16 09:38 UTC

Expanded the socket-free backend/pool regression to complete a timed FT8 job,
then fail its digital capture. The held job becomes invalid, the digital lease
is removed, and interactive reception remains connected. A second case stops
monitoring synchronously inside digitalAudioReady; the published audio becomes
invalid and no decoder job survives or is republished after stop.

Both cases passed with the existing lifetime guards; no production change was
needed. Five focused backend, pool, period, router and pipeline tests passed in
62.30 seconds. Build and whitespace checks passed. Evidence is in ignored
build-web888-evidence/lifecycle-20261016-{build,tests}.log. This increment used
no sockets and changed no receiver settings. Rendered desktop verification,
monitoring controls and qualified live decoder timing remain pending.

## Rendered desktop checkpoint — 2026-10-16 10:12 UTC

Launched the local desktop build with its opt-in automation bridge and an
isolated AETHER_SETTINGS_DIR. Used the existing manual Web-888 connection UI
with a transient password, then the neutral pan-add/close commands. At unchanged
24 kHz, the desktop exposed two slices/pans: primary 7.100 MHz and secondary
14.074 MHz. Framebuffer PNGs were inspected and both waterfalls visibly filled
with receiver data. The secondary initially rendered blank during startup and
filled on a later capture. With no antenna this is transport/render evidence,
not RF performance or decoded-signal qualification.

Closing and reopening the secondary retained the primary and reused display
slot 1 with a new Slice C identity. Its framebuffer again showed a populated
waterfall. Disconnect returned the model to idle with zero slices/pans and both
slots empty; the validation process was then closed. Logs, snapshots and PNGs
are in ignored build-web888-evidence/desktop-20261016-*. A scan of those artifacts
and the isolated settings store found no receiver password.

The bridge's explicit `connect ip ... web888` shortcut rejected the family
before reaching the panel; selecting Web-888 in the manual UI and using the
untyped connect command worked. This shortcut limitation remains to fix through
the existing family registry/seam, without adding a new vendor branch. No
production code changed in this checkpoint. Monitoring GUI controls, qualified
sample UTC, decoder execution and longer combined-load testing remain pending.

## Explicit connection shortcut fix — 2026-10-16 10:41 UTC

The desktop bridge rejected an explicit Web-888 family because doConnect had a
separate three-family allowlist. IConnectionAutomation now exposes available
manual families; ConnectionPanel supplies these from its actual selector items.
The bridge validates explicit requests against this provider instead of naming
vendors. Unknown families still fail before a deferred connection is scheduled.
No extra GUI vendor branch or backend capability was introduced.

Socket-free regression coverage checks Web-888 and an arbitrary fixture family
are delivered unchanged, unknown families never invoke the provider, and the
real panel advertises Web-888 only when the build includes it. The desktop built
and all three focused connection/panel/boundary tests passed (1.28 seconds).
Strict boundary, capability, command-plane, registration and manifest checks,
and whitespace validation, passed. Evidence: ignored
build-web888-evidence/shortcut-20261016-* logs. No live connection or receiver
setting change was needed; the working rate remains 24 kHz.

## 36 kHz firmware-path audit — 2026-10-16 11:09 UTC

Read-only review of the previously inspected RaspSDR/server checkout at
4dfecc4626d206e077f6f6908bd9b97ac4607cb8 traced the mismatch beyond network IQ:

- main.cpp:256-269 computes requested snd_rate, then in HF mode truncates
  rx_decim and independently truncates fpga_rx_decim to a multiple-of-256
  hardware divider. The airband branch already checks exact divisibility;
  changing to an airband clock is not an HF workaround.
- zynq/peri.cpp:237 passes fpga_rx_decim to RX_START. extensions/ext.cpp:40-57
  reports ADC clock / rx_decim, which is a different divisor at 36 kHz.
- rx/rx_sound.cpp:159-165 publishes both snd_rate and the derived frate. The
  receive path delivers pre-FIR and pre-AGC IQ callbacks and real-audio callbacks
  to extensions; its GPS filter-delay correction also uses rx_decim.
- FT8/ft8_lib/decode_ft8.cpp:675 configures FT8/FT4 monitoring with snd_rate.
  FT8.cpp:195 subscribes to real audio. wspr/wspr_main.cpp:899 subscribes to IQ;
  lines 1190-1194 derive fractional/integer decimation from frate and snd_rate.
  Thus relabeling Aether's received IQ alone cannot qualify onboard decoding.
  This is source-based risk analysis, not a measured onboard decode failure.

Exact nominal-clock arithmetic (61.44 MHz) reproduces the observed discrepancy:

| Requested | Reported divisor | FPGA factor | Effective divisor | Effective samples/s |
|---|---:|---:|---:|---:|
| 12 kHz | 5120 | 20 | 5120 | 12000 |
| 24 kHz | 2560 | 10 | 2560 | 24000 |
| 36 kHz | 1706 | 6 | 1536 | 40000 |

The 36 kHz wire rate is nominally 36014.067995, not exactly 36000; therefore a
metadata-only correction would require the divisor ratio 1706/1536, not simply
40/36. Neither correction repairs server filters, onboard decoders or GPS delay
accounting. Do not silently learn a correction from packet arrival timing.
The exact arithmetic assertions passed; evidence is in ignored
build-web888-evidence/rate-audit-20261016.txt.

Two coherent firmware approaches remain candidates, not implemented fixes:
(1) expose a genuine 40 kHz mode and consistently use its effective divisor/rate
through DSP, extensions, wire metadata and timing; audit every consumer that
assumes multiples of 12 kHz; or (2) resample 40 kHz to genuine 36 kHz at 9/10
before downstream consumers, carrying filter delay and sample timestamps through
the conversion. The latter preserves nominal decoder configuration but adds CPU,
filtering and buffering cost across all active RX channels. Updating only
rx_decim to fpga_rx_decim*256 leaves snd_rate inconsistent and is insufficient.

The server initializer accepts an index corresponding to 48 kHz, but the inspected
admin selector only offers 12/24/36. Although 48 kHz divides the nominal HF clock
exactly, it is not a validated or exposed alternative and was not forced.
No firmware or receiver settings were changed. The user permitted temporary rate
changes for investigation; return to 24 kHz after any future bounded experiment
unless a replacement is explicitly qualified. Existing installed-binary/source
identity, RF, GPS and decoder-validation limitations still apply.

## Extended combined transport check — 2026-10-16 11:39 UTC

The integrated backend probe completed a bounded 180-second receive-only run
at unchanged 24 kHz, using two listening/waterfall leases plus one 30m IQ capture
split into FT8 and WSPR audio. It retuned/selected the secondary at 20 seconds,
closed it at 40 seconds, and created its replacement at 50 seconds.

| Output | PCM/mono frames | Waterfall rows |
|---|---:|---:|
| Primary listener (0) | 4,311,552 | 4,091 |
| Secondary listener (1), before close | 946,176 | 903 |
| Replacement listener (2) | 3,115,008 | 2,964 |
| FT8 software slice (100) | 2,152,704 | n/a |
| WSPR software slice (101) | 2,152,704 | n/a |

The probe exited 0, retired listeners 2 and 0, and checked active/pending digital
capture counts were zero after disconnect. No failure signal terminated the run.
Evidence: ignored build-web888-evidence/extended-20261016-live.log. These totals
confirm longer combined delivery and cleanup; this backend probe does not count
individual packet gaps or establish sample-clock accuracy. Three minutes at
three RX leases is not maximum-capacity or overnight stability qualification.
No decoder ran, no reporting was enabled, and no persistent setting changed.
No RF antenna was connected. The remaining work includes monitoring controls,
qualified decoder timing/execution, and a coherent firmware fix for 36 kHz.

## Live-probe progress checks — 2026-10-16 12:08 UTC

Review found that the manual backend probe required only nonzero stream totals,
so an early delivery followed by a silent stall could still pass. The diagnostic
now measures maximum arrival gaps for each listener PCM/waterfall and each
digital output, fails gaps over five seconds, and requires fresh delivery at
finish. The deliberately retired secondary is checked for freshness before its
40-second close rather than incorrectly requiring it at the end. Only nonempty,
current audio or nonempty waterfall rows advance progress. Production receiver
behavior is unchanged.

The probe built and a 75-second live run at unchanged 24 kHz passed (exit 0):
primary 1,793,536 PCM frames / 1,695 waterfall rows; secondary 947,712 / 895;
replacement 594,944 / 567; both digital outputs 891,904 mono samples each.
Maximum observed PCM/waterfall arrival gaps were 209/193 ms on the primary,
173/173 ms on the secondary, and 61/68 ms on its replacement. Digital maximum
gaps were 193 ms each. All leases were released and digital active/pending
counts were zero after disconnect. Build and whitespace checks passed.
Evidence: ignored build-web888-evidence/progress-20261016-{build,live}.log.

These are host callback-arrival intervals, not sample gaps, wire sequence loss,
RF performance, or clock qualification. The five-second threshold is a diagnostic
stall criterion, not a decoder-quality acceptance limit. No reporting, decoder
execution, firmware changes or persistent receiver setting changes occurred.

## Reentrant pan teardown fix — 2026-10-16 12:41 UTC

Review found a demonstrated event-order defect: closing a secondary emitted
sliceRemoved before panRemoved. A synchronous slice-removal observer could open
and admit a replacement using display slot 1, then the old panRemoved event
removed that replacement's pan from RadioModel. The added socket-free full-model
regression failed before the fix (replacement slice existed, pan count was one).

Teardown now retires the reusable pan slot before publishing slice removal.
A generation check suppresses the remaining old-session slice notification if
an observer changes the session. The same regression now preserves both pans
and the replacement slice. No new threads, interfaces or receiver settings.

Desktop and manual probe builds passed; all 27 selected tests passed in 90.32
seconds. Strict boundary, capability, command-plane, registration, manifest and
whitespace checks passed. Evidence: ignored build-web888-evidence/
reentrant-20261016-before.log and reentrant-20261016-full-* logs. This increment
used socket-free observations rather than another live receiver run. The device
remains at 24 kHz; all earlier RF/timing/decoder limitations remain.

## Rate recheck after operator PPS change — 2026-10-16 13:08 UTC

The operator reported enabling ADC correction using GPS PPS. Preserved that
configuration and ran the existing 60-second transport-rate probe without any
admin writes. Advertised IQ rate was 24000.000033 samples/s; the final host
arrival-rate estimate was 24000.517 samples/s. Received 1,432,576 IQ samples and
716,032 samples on each FT8/WSPR software audio output. IQ/audio each recorded
one initial discontinuity, with no subsequent discontinuities. Both PCM listeners
reported zero sound-sequence gaps and 1,359 waterfall rows each. Exit 0; both
listeners and IQ capture closed. A subsequent public status read reported users=0
of 13 and adc_ov=0.

The public GPS counters advanced from gps_good/fixes=25 to 26 during this check.
Those server fields are not an independent PPS phase, absolute UTC, GPS lock or
filter-delay qualification. No decoded messages or RF performance were tested.
The arrival estimate includes network scheduling jitter and must not be used as
a clock correction. The selected rate remains 24 kHz; the 36 kHz integer-divider
mismatch is not resolved by this result. Evidence: ignored
build-web888-evidence/pps-change-20261016-{live.log,status.txt}.

## Development-window closeout — 2026-10-16 13:38 UTC

Stopped at a reviewed checkpoint before the 13:45 UTC deadline. Local changes
remain uncommitted on web888-native; nothing was pushed or published. Completed
this window: two interactive listening/waterfall leases, reusable pan-slot fixes,
backend-owned shared digital capture with two reserved listening slots, explicitly
anchored timed decoder job assembly, lifecycle regressions, connection shortcut
repair, and stronger manual transport diagnostics. Actual desktop framebuffer
captures verified both waterfalls and replacement-pan rendering. The latest
production fix passed all 27 selected regressions plus strict repository checks;
the connection shortcut separately passed three focused tests.

The last live rate check after the operator's PPS change passed at 24 kHz with
zero listener sequence gaps; cleanup was followed by server users=0. No further
receiver leases are being opened for closeout. Preserve the operator's PPS
configuration. No RF antenna, decoded-message or absolute sample UTC qualification
has been claimed.

Next work, requiring a new development window: expose monitoring controls through
neutral model interfaces; qualify sample UTC and filter delay before automatic
decoder execution; exercise known RF signals and decode results with an antenna;
then qualify higher capture counts. The 36 kHz firmware integer-divider defect
is traced but unfixed, and requires a coherent server-side rate strategy. Keep
24 kHz as the working setting. The final source, test and evidence changes are
preserved for review and eventual separation into local commits.

## Upstream integration — 2026-10-16

Fetched origin and merged 136 incoming main commits through 7864ef54 into
web888-native with signed local merge c5acea22 (signature verified). Preserved
all uncommitted Web-888 work in stash web888-before-upstream-20261016, restored
it after the merge, and retained that stash as a recovery copy. Nothing pushed.

Resolved RadioModel's conflict by retaining transient connection options and
upstream's sessionBindAddress argument. Kept both local Web-888 and upstream
AX.25 touchpoint metadata; regenerated the manifest. The stash-restore test-file
conflict retained upstream sleep/PipeWire tests and the manual live probe.

Upstream's CanonWindow connection-panel redesign caused a demonstrated linker
failure in our panel test. Added CanonWindow.cpp and PersistentDialog.cpp to that
target, matching upstream's panel test dependencies. The full desktop and manual
probe then built; all 29 selected regressions passed in 92.51 seconds. Strict
boundary, capability, command-plane, registration, manifest and whitespace checks
passed. Synthetic 12-capture/25-slice benchmark: zero drops, 5.678 seconds wall,
1.830 seconds DSP for five represented seconds (not live-load qualification).

Reviewed upstream typed model-control routing, explicit waterfall persistence
ownership, native receive-audio export gates, connection UI and audio lifecycle
changes. No new Web-888 capability is advertised merely because an interface now
exists: native audio-device export remains unqualified, and existing digital
monitoring/timing limitations remain. Hardware and rendered desktop were not
retested during this source integration; receiver settings were untouched.
Evidence: ignored build-web888-evidence/upstream-20261016-* logs.


## Monitoring controls and decoder timing — 2026-10-16

Added Tools → Digital Monitoring through a neutral DigitalMonitoringModel and
capability-declared digital-monitor extension (plan/start/stop/status). Controls
select 80–10m FT8/WSPR/FT4 presets, preview shared captures, reserve two listener
slots, and show active/pending captures and cumulative output samples. Default
40/30/20/10m FT8+WSPR requires seven captures plus two reserved listeners at 24 kHz.
The budget includes reserved listeners; users must reduce it for other clients
or onboard jobs. Admission remains authoritative. Closing the window leaves
captures running; Stop or receiver disconnect releases them. No decoder reporting
or automatic timed decoding is enabled. No receiver settings changed.

Validation: desktop and manual probe built. All 29 selected regressions passed
in 90.27 seconds, including 7.5/15/120-second period boundaries, partial-period
rejection, gaps and revoked stream lifetime. Added extension invalid-input and
capacity-refusal tests. Final connected socket-free GUI test passed, including
budget refusal/recovery and disconnect disabling; rendered screenshot inspected
with fonts and theme tokens. Strict engine boundary, capability, command-plane,
registration and manifest checks passed (known baseline warnings only).
Evidence: ignored build-web888-evidence/monitoring-20261016-*, monitoring-final-*,
monitoring-gui-test.log and monitoring-controls.png.

A bounded 60-second live probe delivered 1,432,064 IQ samples, 715,775 samples per
digital output, two PCM/waterfall streams with zero reported sound sequence gaps,
and only the initial IQ discontinuity. Advertised rate 23999.999987 Hz; arrival
measurement 23996.183 samples/s is transport evidence, not ADC clock qualification.
Its initial diagnostic counted repeats together with backward timestamps; that
counter must not be interpreted as evidence of backward clock movement.

A refined 15-second probe distinguished them: 689 IQ blocks, one initial zero
timestamp, 688 plausible-shaped timestamps, 687 comparisons, **672 exact repeats,
zero backward steps**, maximum step error 0.981126193 seconds against the expected
512/24000-second block duration. The first six nonzero headers were identical
(age 0, week second 485935, fractional nanoseconds 401000843). Thus solution age
zero does not qualify a sample clock. The source checkout at
4dfecc4626d206e077f6f6908bd9b97ac4607cb8 sets each receive buffer's ticks to zero
(rx/data_pump.cpp:93), while rx/rx_sound.cpp:530–550 uses those ticks to calculate
SND time. This is consistent with the observation; the running firmware's exact
binary/source identity has not been independently established.

Both live probes exited successfully and released their sessions; subsequent
public status reported users=0, 24 kHz and adc_ov=0. No RF/decoder/GPS sample-time
qualification is claimed. Next timing work needs a qualified per-buffer clock or
a separately measured host-clock mapping with explicit uncertainty; repeated SND
headers cannot arm decoder routes. GPS PPS frequency correction is distinct from
sample-time timestamping and was left untouched.

