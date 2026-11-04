# Web-888 local decoder process adapter

The engine's `Web888DecoderProcess` accepts one current routed job and an explicit
absolute decoder executable path. It serializes PCM16 mono/12 kHz audio using the
existing WAV writer, commits it atomically in a unique temporary directory, then
invokes WSJT-X's `jt9` for FT8/FT4 or `wsprd` for WSPR. No shell, shared-memory
decoder session, radio-control interface, audio device or WSJT-X GUI is involved.
The executables are optional local dependencies, not bundled with AetherSDR.

One adapter retains at most one job and starts one child. Busy submissions are
refused; the router retains its existing bounded latest-period queues. Deadlines
are configurable from 100 ms to 120 seconds (default 30 seconds). The adapter
retains at most 64 KiB of merged process output; excess output terminates the job.
Source/route revocation is polled every 50 ms and checked again before completion.
Cancellation kills the child and suppresses its output. Temporary input, wisdom
and diagnostic files are removed after the child finishes. Destruction kills the
child before removing its directory. These limits do not measure or bound the
external decoder's own memory or CPU usage.

Completion carries the original job and raw decoder output, or an error with no
output. A consumer must recheck `job.current()` before using a queued completion.
Raw output is not yet a parsed spot model. No production clock is inferred here;
the router still requires an explicitly verified sample-to-UTC anchor.

## Commands and provenance

The command options were checked against the official WSJT-X 3.0.2 executables'
help and the matching `lib/jt9.f90` and `lib/wsprd/wsprd.c` source:

- FT8: `jt9 -8 -p 15 -d 2 -w 0 -m 1 -a <directory> -t <directory> <wav>`.
- FT4: `jt9 -5 -p 7.5 -d 2 -w 0 -m 1 -a <directory> -t <directory> <wav>`.
- WSPR: `wsprd -H -f <dial MHz> -a <directory> <wav>`. Hash-table persistence is
  disabled; only basic callsign/grid/power decoding is qualified by this fixture.

Each job uses a UTC-shaped filename because the tools derive their displayed
time from it. The precise epoch, RF dial and cancellation witness remain on the
job; filename formatting does not establish receiver timing accuracy.

Official [WSJT-X downloads](https://wsjtx.github.io/wsjtx/downloads.html) point to
the [3.0.2 release](https://github.com/WSJTX/wsjtx/releases/tag/v3.0.2). The local
Windows installer was downloaded and its `bin` directory extracted with 7-Zip;
the installer and GUI were not executed. Installer SHA-256 matches the release
API digest: `4d3a5ee72652abe4dc4d74b1e15ea4d96cb4af5b519d8d93139cf9a5e0ebe414`.
Downloads, extracted tools and recordings stay in ignored local evidence storage.

## Offline fixtures

| Mode | Source and expected content | Preparation |
| --- | --- | --- |
| FT8 | Official `ft8sim`, message `CQ K1ABC FN42`, 1500 Hz, DT 0, no spread/delay, one file, −10 dB | 180,000 samples, unchanged audio |
| FT4 | [Official versioned sample](https://raw.githubusercontent.com/WSJTX/wsjtx/v3.0.2/samples/FT4/000000_000002.wav), including `CQ RU AB5XS EM12` | 72,576 recorded samples plus 17,424 explicit fixture-only silent samples to fill 7.5 seconds |
| WSPR | Existing synthetic IQ split fixture, `K1ABC FN42 37` with an adjacent stronger tone | 1,440,000 samples, unchanged audio |

The FT4 fixture tail is test preparation, not gap filling in the production
router. Production gaps still disarm routes. The initial SourceForge request
returned an HTML page and was rejected as a WAV; the versioned repository sample
above is the source actually used. Its SHA-256 is
`d9e91fa04ba138a7b9f41b4103823c77ca1c3a9775101f6b14d60935bcd3813b`.

Canonical fixture WAV hashes:

- FT8: `a996d945128f0658ec1c44b367cdf8f556f708837fb4011eac8a1c16b02bfb8f`.
- FT4: `d72ad4eb68d41cdefa264e80f6635ee00613e7febffcf51c9eb2797121870a78`.
- WSPR: `2e975ffc975a6630f9e134045c8af4aac388f32dfbeece1c11916ad977489d5a`.

`web888_decoder_process_test` normally uses its own controlled child process to
test supervision; it does not simulate radio firmware or assert decoding quality.
The opt-in `--real <jt9> <wsprd> <ft8.wav> <ft4.wav> <wspr.wav>` path feeds the
prepared PCM through the real router, serializes each completed job and invokes
the selected tools. It requires the expected messages, not merely a zero exit.
No downloads occur in the test. Local reproduction script:
`build-web888-evidence/decoder-process-validation.cmd`.

On 2026-10-02 the adapter and desktop built with MSVC 19.44/Qt 6.12.0. All four
selected process/router/period/WAV tests passed in 2.61 seconds. Supervision
coverage includes successful cleanup, busy refusal, output overflow, timeout,
explicit cancellation, route revocation, failed process startup and immediate
reentrant dispatch from completion. The opt-in real-tool run then passed all
three modes through router → WAV → process, recovering `CQ K1ABC FN42`, 19 FT4
messages including `CQ RU AB5XS EM12`, and `K1ABC FN42 37`. Logs:
`decoder-process-build.log`, `decoder-process-tests.log`, `decoder-process-real.log`
under `build-web888-evidence`. The first opt-in fixture run erroneously marked
every input block discontinuous; the router rejected it. Correcting that test
input restored the intended continuous recording; no production gap guard changed.

FT8 and FT4 acceptance starts at decoded PCM, not RF IQ. WSPR uses the existing
IQ-split synthetic fixture. The FT4 file was decoded successfully by the same
official tool before routing, establishing the expected messages for the
round-trip check. These are file-based software acceptance results.
Hardware, production timing, concurrent decoder load, parsed spot delivery and
desktop monitoring integration remain separate qualification work.
