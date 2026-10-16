# Web-888 IQ timing: source findings and qualification boundary

Initial source inspection: 2026-09-30. Live follow-up: 2026-10-16.

The live 24 kHz stream repeats most consecutive SND timestamps (672 of 687
comparisons in a 15-second follow-up; no backward steps). The current source
checkout sets receive-buffer ticks to zero. See [live validation](web888-live-validation.md)
for counts, source revision and limitations. These headers are not qualified
decoder anchors; the original source findings below remain historical evidence.

## Firmware evidence

The locally available Web888-server revision is
`a66101952c8dec3394e8cb55b3bac64decae680e`. Its `rx/rx_sound.h:61–64` places ten
timing bytes after the SND meter: solution-age byte, reserved byte, a seconds
word and a fractional-nanoseconds word. These occupy wire offsets 10–19 before
the I/Q payload. The source directory was read without changing firmware or
repository configuration.

In `rx/rx_sound.cpp:84,552`, the computed time wraps modulo 604,800 seconds.
The header therefore lacks the full week/date needed for a Unix-epoch mapping.
`init/clk.cpp:245–248` receives the GPS solution time and hardware tick count.
`rx/rx_sound.cpp:639–656` adjusts sample/filter and AGC offsets, writes the prior
computed buffer time, and initializes timing state. The first packet can contain
zeros before that state has settled. The solution-age field uses 255 when the
reference tick count is unavailable and otherwise caps its upper value at 254;
this byte alone does not prove timing accuracy or continuity across week rollover.

`rx/rx_sound.cpp:1300–1305` can zero the three timing fields for external clients
when timestamp disclosure is disabled. A zero header cannot distinguish that
policy from startup or an epoch-boundary case. No administrator setting should
be changed automatically to obtain timestamps.

## Local change

`KiwiIqBlock::timingHeader` now preserves the ten bytes verbatim, independent of
payload endianness. It supplies diagnostic evidence for future recorded/live
qualification, not a parsed timestamp, quality flag, UTC conversion or decoder
anchor. IQ with redacted or unusual timing bytes remains usable as untimed IQ.
Malformed SND framing still follows the existing rejection rules.

Offline tests use distinct nonzero bytes with both I/Q payload byte orders and
an all-zero header. They verify exact preservation without changing decoded
samples. Existing capture/worker tests cover transport delivery and lifecycle.
Reproduce with `build-web888-evidence/iq-worker-validation.cmd`; evidence remains
in `iq-worker-build.log` and `iq-worker-tests.log`.
All five selected IQ, pool, worker, backend and transport tests passed in
1.81 seconds. Whitespace checks also passed.

## Before live timed decoding

Establish byte order and the exact first-sample association for the installed
firmware, including startup, filter changes, AGC settings and local FIR delay.
Resolve full week/date and GPS-versus-UTC conversion from independently verified
information, including leap-second handling; do not infer these from these ten
bytes alone. Verify progression against negotiated fractional sample rate and
packet sample counts, reject discontinuities/stale solutions, and measure drift.
Qualify week rollover, timestamp masking and GPS loss/recovery explicitly.

Choose a stated timing-quality policy with error bounds before allowing it to
arm decoder routes. Synthetic/recorded verified anchors remain the only tested
period-buffer inputs. Packet arrival time is not a substitute for sample time.


## Explicit decoder clock gate — 2026-10-16

The backend no longer accepts an unqualified integer UTC tick to arm a route.
It requires a Web888DecoderClockAnchor identifying the exact capture, generation,
first output sample index and filter delay. The UTC tick refers to that output
sample AFTER all producer-side delay corrections; the router does not subtract
the FIR delay again. Provenance is either an independently verified fixture or
an independently qualified sample clock. Unqualified/unknown sources are refused.

The anchor supplies an exclusive last-valid sample index, a worst-case error
bound through that horizon (including drift), and a revocable clock lifetime.
The initial conservative engineering limit is 1200 output ticks (100 ms), not
an assertion of any mode's decoding tolerance. Admission requires enough valid
samples to discard the initial partial period and assemble one complete period.
Blocks extending beyond the horizon disarm the route. Clock revocation invalidates
both pending and already-taken jobs. Existing gap, source lifetime, generation and
filter-change checks remain in force. Other routes remain independent.

This is a consumer contract, not a clock estimator or proof of provenance. Its
producer remains responsible for the mapping and uncertainty evidence. The raw
router tick overload remains a low-level fixture primitive, but the backend's
production entry point requires the explicit contract. No GUI switch, firmware
change or packet-arrival fallback was added. No live timed decoding was enabled.

Next required increment: obtain a qualified per-buffer sample/UTC association
from matching FPGA/driver/server metadata, or independently measure and bound a
host-clock mapping including transport latency and clock error. Merely setting
the source enum or observing GPS PPS lock is insufficient evidence. The firmware
prototype and missing production RTL integration remain separate work.

Validation: rebuilt the Windows desktop and affected tests. Seven regressions
passed: backend, shared SSB, capture pool, decoder router, synthetic decoder
pipeline, WAV serialization and decoder process (62.19 + 1.27 seconds). Rejection
cases cover unqualified source, wrong stream/generation/index/delay, missing or
revoked clock, invalid/excessive uncertainty, insufficient horizon and negative
UTC. Tests cover partial-period horizon, expiry and already-taken job revocation.
Strict boundary, capability, command-plane and registration checks passed with
existing baselines. No receiver connection or settings change was made for this
increment. This validates the gate, not a real clock source.
