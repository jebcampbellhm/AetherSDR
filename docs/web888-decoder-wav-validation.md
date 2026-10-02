# Web-888 decoder WAV handoff: local validation

2026-09-30, local `web888-native` development; no receiver or decoder process.

`web888DecoderWav` serializes a complete current routed job to an in-memory
RIFF/WAVE container: uncompressed PCM16, one channel, 12,000 samples/second.
FT4, FT8 and WSPR jobs must contain exactly 90,000, 180,000 and 1,440,000 samples
respectively, with nonnegative epoch-aligned period starts. Unsupported modes,
incomplete periods, nonfinite samples and revoked source/route witnesses fail
without returning data. Lifetime is checked before and after conversion.

Samples use fixed gain, rounded to signed 16-bit integers and saturated to
[-32768, 32767]. There is no normalization, volume/mute adjustment, resampling,
file write, sound-device access or process invocation. The maximum returned
buffer is 2,880,044 bytes (one WSPR-sized PCM16 period plus its 44-byte header).
That allocation is additional to the source float job and routing budget.

The caller must retain the job and recheck `current()` immediately before any
handoff. Serialized bytes cannot independently revoke themselves. A future file
adapter needs bounded retention, atomic file creation and cleanup; a future
decoder runner needs bounded execution and cancellation. The WAV contains audio
only: mode, RF dial, UTC origin and lifetime remain in the associated job.

`web888_decoder_wav_test` checks all three period sizes, RIFF/chunk lengths,
format, channels, sample rate, byte rate, block alignment, bit depth, known
quantization/clipping values and rejection cases. Tests operate in memory.
Reproduction: `build-web888-evidence/decoder-wav-validation.cmd`; ignored logs:
`decoder-wav-build.log` and `decoder-wav-tests.log`.
All six selected WAV, routing, period, IQ, pool and worker regressions passed in
1.69 seconds. Test registration, touchpoint manifest and whitespace checks passed.

This makes complete audio available in a conventional file format but does not
prove acceptance or message decoding by WSJT-X or another decoder. Production
timing, executable integration and real receiver qualification remain pending.

## Routed handoff review — 2026-09-30

The WAV regression now also uses actual router-produced FT4, FT8 and WSPR jobs.
Each case starts 173 samples into a period, feeds irregular boundary-crossing
blocks, and checks that the initial partial period is discarded. The resulting
job retains the correct UTC period start and source sample index; every PCM16
sample in its serialized output matches the known synthetic input.

Capture-specific disarming then revokes the already-taken job while its source
remains alive. Further WAV serialization is refused. The test also makes the
handoff limitation explicit: previously returned bytes remain allocated, so a
consumer must retain and recheck the associated job before external use.
This review required no production-code change. All six selected WAV, router,
period, IQ, pool and worker regressions passed in 2.34 seconds; whitespace checks
passed. The same local reproduction script and logs above contain this latest
run. No external decoder or receiver was used.
