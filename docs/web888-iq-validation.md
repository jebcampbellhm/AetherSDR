# Web-888 IQ and software splitting: local evidence

2026-09-29; branch `web888-native`, base `20d022d5`, local uncommitted work.
Hardware remains in transit. No receiver, fake firmware socket server, or
administrator configuration was used for these tests.

## Implemented

- `KiwiSdrClient::SoundFormat::FilteredIq` is immutable and opt-in. Existing
  callers remain on their original demodulated PCM path. IQ requests `mod=iq`,
  uncompressed samples, a rate-dependent passband, and uses the reported
  fractional `sample_rate`; data arriving without a valid rate is rejected.
- `KiwiIqBlock` parses bounded SND frames with the 20-byte header, LE sequence,
  flag-selected sample endian, and signed interleaved I/Q normalized to float.
  Compression, truncation, pair misalignment and oversized payloads fail closed.
- `Web888IqSplitter` provides up to 16 software USB slices per capture. Each uses
  an independent oscillator, 513-tap Blackman-windowed FIR, fractional resampling
  and USB audio reconstruction at 12 kHz mono. Input frames are capped at 4096
  complex samples. Requested passbands need 1 kHz clearance from capture edges.
- Filter state and oscillator/resampler phase persist across packets; malformed
  input, sequence gaps, restarts, source generation, center or rate changes reset
  continuity. Startup convolution is discarded. Outputs carry dial reference,
  first sample index, FIR group delay, sequence and discontinuity information.
  These are relative timing values, not a claim of UTC-aligned decoder periods.
- `Web888IqCapture` owns a sound-only independently admitted session. Each output
  carries a revocable lifetime witness; consumers must check `current()` before
  using queued/retained audio. Stop revokes first and fences reentrant delivery
  before destroying the transport. No IQ samples enter the speaker PCM decoder.
- `Web888CapturePlan` calculates RF windows from explicit presets, checks rate
  and capacity, and reserves listening receivers. An over-capacity proposal is
  returned for review with `fitsCapacity=false`; it opens nothing. Tested counts
  agree with the [coverage table](web888-frequency-coverage.md), including FT4 and
  the alternate 80m WSPR frequency.

## Validation

Windows, Qt 6.8.3, MSVC 19.44, Ninja RelWithDebInfo; the existing
`build-web888` configuration. Complete `AetherSDR.exe` build succeeded.

Thirteen selected offline tests passed: IQ, native backend, connector/model,
connection form, Kiwi session and identifier, shared protocol, three waterfall
tests, backend family switching, seam affinity and connection-window size.
Evidence: `build-web888-evidence/iq-full-build.log` and `iq-full-tests.log`.
The local reproduction script is `build-web888-evidence/iq-full-validation.cmd`.
Evidence scripts/logs are ignored workspace artifacts, not release assets.

The IQ test additionally checks synthetic tones on both sides of a 36/48 kHz
capture (including 36000.125 Hz), desired output amplitude/frequency, unwanted
cross-channel tone levels, sample counts, fractional-rate continuity, metadata,
endian/order, sequence wrap, invalid frames, insufficient bandwidth, nonfinite
samples, stable planner counts, invalid presets and reentrant stop/restart.
Valid narrow 12/24 kHz captures are compared sample-for-sample across different
packet boundaries. Focused reruns are in `iq-build.log` and `iq-tests.log`.

Static checks pass: engine boundary (102 pre-existing findings, zero new
blockers), frozen capability booleans (71), command-plane baseline (413), test
registration, touchpoint manifest, 52-signal seam probe table, unchanged frozen
seven-test PR gate, and whitespace checking. No new shared backend signal or
public native-IQ capability was advertised.

## Limits and next increments

The desktop connector still opens the interactive PCM receiver. The IQ capture
and pure planner are engine components, not yet a runnable multi-band monitoring
UI. Next: transactional capture-pool admission/rollback and stable software slice
identities; bounded worker delivery/load measurement; timed decoder period
assembly; actual internal FT8/WSPR decoding and external audio mappings.

Follow-up: transactional admission/rollback is now implemented and covered in
[capture pool validation](web888-capture-pool-validation.md). Worker delivery,
timed decoder assembly and audio mappings remain pending.

WSJT-X UDP support is not PCM transport. General virtual-device routing and
multiple decoder instances need explicit adapters. No claim of decoded FT8/WSPR
messages is made from the synthetic tone tests.

The splitter algorithm is synchronous, but captures now run it through a shared
bounded worker; see [worker validation](web888-iq-worker-validation.md). This is
not a live multi-band CPU qualification. Downstream adapters must also bound
their queues and honor revocation. FIR delay is reported; network
delay, GPS epoch/validity and accurate UTC sample alignment remain unqualified.

Firmware provenance: RaspSDR/server `a66101952c8dec3394e8cb55b3bac64decae680e`,
`rx/rx_sound.h` and `rx/rx_sound.cpp`; `extensions/ext.cpp` confirms the reported
rate is corrected ADC clock divided by receiver decimation. Firmware IQ is
post-filter/post-AGC. Verify bandwidth, spectral direction, gain, clipping,
timing, resource limits and real decoder performance on the actual receiver.
WebSockets-disabled full-build limitations recorded in the native validation
remain; this increment does not claim a newly qualified disabled build.
