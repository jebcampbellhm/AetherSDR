# RFC draft: native receive-only Web-888 backend

Status: local draft, not submitted or approved upstream. Local experimental
development authorized by the operator on 2026-09-29.
Source baseline: `20d022d50566b7d5aa8fc85ddf66d00923c82117`.
Detailed delivery plan: [implementation plan](web888-native-implementation-plan.md).

Revised operator scope: background FT8/WSPR/FT4 should share sound-only IQ
captures and use software decoder slices, with separate interactive listening
and waterfalls on demand. Required IQ channelization now follows first light;
only general-purpose IQ export remains optional. See the
[coverage and channel budget](web888-frequency-coverage.md). The detailed plan
supersedes earlier one-hardware-receiver-per-decoder assumptions in this draft.

## Problem

Web-888 currently uses the external Kiwi receiver path. It cannot act as a
native primary radio with independent Aether slices, shared panadapters and
the neutral per-slice decoder/TCI audio route.

## Proposal

Add one receive-only `IRadioBackend` implementation per Web-888 radio session.
Reuse `KiwiSdrClient` and `KiwiSdrProtocol`; keep vendor wire handling below
`src/core/backends/`. Begin with one sound stream and its paired waterfall,
then add receiver allocation, multiple slices, pan allocation and audio mixing
as separate milestones. TX, administrator configuration, simultaneous primary
Flex operation, virtual DAX and IQ are outside the first-light milestone.

Protocol authority is the open-source RaspSDR/server implementation, originally
inspected at `a66101952c8dec3394e8cb55b3bac64decae680e`, plus captured behavior
on an explicitly identified device. Existing clean-room transport provenance
is documented in [web888-cleanroom-design.md](web888-cleanroom-design.md).
Source-derived expectations below are not hardware results.

## Resource and lifetime contract

| Object | Identity | Resource ownership | Release policy |
|---|---|---|---|
| Slice | Stable local slice ID plus receiver generation | One SND session lease | Release the whole session only if it carries no surviving pan |
| Panadapter | Stable pan ID plus neutral data-plane index | W/F attached to an owned session | Pause production while sound survives; do not close its socket in isolation |
| Display carrier | Protocol session ID, distinct from slice ID | Retained sound session or W/F-only session | Counts against RX capacity until whole-session teardown |
| Pending open | Request ID plus session generation | One reserved local RX budget entry | Roll back on refusal, cancellation or timeout |
| Remote RX channel | Server-reported channel index | Observation only | Never use as a stable slice identity |

Budget: sound sessions + waterfall-only sessions + pending reservations must
not exceed the negotiated RX ceiling. Paired waterfalls do not add an RX
lease, but do consume applicable waterfall capacity. HTTP occupancy is advisory;
only successful session allocation establishes ownership. Do not assume 13
receivers, two spectrum engines, waterfall sharing, or a particular sample rate.

Proposed carrier deletion policy: retain its lease while another slice still
uses the pan, remove its decoder/speaker contribution, and keep charging the
lease to capacity. Expose that retained allocation truthfully. Whole-session
migration is a later policy requiring explicit interruption handling.

All model-facing signals remain on the backend owner thread. Private I/O work
must have bounded queues, teardown deadlines and generation fences. Publish
pre-monitor PCM for decoders separately from the speaker mix. Never relabel
12/36 kHz data as 24/48 kHz; use the existing resampler for first-light 24 kHz.

## First implementation increment

1. Replace millisecond-only transport session IDs with one shared process-wide
   allocator used by both existing Kiwi clients and native Web-888 sessions.
   Remove dependence on wall-clock advancement or rollback; exercise concurrency and
   matching SND/W/F paths without a synthetic firmware server. State the
   process boundary explicitly. Implemented locally: a process-wide atomic
   counter with a randomized 62-bit starting point, in the firmware's signed
   new-timestamp range. Cross-process uniqueness is probabilistic; local
   allocation never wraps. An immutable sound-only transport option uses
   `/no_wf/<id>/SND` on Web-888 and allocates no W/F socket. Both changes have
   socket-free behavioral tests; there is no native backend yet.
