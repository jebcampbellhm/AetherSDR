# Web-888 Clean-Room Design Note

Status: receive-only Web-888 support uses the **shared KiwiSDR protocol client**,
with an external RX source and a standalone `IRadioBackend` session. The wire
authority is the open-source Web-888 server plus black-box observations against
a live receiver made in this thread.

## What Web-888 Is

Web-888 is a KiwiSDR server fork: the `RaspSDR/server` project (GPL, a clone of
`jks-prv/Beagle_SDR_GPS`) running on a Zynq SoC board. It speaks the same
WebSocket protocol as a KiwiSDR with small deltas, so AetherSDR serves it
through the same `KiwiSdr*` classes (`vendor(kiwi)` tagged — see
`docs/kiwisdr-cleanroom-design.md` for the Kiwi-side design note). A profile
records which family its endpoint serves
(`KiwiSdrProtocol::KiwiSdrReceiverFamily`) and the client applies the deltas
below; everything else is the shared Kiwi path.

## Allowed Inputs Used

- User request: add integration with a Web-888 SDR "related but not identical"
  to the KiwiSDR, with a live receiver offered for probing
  (`http://web888.servehttp.com:8074`).
- User-scoped decisions from 2026-09-08: ride the existing Kiwi-style applet
  path (explicitly not a new `IRadioBackend` family); scope is RX audio +
  spectrum/waterfall, tuning/remote control, and reuse of the Kiwi UI/applet;
  no public receiver directory; receive-only (Principle VI).
- Clean input: the `RaspSDR/server` repository (GPL open-source — open-source
  references are clean inputs per Principle IV). Its server sources were
  consulted only to confirm the wire contract AetherSDR already implements for
  Kiwi (auth, stream URIs, frame layouts) and to locate the fork deltas below;
  no Web-888-only feature outside the shared Kiwi receive contract was
  implemented from it.
- Black-box live observations against `web888.servehttp.com:8074` in this
  thread: HTTP `Server: ZynqSDR_Mongoose/2026.609`, `sw_version=Web888_v2026.609`
  in `/status`, the staged connect handshake on `/kiwi/<ts>/SND` and `/W/F`,
  and the `SET`/`MSG` traffic on both sockets.

## Verified Wire Compatibility

Observed identical to KiwiSDR on the live receiver (so all handled by the
existing, unchanged client code):

- Stream URIs `/kiwi/<ts>/SND` and `/W/F`; the same staged handshake.
- Auth: `SET auth t=kiwi p=<pwd>` (`p=#` when passwordless) and the same
  `SERVER DE CLIENT ... SND` / `... W/F` greeting strings.
- SND frames: 10-byte header, PCM16 and IMA-ADPCM layouts, sequence/RSSI
  fields; W/F frames: 16-byte header with the `0x00010000` compressed flag and
  zoom bits already handled by the Kiwi decoder.
- `/status` HTTP preflight: same tolerant `key=value` body (`rx_chans=13`,
  `wf_chans=13`, `zoom_max=11`, `wf_fft_size=1024`, `audio_rate=12000`,
  `center_freq=15360000 bandwidth=30720000`, `sw_version=Web888_v2026.609`).
- Default port: Web-888 also defaults to 8073 when the endpoint has no port;
  8074 is just an explicitly chosen port.
- `SET ident_user`, keepalive, AGC/squelch/mod command grammar: identical.

## Deltas And How Each Is Handled

1. **All server→client frames arrive as binary WS frames** — including the
   `MSG` control text Kiwi sends as text frames. AetherSDR's
   `handleBinaryMessage` already routed binary `MSG` frames to the text path;
   that dispatch is now a pure classifier
   (`KiwiSdrProtocol::classifyInboundFrameTag`, socket-free testable) and the
   client switches on it. Text frames are still accepted (per-family tolerant).
2. **W/F setup must be (re-)sent after the server's config burst.** Kiwi
   accepts `SET` waterfall commands immediately after connect; Web-888 stages
   its `wf_setup` config burst first and the setup commands sent before it are
   not applied. `KiwiSdrClient` therefore re-sends the waterfall setup sequence
   once after processing the bare W/F `wf_setup` marker and all metadata in
   its message — family-gated (Web888 only) and
   one-shot (`m_waterfallSetupResent`); a no-op for Kiwi, idempotent for
   Web-888. The replay invalidates the cached view so an unchanged zoom/start
   is sent again. Earlier messages, repeated markers, sound-stream markers,
   disconnected transports, and monitor sessions do not consume the replay.
3. **Server version markers.** `parseKiwiVersionFromServerHeader` now also
   recognizes `Web888_` and `ZynqSDR_Mongoose/` markers alongside `KiwiSDR_`,
   so the protocol summary shows the fork's version instead of "unknown".
4. **`audio_rate` in the SND config burst.** Web-888's burst is expected to
   carry the same `audio_rate` token the SND setup is staged on. If a live run
   shows `cfg_loaded` arriving without `audio_rate`, a diagnostic trace fires
   (`WEB888 cfg_loaded without audio_rate`); the family-gated fallback that
   promotes `cfg_loaded` is applied only if the live test proves the token is
   absent. No rate is fabricated.

Everything else — endpoint normalization, camp/monitor, EXT-ignore, waterfall
view/zoom clamps from server MSG values, TX mute (`KiwiSdrTxMuteLatch`) — is
profile-keyed and family-agnostic and needs no change.

