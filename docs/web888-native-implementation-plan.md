# Native Web-888 support for AetherSDR

Local history update — 2026-10-02: this work is organized into five signed local
commits: transport, interactive reception, shared captures, decoder audio routing,
and external decoder execution. Nothing has been pushed. References below to
uncommitted work describe the earlier dated checkpoints. The pre-merge recovery
stash and a separate pre-split file snapshot remain available locally.

Prepared 2026-09-25; local development started 2026-09-29. The original
inspection was planning-only. The continuation below records subsequent work;
signed local commits now exist; no PRs, firmware changes or live-radio operations have been performed.

## Upstream integration — 2026-10-06

Merged 12 upstream commits through `5f09371e` into `web888-native`, signed
merge `a5eb00a0`. The merge was conflict-free. Reviewed the unified title bar,
connection-panel additions, shared test compilation changes and RadioModel
callback lifetime fix against the local Web-888 changes; no additional source
fix was required.

The Qt 6.12/MSVC desktop build succeeded. All 27 selected offline receive,
transport, IQ, decoder and connection tests passed in 72.46 seconds; the new
`unified_title_bar_test` also built and passed (0.35 seconds for its CTest run).
Engine boundary, touchpoint manifest, capability records, command-plane,
generated seam, test registration, frozen CI gate, Qt pin and whitespace
checks passed. Repeatable commands and output are in
`build-web888-evidence/upstream-20261006-*`. No hardware, public receivers,
transmissions or upstream publishing were involved. Linux/macOS and sanitizer
runs remain unverified locally.

## Upstream integration — 2026-10-04

Merged 59 upstream commits through `8df6db2f` (v26.10.1) into `web888-native`.
Both additions were retained in the RadioModel test-access declarations and
architecture classification table. The merge is signed locally as `b8cc5e7d`.

Upstream `f4e463c2` moves desktop audio commands to `requestSliceAudio`.
Web-888 now implements that adapter for its existing speaker gain and mute;
invalid values, other slice IDs, disconnected requests, external suppression
and unsupported balance are explicitly refused. Clean decoder PCM remains
independent of speaker gain/mute. The new behavioral regression failed on the
unfixed merge at the gain-dispatch assertion (26 other selected tests passed).
Evidence: `build-web888-evidence/upstream-audio-before.log`.

After the fix, the Qt 6.12/MSVC desktop build succeeded and all 27 selected
offline tests passed in 67.52 seconds. The synthetic worker benchmark represented
five seconds across 12 captures/25 slices in 5614 ms wall time (1717.752 ms DSP),
with zero dropped blocks; this is not real-time hardware qualification.
Logs and the repeatable command script are under
`build-web888-evidence/upstream-20261004-*`. Engine boundary (102 existing
findings, zero blockers), capability records (70 booleans), command plane (369
tracked uses), manifest, seam table, test registration, frozen CI gate, Qt pin,
build-options documentation and whitespace checks passed.

No receiver was contacted, and hardware qualification remains outstanding.

## Local implementation progress — 2026-09-29

### Current implementation boundary — 2026-10-02

**Post-merge build and decoding update:** Qt 6.12.0 (the pinned package revision)
is installed under `C:\1_HAM\Qt\aether-pinned`, with the older kit retained.
The merged desktop rebuilt in `build-web888-qt612` and all 24 selected offline
regressions passed in 64.76 seconds. The local launcher now uses this build and
its Qt runtime. Optional RADE/DSTAR remain disabled. GPU spectrum rendering is enabled
(`AETHER_GPU_SPECTRUM=ON`; the old `ENABLE_GPU_SPECTRUM` argument was unused); no credential-store dependency was added by this upgrade.

The new single-job decoder process adapter subsequently built and passed four
focused lifecycle/routing/WAV tests and opt-in real WSJT-X 3.0.2 decoding for all
three modes. FT8 and FT4 use prepared PCM fixtures; WSPR uses the existing
synthetic IQ-split WAV. This supplies bounded external decoder execution and
known-message software evidence, not live monitoring or receiver timing. See
[decoder process validation](web888-decoder-process-validation.md) for provenance,
commands, bounds, fixture preparation and results. No WSJT-X GUI or transmitter
was opened. The earlier Qt-blocked checkpoint below is retained as history.

**Upstream integration — 2026-10-02:** `web888-native` was fast-forwarded from
`20d022d5` to `81806bb3` (37 upstream commits), then the existing uncommitted
Web-888 work was restored. The recovery stash `web888-pre-upstream-20261002`
is retained. Three restore conflicts were resolved: both required RadioModel
includes retained, current HL2 manifest description retained, and the warning
comment reconciled with manual recovery. Nothing was published.

Relevant upstream changes and resulting work:

- `25da98de` requires Qt 6.12 or newer and supplies a pinned 6.12.0 setup script.
  The local Qt 6.8.3 kit cannot configure this merged tree. Upgrade the build kit,
  reconfigure, rebuild and rerun the offline suite before treating the merge as
  runtime-validated. The existing desktop executable predates this integration.
- `142f1d63` routes receive frequency/filter/AGC intents through the backend with
  slice-identity and thread guards. Web-888's existing setters remain reachable
  through the default adapters; mode still uses `setSliceMode`. Future controls
  should use these requests, preserving their pan/filter/AGC semantics, without
  adding another model dispatch path. Add Web-888 to the new receive-contract
  coverage when extending its desktop control integration.
- `222815ff` adds receive-contract tests and corrects Icom capacity. It does not
  establish Web-888 capacity or supply the shared digital monitoring UI.
- `2d219482` removes the unused backend `waterfallRowReady` outlet. Web-888 already
  converts its transport waterfall into `spectrumFrameReady`, so no replacement
  signal is required. The generated seam now contains 51 signals.
- Non-Flex control availability and radio-wide span fixes (`2d571294`,
  `66982001`) should inform the later listening/monitoring UI. They do not add an
  FT8/WSPR decoder or qualify Web-888 hardware.

Merged-tree static checks passed: engine boundary (102 existing findings, zero
blockers), capability records (71 booleans), command plane (406, matching the new
upstream baseline), test registration, touchpoint manifest, generated seam,
frozen seven-test PR gate, Qt pin consistency and whitespace. The attempted build
stopped at the Qt-version check, before compilation or CTest. All passing build
and runtime results below are historical results on the earlier integration base,
not validation of `81806bb3` plus the local changes.

| Area | Implemented locally | Still required |
| --- | --- | --- |
| Interactive listening/waterfall | Experimental desktop connector and paired receiver backend; offline lifecycle/UI tests | Live receiver acceptance |
| Shared digital capture | Sound-only IQ, software USB splits, bounded background worker | Hardware passband, gain and timing qualification |
| Capacity planning | Band/mode window planner, listening reservation, transactional capture pool | Shared live UI allocation and monitoring controls |
| Decoder audio preparation | Bounded periods/routes, cancellation witnesses, PCM16 WAV serialization and single-job process adapter | Verified production clock and monitoring scheduler |
| FT8/FT4/WSPR message decoding | External WSJT-X tools exercised through the adapter using known-message files | Parsed spot delivery, desktop integration and sustained multi-band decoder load |
| Signal evidence | Paced 12-capture/25-route synthetic load; actual FT8/FT4/WSPR fixture decoding | FT8/FT4 IQ-path acceptance, then live reception |
| WSJT-X | Existing status/spot client remains available | Per-instance audio input mapping; UDP does not carry PCM |

