# Web-888 decoder routing: local validation

2026-09-30, local `web888-native` work. Synthetic audio only; no receiver,
decoder executable, audio device or remote service is used.

## Scope and limits

`Web888DecoderRouter` is an owner-thread engine primitive with at most 64 routes.
It maps the plan's FT8, FT4 and WSPR presets to 180,000, 90,000 and 1,440,000
samples respectively at 12 kHz. Every route requires an explicit verified
sample-to-UTC anchor through the existing period buffer. Unknown modes and
duplicate slice IDs are rejected. It does not infer clocks or start decoders.

Configuration reserves two periods' float storage per route: one assembling and
one waiting for consumption. The caller supplies a byte budget, capped at 256 MiB.
The 25-slice profile (eight FT8, eight FT4, nine WSPR) reserves 120,960,000 sample
bytes, approximately 115.4 MiB. A 64 MiB budget rejects it; 128 MiB admits it.
This excludes object metadata, upstream IQ queues, consumer-held jobs, decoder
workspace and device buffers. These need additional application-level budgets.

A route holds only its latest complete waiting job. Replacement drops the older
job and increments a counter. Before a block can complete a replacement, the old
waiting job is released so crossing a period boundary cannot transiently retain
three sample buffers. `takeNext()` rotates among routes to avoid favoring a
frequently producing slice. There is no hidden dispatch queue or worker.

Rejected configuration preserves existing routes and waiting jobs. Accepted
configuration replaces all routes and requires new anchors. Reanchoring, gaps,
invalid audio, configuration replacement, clear and destruction revoke route
jobs; stopped captures also revoke them through their source witness. Consumers
must check `Web888DecoderJob::current()` before using a taken job. Taking a job
transfers retention responsibility to the consumer; repeated unbounded takes
would defeat the application memory budget and must not be used by an adapter.

## Offline tests

`web888_decoder_router_test` checks full-profile budgeting, exact budget admission,
rejected edits preserving queued work, duplicate/unknown-mode refusal, latest-job
replacement, fair selection, explicit anchor requirements, stopped-source
revocation, reanchoring, gap invalidation and successful profile replacement.
Fixtures feed deterministic 12 kHz audio directly, without sockets or IQ timing
claims. Related period, IQ, worker and capture-pool regressions run alongside it.

Reproduce with `build-web888-evidence/decoder-router-validation.cmd`. Local
build/test evidence is in `decoder-router-build.log` and `decoder-router-tests.log`.
All five selected tests passed after adding boundary-crossing replacement and
destruction checks (1.38 seconds total). Test registration, touchpoint manifest,
whitespace, engine-boundary checks (102 existing findings, zero blockers) and
the unchanged seven-test PR gate passed.

## Integration evidence and remaining work

### Shared-IQ integration fixture

`web888_decoder_pipeline_test` connects the real shared worker/channelizer to the
period router using the 30m FT8/FT4/WSPR profile in one 36 kHz capture window. It
generates 121 represented seconds of complex input containing three known RF
tones, submits it with completion-controlled pacing, and checks 8 FT8-sized,
16 FT4-sized and one WSPR-sized complete periods. Each output is checked for its
mode, size, synthetic epoch alignment and recovered audio-tone amplitude.

The fixture supplies an explicit synthetic epoch for the first post-filter
sample, plus a synthetic source lifetime; it does not obtain timing from packet
arrival. A skipped input sequence must invalidate all three routes and previously
taken jobs. Source shutdown refuses further worker input. The consumer retains
at most one job per mode during this check.

Reproduce with `build-web888-evidence/decoder-pipeline-validation.cmd`; logs are
`decoder-pipeline-build.log` and `decoder-pipeline-tests.log`. This verifies the
combined IQ-to-period path, not actual FT8/FT4/WSPR message decoding or real-time
performance. The fixture deliberately waits for each worker result.
All six selected tests passed: 40.13 seconds total, including 38.11 seconds for
the integration fixture. It completed 25 correctly routed tone periods with no
input, result or waiting-job drops, and the gap invalidation checks passed.
Whitespace, test registration and the unchanged seven-test PR gate checks passed.

### Desktop regression checkpoint — 2026-09-30

The Windows Qt 6.8.3/MSVC desktop executable rebuilt successfully with all current
IQ, watchdog, period and routing components. All 18 selected offline regressions
passed in 51.48 seconds, including interactive Web-888 connection/UI, Kiwi
transport/waterfall, backend family switching and thread-affinity checks, plus
the combined digital pipeline fixture. Reproduction: `decoder-full-validation.cmd`;
evidence: `decoder-full-build.log` and `decoder-full-tests.log` in the ignored
`build-web888-evidence` directory.

