# Web-888 capture pool: local validation

2026-09-29. Local `web888-native` work, hardware still in transit. No live radio,
public receiver, fake firmware socket server, firmware update or upstream action.

## Behavior

`Web888CapturePool` turns the pure RF-window plan into independently admitted
`Web888IqCapture` instances. Its receiver budget includes this pool's existing
captures plus reserved interactive listeners; it is not the count of currently
free remote receivers. Other users can still consume capacity, so a server
refusal is handled as an admission failure.

Unchanged windows retain their source and software slice identities. All new
windows are counted as pending before the first open. Pending audio is withheld
until every new capture has produced a valid IQ block whose actual rate can
cover its requested slices. Socket-open or requested-rate status alone is not
admission. The first published output from each new slice is marked discontinuous.

Profile changes acquire new windows before retiring old ones. If this temporary
overlap exceeds the budget after listening reservations, the request is rejected
without changing existing sources. Busy/refused startup, insufficient actual
bandwidth or a 30-second admission deadline cancels only the pending transaction.
A removal-only request can reduce the profile under a reduced receiver budget.

On commit, removed captures revoke their audio before `profileReady`. Each output
has its original slice ID, a pool-scoped capture ID that is never reused, and the
capture's revocable lifetime. Consumers must check `current()` for retained or
queued audio. A committed capture failure removes only that capture. If a pending
profile relies on it, that transaction also fails while unrelated survivors run.
Endpoint changes require an explicit stop; credentials are passed into individual
connection attempts and are not saved in the pool.

## Evidence

The complete desktop executable rebuilt successfully on the existing Windows
Qt 6.8.3 / MSVC 19.44 configuration. Fourteen selected regression tests passed,
including the new `web888_capture_pool_test`. Logs are
`build-web888-evidence/pool-full-build.log` and `pool-full-tests.log`; reproduction
script: `build-web888-evidence/pool-full-validation.cmd`.

The allocator tests inject observations into actual capture/splitter objects;
only HTTP preflight is suppressed. They cover listener headroom, pending counts,
valid-IQ admission before FIR output, exact reuse, endpoint refusal, overlapping
profile refusal, partial admission rollback, survivor audio, stale source events,
temporary replacement capacity, lower-budget removal, timeout, committed-source
failure, reentrant stop at commit, actual-rate coverage failure, empty-profile
release and explicit shutdown. A focused rerun also checks synchronous oversized
password refusal rolls back once before transport opening. Focused evidence is in
`capture-pool-build.log` and `capture-pool-tests.log`.

Static checks pass: engine boundary (102 existing findings, zero blockers),
capability/command-plane baselines, test registration, touchpoint manifest,
52-signal seam probe table, unchanged seven-test PR gate, and whitespace checks.

## Remaining limits

This is an engine primitive; the desktop still starts one interactive receiver.
It does not yet share an enforced live budget with interactive UI additions,
offer profile controls, or route complete timed periods into FT8/WSPR decoders.
It reserves local headroom, not firmware-enforced reservations. Existing windows
are reused only when center and complete preset lists match exactly; adding a
software slice to a running window without resetting other slices is future work.

DSP now uses one bounded worker per pool, with owner-thread admission and audio
delivery; see [worker validation](web888-iq-worker-validation.md). The pool tests
now exercise asynchronous completion. Admission timeout and a ten-second
post-admission worker-progress watchdog are implemented; expiry uses the existing
capture-failure path. A one-minute paced synthetic run passed for 12 captures and
25 outputs; longer qualification with downstream consumers remains.
Hardware bandwidth, gain, timing, decoder performance and simultaneous
interactive reception still require the receiver.