Digital components are engine primitives and test fixtures; the desktop does not
yet offer an enabled automatic multi-band decoding profile. A clean synthetic
signal path and fixture decoding do not imply qualified live reception.

Latest broad offline checkpoint: the Windows desktop rebuilt and all 24 selected
regressions passed, including four WSPR input rates, capture startup/progress
deadlines, capture-specific route cancellation and stalled-consumer bounds.
Architecture baselines remain unchanged. See
[worker validation](web888-iq-worker-validation.md) for reproducible evidence.

### Development-window handoff — 2026-09-30

The final scheduled review found no additional production defect in the decoder
handoff. A subsequent six-test checkpoint verifies real routed FT8/FT4/WSPR jobs,
initial partial-period discard, PCM16 output and cancellation before serialization;
see [WAV handoff validation](web888-decoder-wav-validation.md). All changes remain
local and uncommitted on `web888-native`. Evidence scripts/logs and the synthetic
WSPR WAV are in the ignored `build-web888-evidence` directory; they are local
artifacts, not files included in a future upstream patch.

Next bounded work, in dependency order:

1. The external receive-only decoder adapter and known-message file tests are now
   implemented (2026-10-02). Next add parsed results, bounded multi-band dispatch
   and FT8/FT4 IQ-path fixtures; then qualify sustained decoder CPU/memory load.
2. Qualify the sample-to-UTC mapping on the delivered receiver using the
   [timing procedure](web888-timing-qualification.md). Preserve raw observations;
   do not substitute network arrival time or infer UTC from unqualified GPS fields.
3. Verify one interactive receiver and one sound-only capture on the user's unit:
   admission/refusal, negotiated rate, IQ handedness, usable passband, gain,
   audio continuity and clean cancellation. Record firmware and configuration.
4. Connect the tested engine components to shared monitoring controls and live
   capacity accounting, then expand only within the measured receiver budget.
   Keep listening/waterfall leases independent of digital captures and reserve
   their capacity before starting the digital profile.
5. Run known-message decoding under the intended multi-band load with independent
   listening, competing clients and reconnects; record queue/drop/CPU/memory and
   timing evidence over 30–60 minutes before calling the profile hardware-ready.

At the development-window stopping point, no live receiver, external decoder,
firmware change, transmission or upstream publication has been used. The local
desktop launcher opens the experimental interactive connector; automatic digital
monitoring is not yet an enabled desktop feature. The scheduled window ends at
2026-09-30 09:49:55 UTC; further development requires a subsequent user instruction.

### Revised goal: shared digital captures and independent listening

The operator's revised priority is continuous FT8/WSPR (and selected additional
modes) across 80–10m, with freely tunable listening and waterfalls on demand.
The [frequency coverage table](web888-frequency-coverage.md) is the planning
source of truth: with provisional 1 kHz outer margins, the base FT8/WSPR profile
needs 10 captures at 36 kHz, 12 at 24 kHz, or 13 at 12 kHz. Including FT4 on all
eight bands adds two captures at 36 kHz. These are calculated budgets, not a
claim that this unit has that many free channels or that its passband is verified.

Use one sound-only filtered-IQ session per required RF window, split locally into
independent USB audio outputs. A decoder slice does not inherently consume a
hardware receiver. Background monitoring opens no waterfall. Interactive
listening initially retains its own paired SND/W/F session so tuning cannot
interrupt a digital capture. Count capture leases, interactive leases and pending
opens separately from software slices; reserve listening capacity before enabling
a monitoring profile. No administrator rate changes or silent time-sharing.

IQ reception/channelization is now an early required phase. The old “optional IQ”
phase below is limited to general-purpose IQ export. Keep the completed PCM,
session identity, lifecycle and interactive connection work.

The first local IQ increment implements an immutable opt-in IQ transport,
20-byte-header/endian-aware bounded decoding, negotiated fractional sample rate,
sequence/discontinuity handling, and `Web888IqCapture` feeding
`Web888IqSplitter`. Each capture can produce separate 12 kHz mono USB outputs
with explicit dial references and generation labels. A streaming mixer, 513-tap
FIR and resampling retain state across packet boundaries. Rate/coverage mismatch
fails the capture; reconnect/gap/change resets filter history. The desktop
connector still starts the interactive PCM receiver. The multi-band scheduler,
decoder adapters and UI activation are not yet implemented. A pure
`Web888CapturePlan` now computes the preset windows and reserves listening
capacity; it makes no connections and explicitly reports over-capacity profiles.
IQ here is filtered
and post-AGC; it is not raw ADC data or evidence of coherent receivers.

Offline IQ tests cover endian/order, malformed/oversized payloads, missing and
fractional rates, sequence wrap/gaps, opposite RF offsets, channel isolation,
irregular packet boundaries, insufficient bandwidth and reentrant teardown.
See [IQ validation](web888-iq-validation.md) for reproducible evidence and limits.

A subsequent increment added `Web888CapturePool`: owner-thread transactional capture
admission with reserved listening headroom, pending-open accounting, exact-window
reuse and non-reused capture IDs. All new windows must produce valid,
coverage-checked IQ before committing a profile change. Busy, invalid bandwidth,
startup failure or a 30-second deadline rolls back only pending captures.
Existing audio continues until commit; removed sources are revoked before the
new profile is announced. A failed committed capture does not stop survivors.
Changes requiring extra temporary capacity are explicitly rejected rather than
interrupting existing capture windows. Reducing a profile remains possible after
the available budget shrinks. See [pool validation](web888-capture-pool-validation.md).

The pool remains an engine component, not a desktop monitoring switch.
`Web888IqProcessor` now moves channelization onto one shared worker per pool,
with bounded input/result queues, per-capture limits, explicit gap marking on
overflow, terminal-error delivery independent of a full audio queue, and
owner-thread callbacks. Capture admission waits for worker-validated data.
Stopping cancels queued work and prevents late publication. Review added guards
for destroyed processing services and nested event loops during result delivery.
See [worker validation](web888-iq-worker-validation.md) for bounds and a synthetic
12-capture/25-slice timing measurement. A subsequent 60-second independently paced
synthetic run completed all 25,920 input blocks with no drops or unexpected
sample-index gaps across the 25 outputs. Next: decoder timing/routing and longer
load qualification with downstream consumers. Neither synthetic test is hardware
acceptance; the desktop digital monitoring path remains disabled.

