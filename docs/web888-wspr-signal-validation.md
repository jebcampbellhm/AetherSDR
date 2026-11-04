# Web-888 encoded WSPR signal fixture

2026-09-30. Local, entirely synthetic signal processing; no receiver, transmitter,
audio device or external decoder process.

`web888_wspr_signal_test` uses the repository's pure `WsprBeacon::encode` function
for the reference message `K1ABC FN42 37`. The existing `wspr_beacon_test` checks
that encoder against its documented 162-symbol reference vector and runs in the
same validation command. The fixture does not construct a beacon or TX model.

The test constructs ideal continuous-phase four-tone complex IQ at 24 kHz, with
one second of pre-roll and the encoded symbol spacing/duration. It places the
WSPR signal at the 30m WSPR dial plus 1500 Hz. A simultaneous tone at the 30m FT8
dial plus 1000 Hz has four times the WSPR voltage amplitude. Both lie in one
capture window, which feeds two actual software slices in irregular packets.

The real router assembles one WSPR-sized and eight FT8-sized periods using an
explicit fixture clock origin. The WSPR job passes through the PCM16 WAV
serializer. A four-frequency correlator then checks the strongest tone in an
interior window of every symbol, accounting for the splitter's reported FIR
delay and discarded startup samples. It checks all 162 expected symbol values
and recovered amplitude, plus refusal to serialize after source revocation.

This qualifies clean known-symbol preservation, shared-window isolation, period
assembly and quantization together. It is not a general WSPR decoder: it knows
the start time and candidate frequencies and does not perform synchronization,
error-correcting decoding, message unpacking or weak-signal/noise qualification.
The ideal waveform also omits the beacon generator's envelope shaping. Do not
report a decoded over-the-air message or external-decoder compatibility from it.

Reproduce with `build-web888-evidence/wspr-signal-validation.cmd`. Ignored local
evidence: `wspr-signal-build.log` and `wspr-signal-tests.log`. By default no WAV or IQ recording
is written by the test; all audio remains in bounded in-memory buffers.

All eight selected tests passed in 4.68 seconds, including the encoder reference
test and the new signal fixture (2.12 seconds). Every expected WSPR symbol and
the amplitude bounds passed. Whitespace and test-registration checks passed.

## Reusable local acceptance fixture

The optional invocation `web888_wspr_signal_test --write-wav <path>` atomically
writes validated WSPR audio with `QSaveFile`, after all symbol checks pass. Default
CTest still writes nothing. No decoder, sound device or transmitter is started.

The local artifact is
`build-web888-evidence/synthetic-30m-wspr-K1ABC-FN42-37.wav` (ignored by Git).
Python's independent standard-library `wave` reader successfully parsed all
1,440,000 frames and verified one channel, 16-bit uncompressed PCM, 12 kHz and
exactly 120 seconds. Evidence: `wspr-wav-independent-validation.log`.
This run's SHA-256 is
`2e975ffc975a6630f9e134045c8af4aac388f32dfbeece1c11916ad977489d5a`.
Floating-point generation can vary across platforms; the hash identifies this
artifact rather than defining a cross-platform golden binary.

Expected content is encoded `K1ABC FN42 37`, with tone zero at 1500 Hz relative
to the 10,138,700 Hz WSPR dial. The synthetic clock origin is the first post-filter
output sample, not a recorded UTC date. Input pre-roll is one second; output onset
includes FIR delay and removal of startup samples, accounted for by the tests.
No external-decoder result is asserted; this file supports later acceptance work.

After adding opt-in export, all eight selected regressions passed again in
4.53 seconds. Explicit export reran all 162 symbol checks before the independent
WAV read succeeded.

## Input-rate qualification

The fixture accepts `--sample-rate <Hz>` in addition to optional WAV export,
keeping 24 kHz as its default. Signal time and symbol duration are generated from
the selected input clock independently of the fixed 12 kHz routed output.
Registered cases cover 12 kHz, 24 kHz, 36,000.125 Hz and 48 kHz, checking all 162
symbol identities and amplitudes, output period counts, FIR/startup alignment,
PCM16 conversion and revocation at each rate. The fractional case exercises
streaming resampling over the entire message, not just an isolated tone.

Reproduce with `build-web888-evidence/wspr-rates-validation.cmd`; evidence is in
`wspr-rates-build.log` and `wspr-rates-tests.log`. These are accepted software
input rates, not a statement of rates available on the untested receiver.
All eleven selected regressions passed in 12.63 seconds. All four signal-rate
cases preserved every expected symbol and passed the amplitude bounds. Whitespace,
test registration and the unchanged seven-test PR gate checks passed.