2. Add a thin session adapter with injected observations/transport writes,
   explicit sound-only support, refusal rollback and whole-session teardown.
3. Add the single-receiver backend, explicit family registration and a clean
   unavailable result when WebSockets is not built. Audit every permissive
   capability default before advertising anything.
4. Integrate a capability-based connector and one spectrum/audio path. Keep
   unsupported controls unavailable with accessible reasons.
5. Validate before advertising independent slices or additional pans.

Use current indexed meter routing (`SLC<n>:LEVEL`) with definitions carrying
the matching source index. Leave `panZoomModes` absent. `setPanPixelWidth()`
does not authorize changing a server-defined FFT size. Preserve the existing
non-Flex handshake guards and the single neutral spectrum consumer.

## Cross-platform impact

Use existing Qt 6 WebSockets, networking, PCM/resampling and credential
facilities; no new dependency is proposed. WebSockets-disabled builds must
remain valid and refuse native connections clearly. Windows, Linux, macOS and
Raspberry Pi builds need qualification. Virtual audio remains a later platform-
specific integration and must not be advertised merely because PCM works.

## Alternatives considered

- Keep external Kiwi integration only: least work, but lacks native receiver
  allocation and neutral slice routing.
- Reuse Hermes/Metis: rejected as a starting point; presence of HPSDR source
  in the firmware tree does not establish a maintained running endpoint.
- Copy a host-DSP backend: unnecessary for server-demodulated first light and
  duplicates transport/DSP ownership.
- Implement all phases together: creates untestable interactions between
  allocation, waterfall lifetimes, audio, IQ and persistence.

## Hardware evidence matrix

The operator's Web-888 is in transit. All hardware rows are pending. Fill in
device identity, firmware revision, endpoint,
nominal/actual sample rate and configuration before recording results. Do not
store passwords or change administrator settings as part of these checks.

| Check | Evidence required | Status |
|---|---|---|
| Authenticated startup | Sanitized SND/W/F setup and first valid frames | Pending |
| Invalid credentials | Terminal refusal; no repeated login loop | Pending |
| Busy receiver pool | Refusal preserves existing sessions and releases pending budget | Pending |
| Two receiver sessions | Distinct IDs and independently tuned RF/audio | Pending |
| Independent waterfall tuning | Pan movement does not retune slice audio | Pending |
| Paired teardown | Observed SND/W/F partner behavior on whole-session closure | Pending |
| Paused waterfall | Verify `wf_speed=0` preserves sound and lease | Pending |
| Capabilities | Negotiated RX/WF counts, coverage, zoom and FFT size | Pending |
| Server rate | Actual sample rate and valid converted PCM format | Pending |
| Failure/reconnect | No stale audio, meter or pan frame reaches a replacement generation | Pending |

Default tests must be socket-free and registered in `tests/tests.cmake`.
Existing Kiwi/Web-888 parser and startup tests remain regression coverage.
Backend lifecycle, affinity, PCM lifetime and family-switch tests accompany
the new backend. Hardware checks supplement those tests; they do not replace
isolated malformed-input or cancellation assertions.

## Outstanding decisions and evidence

- Confirm current upstream backend-freeze status and RFC approval before an
  upstream contribution. The previous plan recorded a freeze; it has not been
  verified lifted during this checkout refresh.
- The operator chose local experimental implementation with an upstream-facing
  plan. This does not establish upstream maintainer approval.
- Supply an owned test receiver endpoint before live validation; no endpoint
  is inferred from historical public-receiver observations.
- Local Windows build environment established: Qt 6.8.3, MSVC 19.44, CMake
  3.31 and Ninja. See [transport validation](web888-transport-validation.md)
  for the bounded test results; they are not hardware qualification.