Admitted IQ captures now expire after ten seconds without produced slice audio.
Initial coverage-checked IQ starts the deadline, allowing FIR warmup. Expiry
stops that capture and revokes retained audio through the same
failure path used by the pool, leaving unrelated captures intact. Startup still
has both a per-capture 30-second startup deadline and the pool's separate
30-second profile admission deadline. This is a local watchdog,
not automatic reconnection or a measurement of receiver sample timing.

`Web888DecoderPeriodBuffer` now assembles bounded complete periods from 12 kHz
audio only after an explicit verified sample-to-UTC anchor is supplied. Initial
partial periods are discarded; gaps, source changes and revocation invalidate
the anchor. Synthetic fixtures cover 7.5-, 15- and 120-second periods and exact
sample continuity. No production clock source or decoder invocation is connected.
See [period validation](web888-decoder-period-validation.md). Timing-quality policy
and actual decoder/audio adapters remain to be implemented.

`Web888DecoderRouter` now adds per-mode period routing and an aggregate sample
budget: at most 64 routes, one assembling period and one waiting job per route.
The full 25-slice profile reserves 120.96 MB of float audio storage. Waiting jobs
use latest-complete replacement and round-robin selection; route changes and
reanchoring revoke previously taken jobs. Rejected profile edits preserve work.
See [routing validation](web888-decoder-router-validation.md). Actual decoder
execution, verified live timing and desktop integration remain pending.

A combined synthetic fixture now runs 121 represented seconds of a shared 30m
IQ window through the background worker and all three period routes. It verifies
8 FT8-sized, 16 FT4-sized and one WSPR-sized periods, expected recovered tones,
fixture-clock alignment, zero drops and route revocation following a sequence
gap. All six selected regressions passed. This is signal-path qualification,
not message decoding or real-time performance evidence.

The subsequent desktop regression checkpoint rebuilt the application and passed
all 18 selected offline tests, covering the digital components alongside
interactive connection/UI, waterfall handling, family switching and thread
affinity. Architecture checks found no new violations. See the routing validation
notes for the 2026-09-30 build/test evidence and remaining integration limits.

The full 12-capture/25-route profile subsequently passed a 121-second independently
paced synthetic run: 52,272 input blocks and 201 complete routed periods, with
zero input/result/job drops or unexpected sample gaps. The consumer collected
jobs immediately; decoder CPU, device I/O and GUI load were not included. This
extends the earlier paced worker-only check through period assembly/routing.

An in-memory WAV serializer now provides a bounded 12 kHz mono PCM16 handoff for
complete current jobs, rejecting revoked jobs, incorrect period sizes, unaligned
timing and nonfinite samples. It does not write files or launch a decoder. See
[WAV validation](web888-decoder-wav-validation.md); production timing and actual
decoder acceptance remain pending.

Firmware timing review confirmed that IQ header time is modulo one GPS week and
can be redacted or initially zero. The transport now preserves its ten timing
bytes verbatim for diagnostics without treating them as UTC or arming decoders.
See [timing qualification](web888-timing-qualification.md) for source evidence
and the required epoch, continuity and delay checks before live timed decoding.

Review tightened decoder-profile validation to reject slice IDs and filter
limits that the IQ splitter cannot accept. Regression tests first demonstrated
the defect, then confirmed invalid edits preserve existing queued jobs and
memory reservations. All five focused routing/capture tests passed after the fix.

An encoded WSPR fixture now exercises the shared 30m IQ window, software slices,
period router and PCM16 WAV conversion using the repository's reference-message
encoder. It checks preservation of all 162 known symbols against a stronger
adjacent FT8-dial tone. See [signal-fixture validation](web888-wspr-signal-validation.md)
for scope; this is not a general weak-signal decoder or an over-the-air reception.

The signal fixture now supports explicit atomic WAV export for later decoder
acceptance. A local 120-second WSPR WAV was generated after all symbol checks and
independently read with Python's WAV parser; its hash and expected signal are
recorded in the signal-validation notes. Repository inventory confirms that the
existing WSJT-X client handles status/spots, not audio decoding. No actual
FT8/WSPR audio decoder has been integrated or qualified yet.

Decoder routes can now be explicitly disarmed by capture ID for clock/source
invalidation. This revokes queued and taken jobs for that capture while preserving
unrelated routes and the profile's memory reservations. Fresh timing anchors are
required before affected routes resume; automatic clock monitoring is not implied.

Encoded WSPR preservation now passes at 12 kHz, 24 kHz, 36,000.125 Hz and 48 kHz
software input rates, including full-message fractional resampling into routed
PCM16 WAV periods. All eleven selected rate/decoder/capture regressions passed;
these results do not establish receiver rate availability or general decoding.

Full-profile downstream overload qualification now feeds four represented minutes
of audio per route without consumption. The router retains exactly one latest
period on each of 25 routes and replaces 377 older jobs, with correct timestamps
and unchanged reservations. All five focused regressions passed. This is an
accelerated buffering test, not a paced receive/decoder-load measurement.

The user authorized productive local work and review/fixes until
**2026-09-30 09:49:55 UTC (04:49:55 America/Chicago)**. A thread follow-up is
scheduled every 30 minutes within that window. Prioritize the next tested
increment; do not publish, use public radios, or claim hardware acceptance.

The operator authorized local implementation while retaining an upstream-facing
plan. Work is on `web888-native`, based on `20d022d5`. The Web-888 is still in
transit, so every hardware acceptance criterion remains pending.

Completed transport prerequisites:

- Process-wide session-ID allocation shared by Kiwi and Web-888 clients, with
  no clock dependency, an atomic counter, randomized starting point, signed
  firmware URI compatibility and permanent refusal on exhaustion.
- Immutable sound-only sessions, preserving default paired SND/W/F behavior.
  Web-888 sound-only requests use `/no_wf/<id>/SND`; no waterfall socket is
  opened, and capability/cleanup paths cannot report a nonexistent waterfall.
- Injectable transport-opening methods for socket-free startup tests. No fake
  firmware server or public receiver is used.
- Session-generation fences stop an old queued secure retry from opening
  transports for a replacement connection, and stop partner startup after
  cancellation during sound-transport opening.
- Six targeted tests built and passed on Windows with Qt 6.8.3/MSVC 19.44.
  See [validation evidence](web888-transport-validation.md) for scope and limits.

The next local increment is now implemented in
`src/core/backends/web888/Web888Backend.{h,cpp}` and compiled into aethercore:

- One receiver with paired SND/W/F, engine-owned tuning/mode/filter setpoints,
  AGC mode, speaker gain/mute, reported level and frame-bound spectrum geometry.
- Immutable independent-receiver admission: reject camping/monitor offers and
  HTTP redirects, and disable the shared client's proxy-host TLS fallback.
  The existing auxiliary receiver constructor retains its previous policy.
- Generation-guarded owner-thread delivery; revocable 24 kHz stereo slice and
  speaker PCM; no decoder mute when the speaker is muted; deferred client
  destruction safely unwinds callbacks during disconnect.