The accompanying 12-capture/25-output accelerated benchmark reported 5.625 seconds
wall time and 1.630 seconds summed DSP time, with no drops. As before, its producer
waits for completions; this is not a free-running receive qualification.
Engine boundary (102 pre-existing findings, zero blockers), capability and
command-plane baselines, the unchanged 52-signal seam, touchpoint manifest and
whitespace checks passed. No hardware or actual message decoder was exercised.

### Full-profile paced fixture

The opt-in `web888_iq_processor_test --paced-routes` exercises all 12 capture
windows and 25 routes for 121 represented seconds at 36 kHz. Producer scheduling
uses elapsed time independently of worker completion, including catch-up bursts
after late callbacks. It extends the earlier 60-second worker-only paced test
with the real period router and immediate bounded job collection.

The fixture supplies explicit synthetic clock anchors and source lifetime, then
checks 64 FT8-sized, 128 FT4-sized and nine WSPR-sized jobs across all 25 routes.
It also checks period alignment, source validity, sample continuity, queue bounds
and input/result/job drops. Input is synthetic constant complex data, not encoded
messages. Actual decoder computation, device I/O and GUI rendering are excluded;
this is not a hardware test or a prolonged soak.

Reproduction: `build-web888-evidence/iq-routed-paced-validation.cmd`; evidence:
`iq-routed-paced-build.log`, `iq-routed-paced-regression.log` and
`iq-routed-paced-load.log`. The optional run does not lengthen default CTest runs.

The first run passed in **121.011 seconds wall time**, with **37.012 seconds
summed inside DSP calls** and **1.857 ms** maximum producer-callback lateness.
All **52,272 input blocks** completed with zero input/result drops, only the 25
expected initial discontinuities and no sample-index continuity errors. All
**201 complete periods** were collected: 64 FT8, 128 FT4 and nine WSPR, with zero
job drops. Peak waiting counts were 12 IQ inputs, two IQ results and four routed
jobs. Reserved route sample storage was 120,960,000 bytes; this is a configured
bound, not measured process memory. The default worker regression and whitespace/
test-registration checks also passed. Timings vary by machine and system load.

### Invalid-profile preservation review — 2026-09-30

Review found that router configuration checked mode and dial but accepted slice
IDs/filter bounds rejected by the upstream splitter. Such a proposed profile
could replace valid routes and discard their waiting jobs before acquisition
refused the slice. The router now applies the same slice-domain checks before
committing: nonnegative ID, finite positive dial, finite filter limits, low edge
at least zero, high edge at most 5 kHz and width at least 100 Hz.

New regression cases cover a negative ID, NaN/infinite filter values, negative
low edge, excessive high edge and insufficient width. Each rejected edit must
leave two existing queued jobs and the configured memory reservation intact.
The regression failed before the fix at invalid-profile acceptance; the local
failure log is `decoder-router-invalid-profile-before.log`.
After the fix all five selected routing, period, IQ, pool and worker tests passed
in 1.34 seconds (`decoder-router-tests.log`). Whitespace checks also passed.

### Full-profile stalled consumer

The router regression now supplies 240 represented seconds of synthetic 12 kHz
mono audio to each of the 25 configured routes without collecting any job. This
is accelerated direct-router input, not a four-minute paced receiver run.
Of 402 completed periods, 377 older waiting jobs are replaced and exactly 25
remain, one per route. Collection checks every route once, the latest expected
period start/sample index, valid lifetimes and a total of 15,120,000 queued float
samples (60.48 MB). The configured assembling-plus-waiting reservation remains
120.96 MB; no process-memory measurement is implied. All 25 anchors can then be
disarmed together without changing that reservation.

All five selected routing, period, IQ, pool and worker regressions passed in
2.25 seconds; the expanded router test took 0.45 seconds. Evidence remains in
`decoder-router-tests.log`. Whitespace checks passed.

### Capture-specific disarm

`disarmCapture(captureId)` now provides explicit clock/source invalidation for
all routes sharing one capture, without replacing the profile. It clears their
partial and waiting periods and revokes taken jobs even if the source itself
remains alive. Unrelated captures continue; memory reservations remain in place
and affected routes require a fresh explicit anchor. Unknown/already-disarmed
capture IDs have no effect. This is an integration hook, not a clock monitor.

Regression coverage checks retained-job revocation, waiting-job removal, a
surviving capture's continued periods, rearming, repeat cancellation and multiple
software slices sharing a capture.
All five selected routing, period, IQ, pool and worker regressions passed with
the final assertions in 1.52 seconds (`decoder-router-tests.log`); whitespace
checks passed.

### Remaining adapters

The router is now exercised with a bounded external decoder adapter and actual
FT8/FT4/WSPR known-message files; see
[process validation](web888-decoder-process-validation.md). Desktop monitoring,
verified clock-quality handling, parsed results and multi-band dispatch remain.
The present WSJT-X UDP client does not transport PCM; file decoding does not
establish virtual audio-device routing. Receiver timing, passband and simultaneous
interactive load still require hardware.
