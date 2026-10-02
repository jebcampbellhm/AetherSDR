# Web-888 bounded IQ worker: local validation

2026-09-29, local `web888-native` work. Hardware still in transit. No receiver,
firmware substitute, remote publication or administrator change was used.

## Implemented path and bounds

`Web888IqCapture` now submits IQ to `Web888IqProcessor` instead of running the
splitter on the connection thread. A pool shares one serial processing thread
across its captures; standalone captures own a private processor. Splitter state
belongs exclusively to the worker after session creation. Authentication,
allocation, callbacks, audio publication and revocation remain on the owner thread.

The processor admits up to 64 sessions, with at most 32 waiting input blocks and
32 waiting result batches globally, and two of each per session. One additional
input block can be in processing. An owner-thread drain can hold one batch of up
to 32 results while the worker fills the next bounded queue. Each input has at
most 4096 complex samples; each result has at most 16 software slice outputs.
At the lowest accepted 8 kHz input rate, that is at most approximately 6144 mono
samples per output. These bounds apply before any downstream decoder adapter.

Only one drain notification is outstanding. Nested event loops inside consumer
callbacks cannot recursively drain more batches. Input overflow rejects the new
block and marks the next admitted block discontinuous; result overflow discards
the new result and marks the next published audio discontinuous. Existing queued
results retain their order. Counters expose drops, queue high-water marks,
in-flight work and measured time inside the processing calls.

A full audio queue cannot hide a terminal validation/coverage error: one bounded
error slot per session bypasses it. That session then refuses further work.
Close removes its queued inputs/results and invalidates in-flight delivery.
Worker destruction joins after at most the currently executing bounded block;
it never waits for an owner callback. Capture output retains its existing atomic
revocation witness. Destroying a shared processing service explicitly fails any
still-running capture, rather than leaving it apparently active.

## Tests and build

The complete Windows Qt 6.8.3 / MSVC 19.44 desktop executable builds. Fifteen
selected offline regressions pass, including the new `web888_iq_processor_test`,
asynchronous capture-pool tests and the existing interactive backend/connector,
Kiwi transport/protocol/waterfall, seam-affinity and family-switch tests.

New checks cover input overload, a deliberately stalled owner event loop,
bounded output retention, post-drop discontinuity, owner-thread callback affinity,
close/reopen without stale delivery, terminal errors with a full result queue,
global/session limits, shared-service destruction and nested-event-loop delivery.
The pool tests continue to cover rollback, timeout, reuse, survivor audio and
reentrant stop while waiting for genuine worker completion.

Reproduce using `build-web888-evidence/worker-full-validation.cmd`. Build and test
logs: `worker-full-build.log`, `worker-full-tests.log`; isolated worker-test runs
use `iq-worker-validation.cmd`. Scripts and logs remain ignored local evidence.

Static checks pass: engine boundary (102 existing findings, zero blockers),
frozen capability and command-plane baselines, test registration, touchpoint
manifest, the unchanged 52-signal backend seam, unchanged seven-test PR gate,
and whitespace checks.

## Synthetic timing sample

The opt-in `web888_iq_processor_test --benchmark` uses the table's 36 kHz profile:
12 captures and 25 outputs (FT8, WSPR, FT4 and alternate 80m WSPR). It processes
five seconds of synthetic complex input per capture in 180 batches. Each round
waits for all results before submitting the next, so this measures processing and
dispatch cost under controlled submission, not free-running overload tolerance.

The initial local sample took **5.630 seconds wall time**, including the polling
test harness, with **1.607 seconds summed inside DSP calls**, peak queues of
12 inputs and 12 results, and no dropped blocks. DSP duration is measured elapsed
time, not a CPU-utilization measurement. The wall time exceeded the represented
five seconds; do not claim a demonstrated real-time receiver from this result.
The latest reproduction is recorded in `build-web888-evidence/iq-worker-benchmark.log`;
individual run timings vary. No actual FT8/WSPR message decoding was measured.