## Implementation Shape

- `src/core/KiwiSdrProtocol.{h,cpp}`: family enum + name/id/fromString
  helpers; `classifyInboundFrameTag`; version-marker loop.
- `src/core/KiwiSdrClient.{h,cpp}`: `setReceiverFamily()` (set by the manager
  before the client thread starts); Web-888 waterfall re-send guard;
  family-named user-facing state text; binary-frame dispatch via the classifier.
- `src/core/KiwiSdrManager.{h,cpp}`: `KiwiSdrAntennaProfile.family`, persisted
  in settings JSON (optional key, legacy → Kiwi) and in the receiver-list CSV
  as an optional `RECEIVER_TYPE` column (`KIWI`/`WEB888`; absent → Kiwi, an
  unrecognized non-empty value is an import error). A family change reconnects
  the profile (joined with the existing endpoint-change trigger) and is pushed
  to the live client.
- `src/gui/RadioSetupDialog.cpp`: TYPE combo ("KiwiSDR"/"Web-888") on the
  configured-receiver rows and the add-receiver row.
- `src/gui/KiwiSdrApplet.{h,cpp}`: family on `KiwiSdrReceiverStatus`; a
  "Web-888" badge next to the receiver name and the family in the accessible
  text. `MainWindow_KiwiSdr.cpp` populates it.

## Constant-row recovery

On a live Web888_v2026.919 receiver, direct 1024-bin rows at zoom 11 contained
only byte 55. Changing interpolation (13, 10, 0) or the FFT window (0, 1, 3)
did not restore variation. Zoom 10 repeated byte 83 apart from the DC edge;
zoom 9 and wider returned varying bins. The server advertised zoom_max=14
and echoed the requested start and zoom correctly.

The client excludes 16 edge bins on each side and waits for 20 consecutive
constant rows before lowering its session-only request ceiling by one. Such
rows do not seed trace stabilization or waterfall autoscale. The advertised
zoom maximum still determines header decoding and the fixed-point frequency
scale. Old rows above the learned ceiling are discarded. The viewport stays
unchanged and the existing frequency-aware row mapping crops the wider data.
Disconnect resets recovery; ordinary Kiwi profiles do not use it. The
socket-free waterfall setup test covers the threshold, varying-row reset,
stale frames, repeated recovery, unchanged viewport and reconnect reset.

## Non-Goals

- No TX of any kind (Principle VI) — the Kiwi path is already control-
  suppressed and TX-muted; Web-888 rides it unchanged.
- No `/EXT`, `/admin`, or `/MON` streams; no IQ/DRM 20-byte-header streams.
- No public receiver directory for Web-888 endpoints (out of scope per the
  user's scoping; Kiwi's directory feature is Kiwi-only).

## Standalone connection (local fork, October 2026)

The native speaker path declares a 360 ms host startup/refill reserve through
`ReceiveAudioBuffering`. Qualification observed occasional incoming-frame gaps
longer than the ordinary 100 ms native queue cap, including a 215 ms gap, and
brief sink-idle periods. The common audio engine holds initial audio, then
consumes the reserve across delivery gaps; it refills only after exhaustion.
Capacity is bounded and accommodates the reserve. No wire command, family
branch in the audio engine, or additional decoder-export delay is introduced.

The operator explicitly requested a separate Web-888 connection option on
2026-10-16, superseding the earlier external-source-only scope for this fork.
`Web888Backend` owns a shared `KiwiSdrClient` configured for Web888. It emits
normalized slice/pan deltas, stereo 24 kHz PCM, and viewport spectrum frames
through `IRadioBackend`. It owns no Flex connection or simulator. Network I/O
is asynchronous on the backend owner thread; teardown closes both sockets
and fences pending credential callbacks by session generation.

The connection page has its own Web-888 choice, port, and user-password
field. Endpoint-bound passwords are read asynchronously from QtKeychain and
saved only after an accepted connection. Recent-address route documents hold
family and port only. The existing external RX source is unchanged.

This first standalone contract admits one demodulated receiver and one pan,
with no TX, IQ, admin, public-directory, or daemon discovery claim. Wider
rows used for narrow-zoom recovery are mapped by RF frequency into the
selected viewport before publication.

### Windows background receive

A long receive run exposed a separate Windows desktop-lock issue: Qt's
QRhi-backed widget compositor repeatedly waited about 750 ms in frame startup
while the session was locked. Those GUI-thread waits delayed seam audio.
Stack-only samples identified `QRhi::beginFrame` / backing-store flush; a
read-only session query confirmed the locked state. Minimizing the application
removed the waits without changing the receiver connection.

The GUI's `DisplaySessionMonitor` registers a hidden message-only window for
same-session Windows lock/unlock notifications and reads the initial state.
It suppresses painting, including already-posted widget update requests,
while preserving event delivery and reception. Unlock restores updates on
surviving windows that were previously enabled. Windows opened while locked
are covered too. If notification registration fails, it leaves painting
enabled rather than creating a state from which it cannot recover.

The injected-state GUI test covers existing and new windows, queued control
delivery, deleted windows, and restoration of deliberately disabled updates.
No screen lock, power, or security policy is modified. Native API authority:
[Microsoft session notifications](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsregistersessionnotification)
and [session flags](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/ns-wtsapi32-wtsinfoex_level1_w).