- A socket-free backend test covers pending/connected/refused states, control
  validation, clean versus monitored audio, malformed input, stale queued PCM,
  epoch invalidation, reentrant cancellation/reconnect and seam thread affinity.
  Seven targeted tests pass on the Windows build.

The next local increment adds a manual connection entry, a neutral backend
factory, and terminal failure handling. Choose **Connect by IP → Web-888
(experimental RX)** and enter a direct host/IP with optional port (default
8073). The optional password is used for one attempt, cleared from the form,
and never saved. Native routes do not enter startup auto-connect profiles.
WebSockets-disabled builds omit the entry and refuse the family without
falling through to Flex. A per-feature recovery record prevents both model
retry paths and the desktop reconnect display from repeating native failures.
Other backends retain their existing retry policy.
The complete local desktop executable now builds, and twelve selected offline
tests pass, including the real factory/model refusal path, UI handoff, family
switches and connection-window sizing. Use `C:\1_HAM\Start-AetherSDR-Web888.cmd`
to start this workspace build with its Qt runtime available.

This is not completion of Phase 1. Credential persistence, complete capability
negotiation, saved-state integration and hardware qualification remain. The initial implementation
keeps asynchronous sockets and decoding on the owner thread, avoiding a new
unbounded cross-thread data queue. Worker extraction and load measurements
must precede a multi-receiver performance claim. Engine setpoint echoes do
not establish firmware acknowledgement. See
[native validation](web888-native-validation.md).

Keep upstream delivery split between the collision fix, sound-only transport,
approved native backend/connector and later slice/pan allocation. The full
native design still needs upstream RFC review. Do not label local authorization
as a lifted upstream freeze.

## Continuation checkpoint — 2026-09-29

The requested clone is now available at `C:\1_HAM\AetherSDR`, from
`https://github.com/aethersdr/AetherSDR.git`. Inspected HEAD is
`20d022d50566b7d5aa8fc85ddf66d00923c82117` (v26.9.5 release preparation).
The original inspection below remains historical provenance; its old checkout
restriction is resolved for this workspace. Source links below deliberately
retain their original pinned revision.

Compared with the original `43df9732` baseline:

- `IRadioBackend` adds `setPanPixelWidth()`. Preserve the server's negotiated
  waterfall FFT size; this optional display hint must not imply arbitrary
  Web-888 FFT sizes.
- `RadioCapabilities` adds the optional `panZoomModes` record. Leave it absent
  for Web-888 unless its specific band/segment operations are implemented;
  ordinary center/span tuning is a separate capability.
- `RadioModel` now parses indexed meter IDs through `MeterModel::splitMeterId`.
  Publish `SLC<n>:LEVEL` with matching meter definitions for independent slice
  meters; do not route all readings through the unindexed match-any ID.
- Non-command-plane connections now skip the Flex GUI handshake and RF-gain
  query. Preserve those upstream guards during backend integration.
- The neutral spectrum path still derives waterfall rows from producer frames.
  Preserve one producer and one rendering path, with truthful row geometry.
- The inspected Kiwi client/protocol, PCM frame contract, TCI server/map,
  decoder model, RadioStateMemory, ConnectionPanel and digital-mode GUI files
  had no differences from the original baseline before local edits. The transport derived
  session IDs from milliseconds alone, so concurrent opens remain a concrete
  prerequisite addressed by the first local increment above.

Current repository guidance still requires an approved design for a new
backend. The earlier issue-body freeze is historical evidence: live issue
retrieval failed during this refresh, so its present status is unverified.
No maintainer approval or lifted freeze is claimed. A local experimental
implementation and an upstream contribution are separate decisions.

Phase 0 continues in [the RFC draft](web888-native-backend-rfc.md), which
records the proposed contract and a hardware evidence matrix. No device
endpoint or current hardware captures have been supplied. The first PATH check
did not find build tools; subsequent inspection found Visual Studio Build Tools
2022, and Qt 6.8.3 was installed locally. Current compilation/test results are
recorded in the validation evidence linked above.

## Recommendation

Implement one native, receive-only Web-888 backend per radio session, with hardware capture leases, software decoder slices, interactive listening leases and a separate panadapter table. Reuse AetherSDR's existing Kiwi/Web-888 protocol client and parsers. Retain server-demodulated PCM for interactive listening; implement shared IQ capture and host splitting early for background digital reception. Virtual audio and general-purpose IQ export remain separate integrations.

Basic multi-slice and multi-waterfall support should not require firmware changes. However, the server's session allocator couples sound and waterfall resources: independent UI objects do not mean independent firmware resource pools. Do not promise 13 slices plus an additional unrestricted pool of 13 waterfalls.