## Paced synthetic qualification

The opt-in `web888_iq_processor_test --paced` submits the same 12-capture,
25-slice profile for 60 seconds at 36,000 complex samples per second per capture.
A precise timer schedules 1,000-sample packets against elapsed time independently
of worker completion. Late producer callbacks submit catch-up bursts. The owner
event loop remains active, and the test checks every output's sample-index
continuity as well as drops and queue limits. This is an optional local load test,
not an added minute on the default CTest run.

The first run completed in **60.008 seconds**, with **18.226 seconds summed inside
DSP calls** and maximum producer lateness **1.795 ms**. All **25,920 input blocks**
were admitted and completed; input and result drops were both **zero**. Peak queues
were **12 inputs and 3 results**. All 25 slices had only their expected initial
discontinuity, with no sample-index continuity errors. The existing processor
regression also passed, including its deliberately induced overload/drop cases.

Reproduce using `build-web888-evidence/iq-paced-validation.cmd`; results are in
`iq-paced-build.log`, `iq-paced-regression.log` and `iq-paced-load.log`. This
demonstrates one minute of paced synthetic processing on this development machine.
It does not include receiver networking, interactive rendering/audio, decoder CPU
load or actual message decoding, and is not a prolonged soak or hardware test.

## Next qualification

Add longer paced runs with consumers and actual decoder adapters. Bounded
[period assembly](web888-decoder-period-validation.md) and
[routing](web888-decoder-router-validation.md) now exist and are tested together
with this worker using synthetic clock anchors. Production timing quality and
bounded decoder execution remain. Admission timeout and a
ten-second post-admission progress watchdog now exist. The first successful
worker result starts it, allowing FIR warmup; subsequent renewal requires actual
slice audio. Socket traffic or repeated empty results cannot keep a capture alive.
Expiry stops the capture and revokes retained
audio. The timer is inactive before admission and after stop; a new capture
starts with a fresh deadline. Offline lifecycle checks exercise expiry, single
failure delivery, revocation and restart without waiting ten seconds.
The desktop monitoring UI is not
enabled. Real bandwidth, sample timing, gain behavior, competing clients,
waterfall/listening load and prolonged receive performance still require hardware.

## Audio-starvation review — 2026-09-30

A regression reproduced a watchdog defect: repeated short blocks with sequence
gaps reset FIR history, produce no audio and previously renewed the deadline on
every successful but empty worker result. The capture could remain admitted
indefinitely. Renewal now requires a nonempty slice output after initial admission.

The socket-free test shortens only its own timer to 100 ms and continuously sends
100-sample discontinuous packets; it must expire with zero audio published. It
failed before the fix after five seconds (`iq-starvation-before.log`). Production
timeout remains ten seconds. Existing tests cover normal admission, stop/restart,
retained-output revocation and independent pool failures. All five selected IQ,
pool, worker, backend and transport regressions passed after the fix in 2.18 seconds
(`iq-worker-tests.log`).

## Full desktop checkpoint after timing/WSPR/watchdog work

On 2026-09-30 the complete Windows desktop rebuilt and all 21 selected offline
regressions passed in 53.55 seconds. This includes the IQ-to-period integration,
encoded WSPR fixture and encoder reference, WAV serializer, capture watchdog,
interactive connection/UI, waterfall setup/scale/zoom, family switching and
thread-affinity checks. Reproduce with
`build-web888-evidence/web888-checkpoint-validation.cmd`; evidence is in
`web888-checkpoint-build.log` and `web888-checkpoint-tests.log`.

Engine boundary remained at 102 pre-existing findings with zero blockers;
capabilities remained at 71 booleans, the command-plane baseline at 413 commands,
and the backend seam at 52 signals. Registration, touchpoint manifest, unchanged
seven-test PR gate and whitespace checks passed. Compiler warnings in existing
FFTW and CenterLockRebindTracker code remain; this was not a warning-free build.
Hardware, production clock quality and actual message decoding remain unverified.

