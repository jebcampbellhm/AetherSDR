# Web-888 decoder period assembly: local validation

2026-09-30, local work on `web888-native`. No receiver or actual message decoder
was used. The desktop digital monitoring path remains disabled.

## Implemented primitive

`Web888DecoderPeriodBuffer` assembles one route's 12 kHz mono audio into complete
periods. The caller must explicitly arm it with a verified mapping from the first
audio sample to Unix-epoch ticks at 12,000 ticks per second, corrected for FIR and
recording delay. It does not derive UTC from packet arrival, relative sample
indices or unqualified GPS bytes. There is no production clock provider yet.

Period sizes are bounded to 12,000–1,440,000 samples (1–120 seconds). Input blocks
are bounded to 6,144 samples. An initial partial period is discarded; complete
periods align to epoch multiples of the selected size. No missing samples are
padded. Each call can return at most one complete period; there is no output
queue, thread, socket, device or decoder invocation.

The buffer reserves at most one period's sample storage (5.76 MB of float samples
at 120 seconds). Completed audio transfers to the caller, which must impose its
own retention and total route-memory limits; returned periods are not included
in this primitive's bound. The [routing primitive](web888-decoder-router-validation.md)
now budgets assembling and waiting periods across routes. Worker queues, taken
jobs and actual decoder workspaces still need a combined application budget.

Missing/revoked lifetime, changed capture/generation/slice/dial/filter delay,
noncontiguous indices, an unexpected discontinuity, nonfinite samples, empty or
oversized blocks and index overflow invalidate the anchor and discard buffered
audio. Explicit rearming is required. Returned periods preserve the source's
revocation witness, which consumers must check before use. Each complete period
is marked as an independent audio job rather than a continuous device stream.

## Offline evidence

`web888_decoder_period_test` exercises 7.5-, 15- and 120-second periods using
irregular blocks of deterministic synthetic samples. It checks two consecutive
complete periods per duration, exact alignment and sample contents, discarded
startup audio, storage bounds, rejection without an anchor, ten gap/source/data
failure cases, retained-result revocation and integer overflow refusal.

Reproduce with `build-web888-evidence/decoder-period-validation.cmd`. Build and
test logs are `decoder-period-build.log` and `decoder-period-tests.log`; local
scripts and logs remain ignored. The selected four tests cover period assembly,
IQ conversion/capture lifecycle, shared worker and capture-pool rollback.
All four passed after the buffer-allocation review (1.29 seconds total). Test
registration, touchpoint manifest, whitespace, engine boundary (102 existing
findings, zero blockers) and the unchanged seven-test PR gate checks passed.

## Remaining work

This is period assembly, not an FT8/FT4/WSPR decoder or WSJT-X audio adapter.
Production timing, clock-quality policy, application memory/CPU budgets and
bounded decoder execution remain. The real receiver's timing and
passband need hardware validation. WSJT-X UDP messages do not transport PCM.