There is also an upstream sequencing gate: the live bodies of [#5554](https://github.com/aethersdr/AetherSDR/issues/5554) and [#5262](https://github.com/aethersdr/AetherSDR/issues/5262), checked during this inspection, record a maintainer decision dated 2026-09-11: no new radio backends until aetherd is complete. Prepare the design now; implementation should follow the freeze being lifted or an explicit maintainer exception. Align the work with #5262 M6, which already plans to move Kiwi behind the backend interface, and its standalone-receive discussion #3894. This is not a reason to duplicate the current external-receiver path.

## Inspection baseline and checkout status

- AetherSDR: `43df97327ac8f393acefd473f2a6278313636c30`, main, dated 2026-09-25.
- RaspSDR/server: `a66101952c8dec3394e8cb55b3bac64decae680e`, shallow reference checkout.
- AetherSDR working tree was clean after inspection. No builds or runtime tests were run; conclusions below are source-based, with historical hardware observations identified as such.
- Requested destination: `C:\1_HAM\AetherSDR`. Filesystem permission was not granted for that directory, so that portion remains incomplete. The full Git clone used for inspection is at `C:\Users\jebcampbell\.codex\.chatgpt-projects\g-p-6a71244d1120819180924630940d3571\AetherSDR`. The firmware reference is in the adjacent `Web888-server` directory. No synced `sources/` files were changed.

Repository guidance inspected includes AGENTS.md, CLAUDE.md, GEMINI.md, CONTRIBUTING.md, the Constitution, GOVERNANCE.md, README build/developer notes, docs/DEVELOPER-GUIDE.md, .github/copilot-instructions.md, CODEOWNERS, the PR template, and tests/README.md. The firmware repository's AGENTS.md was also read. Apply these constraints throughout:

- Approved RFC/design authority for the new backend, threading/audio changes, and connector UX; identify the open-source protocol authority.
- C++20/Qt6; engine work in `aethercore`; vendor wire logic below `src/core/backends/`; no new concrete-backend casts or family checks in shared behavior.
- New capabilities are optional feature records with implemented verbs and consumers, not additional loose booleans. Existing capability fields may be used.
- All backend signals emit on the owner thread; private workers have bounded teardown, generation fences, and no emissions after disconnect.
- AppSettings/RadioSettingsScope, atomic versioned feature documents, QtKeychain for credentials, no new flat settings keys.
- Accessible capability-based controls; read theme/a11y guidance before UI implementation. Do not copy older documentation's superseded “hide unsupported controls” guidance.
- Signed commits and human review when implementation is authorized; no feature entries in CHANGELOG.md. Register tests in tests/tests.cmake, not root CMakeLists.txt, and do not grow the frozen PR test allow-list.

## What already exists

| Area | Evidence and implication |
|---|---|
| Backend interface | [IRadioBackend.h:83](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/src/core/backends/IRadioBackend.h#L83) defines the lifetime/thread contract. Frequency, mode, filter, AGC, mute/gain/balance, pan geometry, slice creation/removal and pan creation/removal already have neutral verbs. `createSlice()` accepts a request; `sliceChanged`, `sliceRemoved` and `sliceLifecycleFailed` report outcomes. |
| Backend selection | [RadioModel.cpp:775](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/src/models/RadioModel.cpp#L775) selects Flex, HL2, ANAN, Icom, simulator and RTL. There is no native Kiwi/Web-888 backend. Unknown families fall through to Flex, so the new family must be explicitly registered and disabled builds must fail clearly. |
| Session ownership | [RadioSession.h:14](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/src/models/RadioSession.h#L14) owns RadioModel and session service lifetimes. First scope should be Web-888 as the primary radio. Simultaneous Flex plus native Web-888 requires separate multi-session/auxiliary-backend work. |
| Existing Web-888 support | [web888-cleanroom-design.md](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/docs/web888-cleanroom-design.md) documents an implemented receiver family on the Kiwi external-antenna path, deliberately excluding a new backend and IQ. Reuse `KiwiSdrClient`, `KiwiSdrProtocol`, redirect/credential handling and their tests; do not use GUI-oriented `MainWindow_KiwiSdr` as the native backend. |
| Hermes precedent | [Hl2Receivers.h](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/src/core/backends/hl2/Hl2Receivers.h) separates hardware, DSP, analyzer and UI identities. [Hl2Backend.cpp:1712](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/src/core/backends/hl2/Hl2Backend.cpp#L1712) advertises dynamic receiver ceilings but a paired pan/receiver topology (`canCreateSlices=false`). Reuse the identity and lifecycle principles, not its contiguous-DDC allocator, Metis transport, or copied WDSP scaffolding. |
| Capabilities | [RadioCapabilities.h:331](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/src/core/backends/RadioCapabilities.h#L331) distinguishes slice creation, slice ceiling, pan ceiling and stream rates. Pan span/level records and receive-control authority records already exist. |
| Pan data | [RadioModel.cpp:7002](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/src/models/RadioModel.cpp#L7002) consumes float spectrum bins, resolves pan IDs and generates waterfall rows from them. The declared backend `waterfallRowReady` signal does not currently have a matching neutral consumer in RadioModel. The old Kiwi path separately carries row frequency bounds/timecodes. |
| Slice audio | [IRadioBackend.h:1251](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/src/core/backends/IRadioBackend.h#L1251) distinguishes pre-mute/pre-gain per-slice PCM from the speaker mix. `PcmFrame` includes producer/session/receiver-instance/format identity and revocation. Its current format validator permits 24 or 48 kHz, not arbitrary server rates. |
| Digital routing | [TciServer.cpp:221](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/src/core/TciServer.cpp#L221) subscribes to backend slice PCM. `TciTrxMap` maintains stable external receiver numbers. `DecoderAudioModel` has a native-slice route for CW/RTTY. These provide a useful initial digital receive path. |
| DAX limitation | [MainWindow_DigitalModes.cpp:1247](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/src/gui/MainWindow_DigitalModes.cpp#L1247) refuses DAX startup without a Flex PanadapterStream. VirtualAudioBridge/PipeWireAudioBridge currently expose eight RX channels. Native DAX requires a neutral input adapter, not setting `hasDaxStreams=true`. |
| Persistence limitation | [RadioStateMemory.cpp:29](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/src/core/RadioStateMemory.cpp#L29) restores one common tuning/filter/rate/AGC set. Its extension handoff currently recognizes RF-gain and TX-setpoint domains; it cannot silently accept arbitrary multi-slice topology under an invented extension key. |

## Firmware facts that govern the design

1. **Discover counts; do not assume them.** [main.cpp:249](https://github.com/RaspSDR/server/blob/a66101952c8dec3394e8cb55b3bac64decae680e/main.cpp#L249) gets RX count from the FPGA signature. With `wf_share` enabled, logical waterfall count equals RX count; otherwise it uses the physical WF count. `no_wf` can disable waterfalls. The server's compile-time arrays allow 16, which is not a promise of 16 available hardware receivers.
2. **The two-spectrum-engine claim is configuration-specific.** [zynq/peri.cpp:128](https://github.com/RaspSDR/server/blob/a66101952c8dec3394e8cb55b3bac64decae680e/zynq/peri.cpp#L128) discovers physical waterfall count. [rx_waterfall.cpp:897](https://github.com/RaspSDR/server/blob/a66101952c8dec3394e8cb55b3bac64decae680e/rx/rx_waterfall.cpp#L897) acquires a shared engine, programs it, samples and releases it. Aether consumes logical views; it must not implement another FPGA scheduler or assume every configuration is exactly 13 RX plus two WF engines.
3. **Sessions allocate channels.** [rx_server.cpp:393](https://github.com/RaspSDR/server/blob/a66101952c8dec3394e8cb55b3bac64decae680e/rx/rx_server.cpp#L393) pairs SND and W/F by session timestamp (with IP conditions for the older timestamp space). [Line 474](https://github.com/RaspSDR/server/blob/a66101952c8dec3394e8cb55b3bac64decae680e/rx/rx_server.cpp#L474) allocates a free RX slot when no partner exists. The client does not directly select “RX5”. A waterfall-only session also consumes a channel; a paired waterfall does not consume a second one. Duplicate same-type streams for a session are rejected.
4. **Lifetime is coupled too.** SND and W/F shutdown paths can stop their partner ([rx_sound.cpp:358](https://github.com/RaspSDR/server/blob/a66101952c8dec3394e8cb55b3bac64decae680e/rx/rx_sound.cpp#L358), [rx_waterfall.cpp:610](https://github.com/RaspSDR/server/blob/a66101952c8dec3394e8cb55b3bac64decae680e/rx/rx_waterfall.cpp#L610)). Closing just one socket is not a safe abstraction for deleting a UI pan while retaining its audio slice.
5. **Negotiate from MSG as well as HTTP.** The inspected `/status` response includes occupancy, version and coverage information but does not guarantee all the fields listed in Aether's older live-observation note. SND sends `audio_rate`, actual `sample_rate`, center and bandwidth ([rx_sound.cpp:164](https://github.com/RaspSDR/server/blob/a66101952c8dec3394e8cb55b3bac64decae680e/rx/rx_sound.cpp#L164)); W/F sends channel counts, 1024-point FFT size, zoom and frame-rate limits ([rx_waterfall.cpp:210](https://github.com/RaspSDR/server/blob/a66101952c8dec3394e8cb55b3bac64decae680e/rx/rx_waterfall.cpp#L210)). `/status` occupancy is advisory and races other clients.
6. **Rates have changed since earlier examples.** Current startup code permits configured 12/24/36/48 kHz nominal rates, with airband restrictions and hardware-derived actual rate. These are server-wide configuration, not a per-slice sample-rate selection API. Sharing also changes maximum WF zoom (11 versus 14). Advertise the observed session configuration.
7. **IQ exists, but it is not wideband raw ADC data.** [rx_sound.h:54](https://github.com/RaspSDR/server/blob/a66101952c8dec3394e8cb55b3bac64decae680e/rx/rx_sound.h#L54) defines a 20-byte IQ header with GPS fields. [rx_sound.cpp:1054](https://github.com/RaspSDR/server/blob/a66101952c8dec3394e8cb55b3bac64decae680e/rx/rx_sound.cpp#L1054) sends filtered, potentially AGC-processed 16-bit I/Q pairs with selected endianness. `iq` changes that receiver's SND payload; it does not add simultaneous demodulated PCM on the same connection.
8. **Use the maintained WebSocket path.** The tree contains `hpsdr/hpsdr.cpp`, but inspection found no build/startup integration for it. Its presence is insufficient evidence of a usable Hermes-compatible server. Do not select HL2 as a shortcut.

## Proposed design and files

Names below are proposals, not files already implemented.

| Proposed component | Responsibility |
|---|---|
| `src/core/backends/web888/Web888Backend.{h,cpp}` | IRadioBackend implementation, capabilities, canonical verbs, typed model deltas, restore/capture, aggregate connection state and RX-only guards. |
| `Web888ReceiverPool.{h,cpp}` | Pending reservations, stable slice identities, session leases, remote channel metadata, cancellation, capacity accounting and per-receiver generations. |
| `Web888PanadapterPool.{h,cpp}` | Pan identity/geometry, carrier-session ownership, slice membership, W/F subscriptions and shared-view reference counts. |
| `Web888Session.{h,cpp}` | Thin backend-side adapter over the reused Kiwi transport: unique session ID, optional waterfall, authenticated setup and health. Avoid a second protocol implementation. |
| `Web888Capabilities.{h,cpp}` | Pure conversion of observed metadata into validated limits/feature records; unknown and temporarily unavailable are distinct. |
| `Web888AudioMixer.{h,cpp}` | Bounded per-slice buffers and speaker clock; pre-monitor slice publication, speaker gain/mute/balance, underflow and clipping policy. Extract reusable mixing primitives only where justified. |
| `Web888StateCodec.{h,cpp}` | Versioned topology and per-slice/pan state validation, integrated with RadioStateMemory's approved extension. |
| Later `Web888IqDecoder.{h,cpp}` | Bounded IQ parsing and metadata; host demodulation is a separate shared DSP consumer, not another copied HL2 engine. |

Initially reference existing `KiwiSdrClient/Protocol` from below the backend seam. If M6 is landing concurrently, put shared transport in `src/core/backends/kiwi/` once and adapt both callers. Do not combine a wholesale old-Kiwi GUI rewrite with Web-888 bring-up.

Integration points: backend factory/registry in RadioModel; generic connector descriptor in ConnectionPanel/discovery; CMake aethercore source list and `HAVE_WEBSOCKETS` handling; RadioCapabilities and its actual consumers; SliceDelta/PanadapterModel; RadioStateMemory/RestoredRadioState; DecoderAudioModel/TciServer; later virtual-audio and DaxIqModel adapters. Preserve RadioSession teardown ordering.

## Receiver and pan allocation rules

Represent separately: `sliceId`, receiver-instance generation, protocol session ID, reported remote RX index, `panId`, neutral data-plane pan index, and TCI receiver number. No arithmetic equality between them. Server RX numbers may change on reconnect while slice identities remain stable.

- First connection opens one receiver session and one paired waterfall, publishing one slice and pan only when setup succeeds. No eager reservation of the entire radio.
- Adding an interactive slice outside existing IQ coverage opens another SND session, reusing a suitable pan's existing W/F view. Adding a digital software slice inside a capture consumes no additional receiver lease. Capture center/filter/gain are shared; local slice filtering and decoder output are independent. Do not promise per-slice hardware AGC within one shared capture.
- A pan attaches its W/F socket to one owned carrier session. Additional slices need no W/F socket. Separate pans may use separate owned carrier sessions; multiple UI views with identical geometry may share a subscription if the product permits it.
- When pans outnumber sound sessions, a W/F-only carrier needs an additional RX lease. Track a budget such as `owned sound sessions + waterfall-only carrier sessions + pending reservations <= usable RX ceiling`; also enforce W/F eligibility and local implementation limits.
- With waterfall sharing disabled, only eligible server-assigned RX channels have genuine wideband W/F. An audio FFT fallback must be labeled and limited to its audio span, not presented as a full-band pan.
- Deleting a non-carrier slice closes only its own session. Deleting a carrier slice while its pan is still used requires an explicit policy: retain the session temporarily as a display lease, or migrate the pan with an acknowledged display interruption. Retaining it still consumes a receiver slot. Do not call a muted carrier “free”.
- Deleting a pan must not destroy surviving slice audio as a socket side effect. Pause its W/F production and retain the paired transport until its lease can be released safely, or use a tested whole-session transition. Confirm the server's `wf_speed=0` behavior during first hardware validation.
- Serialize/track pending opens so simultaneous Add RX requests cannot overbook the local budget. A server busy/auth/exclusive-use response rolls back only that pending request. Do not renumber or retune survivors.
- Do not count camping/monitor audio as an independently tunable slice. Do not automatically chase redirect endpoints for a native physical-radio identity. Retain the existing redirect and credential policies.

The existing client builds session IDs from milliseconds ([KiwiSdrClient.cpp:807](https://github.com/aethersdr/AetherSDR/blob/43df97327ac8f393acefd473f2a6278313636c30/src/core/KiwiSdrClient.cpp#L807)). Concurrent opens need a collision-free process/session allocator compatible with the server's timestamp space, including simultaneous old-Kiwi and native connections.

## Phased implementation and exit criteria

### Phase 0 — agreed contract and hardware evidence

Deliver an RFC/design document and recorded protocol matrix against a named device, firmware and configuration. Resolve the backend freeze, primary-session scope, carrier-retention behavior, M5 connector integration and M6 shared transport ownership. Recheck current main before coding: the interface is moving quickly.

Capture permitted SND/W/F setup, no-password/password failures, busy handling, two distinct concurrent receiver sessions, independent W/F tuning, and paired teardown. Verify source-derived assumptions on the actual unit; do not treat historical public-receiver observations as tests performed here. Record sample rate, usable passband, channels, `wf_share`, zoom and coverage without changing administrator configuration.

Exit: agreed requirements, source provenance, resource/lifetime table and captures. No firmware fork is a prerequisite.

### Phase 1 — one native receiver

Add the backend and minimal session adapter. Reuse endpoint normalization, Web-888 `/kiwi/<id>/SND` and `/W/F`, auth-first staging, binary MSG classification, keepalives, compression handling, and the existing one-shot setup replay after `wf_setup`. Use one private asynchronous I/O worker initially, rather than one thread for every slice; preserve owner-thread seam emissions.

Implement RX frequency, supported modes, filters, AGC/squelch, audio controls and RSSI. Map modes explicitly; DIGU/DIGL are receive-audio configurations over USB/LSB where appropriate. Unsupported modes are unavailable, not silent AM fallback. Resolve the existing 0–100 model AGC scale versus Kiwi's signed dB threshold with a tested neutral control-range/mapping contract.

Set `canTransmit=false`, no TX/tune/keyer/ATU support, no Flex DAX/firmware/admin capabilities. Audit permissive capability defaults. Use receive observations only for what can be established: command acceptance is not hardware readback; later reported state wins.

For first light, normalize supported server audio to the existing 24 kHz PCM path using the existing resampler. Explicitly disclose downsampling if a wider server configuration is accepted; preserve 48 kHz bandwidth in the later native-rate path. Never label 12/36 kHz bytes as valid 24/48 kHz frames.

Exit: standalone Web-888 connection, tune/mode/filter response, speaker audio, one correct pan, meter and clean disconnect; Flex/HL2 slice-0 regression check unchanged; disabled-WebSockets builds refuse cleanly.

### Phase 1b — required digital IQ capture and software splitting

Use the dedicated opt-in filtered-IQ path and sound-only admission policy. Validate
20-byte header, I/Q pair order, payload endian, negotiated sample rate and sequence
wrap/gaps. Keep ordinary Kiwi/PCM consumers unchanged. Refuse unsupported frames
and coverage; never mislabel IQ as stereo speaker audio. Preserve generation
fences before teardown. GPS fields need separate timing qualification.

Channelize each capture into USB-referenced slices with anti-alias filtering and
12 kHz mono decoder output. Keep packet boundaries transparent. Reset on a gap,
retune, rate change or new generation and mark the next decoder output incomplete.
Expose deterministic filter latency/sample offsets before timed decoder assembly.
Use synthetic separated tones and known recordings to check direction, isolation,
amplitude, rate drift and decode timing. Bound all frame/slice/work queues and
move sustained DSP off the owner thread before enabling multi-band operation.

Exit: socket-free end-to-end IQ-to-multiple-audio tests, then real-device IQ
qualification. The current implementation provides the first engine primitive;
it does not yet assemble timed decoder periods or activate a monitoring profile.

### Phase 2 — capture allocation, software slices and honest capacity

Build a deterministic RF-window planner from the coverage table and a receiver
pool that reserves interactive capacity. A capture can feed several decoder
slices; count leases rather than output count. Provide explicit alternatives
when the selected modes exceed capacity. Background digital captures must not
retune for an interactive request. Keep topology changes transactional so a
failed open preserves existing decoding. A no-waterfall capture still costs
one receiver. Qualify at 12/24/36 kHz using the actual usable passband.

Add ReceiverPool and the canonical create/remove lifecycle. Expose ceiling separately from availability; `maxSlices` is not “free channels right now”. Publish capability changes after negotiated limits change; add an optional resource-availability record only with a real consumer. Treat server occupancy as advisory until each open succeeds.

Publish pre-monitor PCM per slice and one speaker mix, with distinct producer purposes. Use bounded jitter buffers, a paced mixer, one resampler per source where needed, discontinuity signaling and generation invalidation. A delayed source must not stall all other slices. Monitor mute/solo/gain/balance must not alter decoder input; digital paths should avoid squelch clipping unless explicitly selected.

Audit A–M labels, color reuse, slice selectors, active-slice changes, decoder targeting, restore validation and arrays for assumptions of eight slices. Eight colors may repeat; they must not cap receiver count. Keep DAX's eight-output limit independent of the slice limit.

Exit: two independently tuned slices on one pan, then the negotiated capacity on owned hardware; rejected extra opens preserve existing audio; closing a middle slice preserves all surviving IDs and TCI bindings.

### Phase 3 — multiple pans and correct waterfall geometry

Implement PanadapterPool and the carrier rules above. Translate pan center/span to server zoom/start, report actual quantized geometry, and keep slice tuning independent. Use incoming W/F headers to identify the row's actual RF span; a late old-zoom row must never be painted under new geometry. Honor server FFT size, inversion/offset, calibration and frame-rate limits.

Prefer a narrow, neutral typed pan-frame payload carrying bins, actual frequency bounds, session/generation and timing, consumed once by RadioModel into the existing `panFeed` signals. This addresses the present transitional QByteArray/implicit-geometry gap. It should land as a shared-interface change with regression tests, not a Web-888-only GUI bypass. If reusing `spectrumFrameReady` for a first-light prototype, explicitly accept its timestamp/geometry limitations and do not also emit a second waterfall feed.

Add explicit asynchronous pan-lifecycle failure reporting if the current interface still lacks it at implementation time. Coalesce spectrum frames and throttle at the source; do not accumulate unbounded Qt events. Advertise pan level calibration conservatively; W/F display bins are not automatically laboratory-calibrated dBm or raw IQ.

Exit: two pans at different bands/zooms, multiple slices on one pan, no retunes caused by pan movement, tested carrier deletion and no-WF operation, and bounded performance under the device's supported waterfall load.

### Phase 4 — digital receive and virtual audio

First assemble uninterrupted, time-aligned periods for actual internal FT8/WSPR
decoders and/or independent WSJT-X instances. Current UDP control/spot support
does not transport PCM. Map each external decoder to an explicit audio input;
do not infer sixteen audio routes from a single virtual device. Test synthetic
or recorded known messages through the complete decoder, including gap handling.
Keep decoder audio independent of speaker mute, listening tune and waterfalls.

First validate TCI audio/control through backendSliceAudioFrameReady, TciRxConverter and TciTrxMap, plus CW/RTTY through DecoderAudioModel. Test supported third-party clients; do not promise every client can address 13 TCI receivers merely because Aether's mapping can represent them.

Then add a backend-neutral RX audio-source adapter for virtual audio, tentatively `src/core/ReceiveAudioRouter.{h,cpp}`. Feed VirtualAudioBridge/PipeWireAudioBridge through bounded conversion from slice PCM. Keep Flex stream acquisition below its existing transport boundary; remove the blanket null-PanadapterStream restriction only for the new supported source. Expose up to eight assignable virtual outputs initially, with an explicit slice-to-output table, while allowing more native slices. Preserve OS-specific driver availability; do not promise identical virtual-device support on all platforms.

Validate receive-only FT8/FT4/JS8/WSPR using appropriate external software, and audit internal modem/FreeDV/recording consumers individually. CW/RTTY and TCI reuse does not establish that all digital consumers have neutral routing. Disable all transmit requests, including CAT/TCI PTT and modem auto-TX. A separate transmitter integration is outside this phase.

Exit: decoder audio stays clean and isolated with speakers muted; known digital signals decode with sensible timing; no cross-slice audio, double-feed, or TX dispatch.

### Phase 5 — optional general-purpose IQ export

Reuse the required Phase 1b IQ decoder/channelizer. Complete GPS validity and hardware timing qualification before exposing timestamps as authoritative. Add a typed general-purpose IQ stream/capability contract with center, passband, rate, sample format, timestamps and lifetime; current `IRadioBackend` has no general native IQ signal.

Choose explicitly between (a) switching a slice to IQ and demodulating locally for simultaneous audio, or (b) reserving an extra receiver for IQ. Prefer (a) once a shared host-DSP component is available; do not copy Hl2RxDsp/Anan scaffolding. Option (b) is simpler but consumes capacity and is not phase-coherent duplication by assumption.

Connect the neutral IQ route to DaxIqModel and TCI IQ only at supported rates. Do not advertise Flex's 96/192 kHz bandwidth for a 12–48 kHz Web-888 stream. Upsampling does not create RF bandwidth. IQ is filtered/AGC-affected unless a tested configuration establishes otherwise; verify AGC-off gain/scaling before claiming measurement use. GPS fields do not by themselves prove coherent multi-receiver diversity.

Exit: known offset tones demonstrate correct frequency direction, amplitude scaling, channel order and discontinuity handling; IQ and audio coexist under the declared resource policy.

### Phase 6 — persistence, resilience and release qualification

Start endpoint/credential persistence in Phase 1, single-slice state in Phase 2, then complete multi-slice topology here. Use a stable configured UUID when hardware identity is unavailable; share endpoint credentials through the established credential store, but do not silently merge independent profiles solely by hostname.

Extend RestoredRadioState/RadioStateMemory with a capability-gated receive-topology domain and schema migration (or reuse its successor if upstream has added it). Store slice tuning/filter/AGC/monitor settings, pan membership/geometry and output assignments as bounded arrays in the one OperatingState document. Extend the domain filtering deliberately; do not hide topology under RF gain. Store connector preferences separately as their own versioned feature. No passwords, remote channel numbers, socket IDs or pending operations in saved state.

Restore gradually after negotiation. If fewer channels are available, restore an available subset and report the rest without overwriting the saved desired layout. Never change server-wide sample rate, RF settings or administrator configuration to satisfy a remembered client preference.

Use per-stream health and exponential reconnect backoff with jitter, deadlines, explicit cancellation and intentional-disconnect suppression. Wrong passwords, TLS failures, busy capacity, bans/time limits, server restart and malformed frames need distinct operator-visible outcomes. No automatic repeated login on invalid credentials or automatic camping that masquerades as a slice. One receiver failure should not tear down healthy ones unless shared transport state actually failed. Revoke PCM/IQ epochs before teardown and reject stale callbacks after slot reuse.

Exit: reconnect, capacity reduction, source switching and shutdown are stable across supported OS builds, with documented tested firmware/configurations and measured resource use.

## Test and evidence strategy

- Extend existing `kiwi_sdr_protocol_test`, waterfall setup/zoom/scale tests, redirect/password/family tests for shared behavior. Preserve both Kiwi and Web-888 paths during extraction.
- Add socket-free `web888_session_test`, `web888_receiver_pool_test`, `web888_panadapter_pool_test`, `web888_capabilities_test`, `web888_audio_routing_test`, `web888_state_test`, and later `web888_iq_test`. Inject transport writes and observations separately. Test accepted request versus confirmed state, partial startup, cancellation, duplicate IDs, malformed/truncated input and rollback.
- Extend `backend_slice_lifecycle_test`, `backend_family_switch_test`, `backend_seam_affinity_test` with an actively driven backend plus `SeamThreadAffinityProbe`; `backend_capability_revision_test`; PCM lifetime/decoder adapter tests; `tci_trxmap_test` and `tci_rx_audio_test`.
- Pin resource examples: N sound sessions plus P paired pans uses N RX leases; an unpaired W/F consumes one more; pending requests count; busy refusal leaks none; deleting/recreating a slot cannot deliver its old audio into the new instance.
- Use tests/tests.cmake registration and settings-consumer declarations where needed. Default tests must not introduce a fake third-party firmware socket server. Recorded byte fixtures and injected state machines are appropriate; the firmware's native browser harness is an optional development aid, not proof of Zynq hardware behavior.
- Run targeted builds/tests for each change, cross-platform compile/CI, and sanitizer churn coverage. Compare engine-boundary, capability-record, command-plane and touchpoint-manifest findings against the merge base, not just green exit codes. Preserve before/after slice-0 RX checks.
- Hardware evidence: distinct simultaneous RF signals, every advertised rate/configuration, shared/nonshared/no-WF cases, maximum intended receiver load, competing browser clients, slow networking, reconnect and 30–60 minute digital receive runs. Record audio underruns, sequence gaps, queue maxima, CPU/memory, RF geometry and effective waterfall cadence. Fix latency/buffering thresholds from measured baselines before release claims.

## Firmware changes: required versus optional

**Not presently required by the inspected protocol:** independent receive slices, multiple logical waterfalls, server-side demodulated PCM, ordinary tuning/filter/AGC controls, narrowband IQ, or host-side digital decoding.

**Optional improvements only after proving a concrete limitation:** richer capability/occupancy reporting; command acknowledgements and authoritative setpoint readback; waterfall lifetime independent of its SND partner; standalone waterfall leases that do not reserve RX channels; explicit reservation/release tokens; simultaneous PCM and IQ from one receiver; genuinely wider unprocessed IQ. Some of these would require driver/FPGA work as well as server code, especially wider sample transport.

The minimum viable native integration should stay compatible with existing firmware. Performance at the maximum channel count, exact firmware variants and IQ semantics still require live validation.

## Recommended delivery order and risks

Land small reviewable changes in this order: approved design/freeze resolution → shared transport identity/testability → one native interactive RX → required sound-only IQ and software splitting → capture planner/pool with reserved listening capacity → timed decoder routing and known-message qualification → interactive pan allocation/typed frames → neutral virtual audio → optional general-purpose IQ export → topology restore and release hardening. Local authorization permits continuing implementation while upstream approval remains a separate prerequisite. Persistence and failure tests accompany each phase.

The largest risks are the upstream architecture freeze and moving interfaces; coupled SND/W/F teardown; overstated capacity on shared/public devices; incorrect spectrum geometry during asynchronous zoom; duplicate or stale PCM; fixed-rate/eight-channel assumptions in downstream consumers; and claiming raw/coherent IQ from a filtered stream. Each has a named phase and acceptance test above. A 13-slice headline should wait until the entire route—including controls, IDs, audio and cleanup—has been exercised on a configuration that actually exposes 13 usable receivers.