## Standalone startup deadline — 2026-09-30

Review found that only pool-owned captures had a startup deadline. A standalone
`Web888IqCapture` could remain running without ever receiving valid IQ. Each
capture now starts its own 30-second deadline before connecting and cancels it
on the first coverage-checked worker result or on stop. Expiry releases the
processing session and transport through the normal failure path. The pool's
whole-profile admission deadline remains in place, and post-admission audio
progress still uses the separate ten-second watchdog.

The new socket-free regression failed before the fix (`iq-startup-before.log`).
It checks timer activation, one failure on expiry, stopped-timer state, restart,
cancellation after valid IQ and harmless expiry signals after admission. Tests
invoke the real timer signal directly to avoid waiting thirty seconds.
All five selected transport, backend, IQ, pool and worker tests passed after the
fix in 2.19 seconds (`iq-worker-tests.log`); whitespace checks passed.

## Expanded desktop checkpoint — 2026-09-30 08:25 UTC

The Windows desktop rebuilt and all 24 selected offline regressions passed in
63.54 seconds. This checkpoint includes the standalone startup deadline,
post-admission progress watchdog, capture-specific decoder-route cancellation,
full-profile stalled-consumer bounds, IQ-to-period integration, WAV serialization,
and encoded WSPR symbol preservation at 12 kHz, 24 kHz, 36000.125 Hz and 48 kHz.
Interactive connection/UI, waterfall, backend-family and thread-affinity checks
also passed. The WSPR fixture still checks known symbols, not general message
decoding.

The subsequent accelerated benchmark represented five seconds across 12 captures
and 25 slices in 5.640 seconds wall time, with 1.651 seconds measured DSP time,
peak input/result queues of 12/12 and zero drops. This completion-controlled run
is not a paced-load qualification; the earlier paced results remain separate.

Reproduce locally with `build-web888-evidence/web888-expanded-validation.cmd`.
Evidence is in `web888-expanded-build.log`, `web888-expanded-tests.log` and
`iq-worker-benchmark.log` in that ignored local evidence directory.
Engine boundary, capability, command-plane, test registration, touchpoint manifest,
backend seam, unchanged PR gate and whitespace checks passed. Architecture
baselines remain unchanged. No receiver was contacted, and production timing,
actual decoder execution and hardware acceptance remain outstanding.

## Merged-tree Qt 6.12 checkpoint — 2026-10-02

After integration of upstream `81806bb3`, the desktop rebuilt in the separate
`build-web888-qt612` directory with the pinned Qt 6.12.0 kit and MSVC 19.44.
All 24 selected offline regressions passed in 64.76 seconds. The subsequent
five-second accelerated benchmark took 5.614 seconds wall time and 1.744 seconds
measured DSP time with peak queues 12/12 and zero drops. Reproduction/evidence:
`build-web888-evidence/qt612-validation.cmd`, `qt612-configure.log`,
`qt612-expanded-build.log`, `qt612-expanded-tests.log` and `iq-worker-benchmark.log`.

The new decoder process adapter then built with the desktop and passed four
focused tests in 2.61 seconds plus three opt-in real-tool message cases; see
[decoder process evidence](web888-decoder-process-validation.md). Upstream
`backend_receive_contract_test` and `receive_intent_routing_test` passed in
1.18 seconds (`receive-contract-tests.log`). Their family registry does not yet
include Web-888; Web-888's own earlier connection/backend checks remain separate.

Static checks retain 102 existing engine-boundary findings and zero blockers,
71 capability booleans, 406 commands matching upstream's reduced baseline,
51 generated seam signals and the frozen seven-test PR gate. Registration and
touchpoint manifest checks passed. The launcher points to the Qt 6.12 build.
The older Qt kit/build and the pre-merge recovery stash were retained.
