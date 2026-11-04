# nRSP-ST direct receive protocol

This is an experimental interoperability specification for the SDRplay nRSP-ST
TCP service, verified against firmware **1.0.15**. It describes a direct client
that receives radio-generated spectrum rows and per-receiver IQ without running
SDRconnect or its WebSocket exporter. It is not an SDRplay-supported API.

The command descriptions and acceptance claims below are grounded in captures
of SDRconnect/NRSPAdministrator and direct-client trials on an operator-owned
radio. Initial research also used binary analysis to accelerate identification
of framing and the public verification key. The implementation is original;
no proprietary source or decompiler output is included. This provenance is not
a claim of conformity with the upstream project's end-to-end clean-room rule.

## Evidence and implementation scope

`tests/fixtures/nrsp/wire-examples.json` preserves 48 original wire frames from
eight sessions, their capture references, decoded payloads, and SHA-256 hashes.
It includes compressed and uncompressed IQ, display rows, and a challenge/reply
pair. Fresh connections must generate fresh challenges; the fixture is offline
test data, not a connection credential. `nrsp_protocol_test` consumes this data
without a radio or synthetic server.

The separate research workspace holds the original pcapng files, longer trial
logs, and Python prototypes. Those logs distinguish control requests from
radio readbacks and measure IQ continuity independently of display traffic.
The `nrsp-st` integration has received live 48 ksample/s IQ through its native
backend and displayed spectrum/waterfall rows in the desktop application.
USB, AM, FM, CW and passband changes have desktop readback evidence; synthetic
modulated-signal tests additionally cover LSB and SAM. A desktop control trial
confirmed 14.1 MHz A/B/C selection, the ordered C-to-A transition before
430.5 MHz tuning, and return to HF. Independent captured hellos establish each
shared state, and final readback confirms restoration to the original radio
configuration. Evidence: `aether-native-controls-fixed.pcapng` and its
`control-and-edge-analysis.json` in the research workspace.
Initial standalone validation delivered 351,744 audio frames and 150 spectrum
rows over 15 seconds. These counts establish reception, not sample-exact
continuity or RF calibration.

A 12-minute native receive run delivered 17,272,320 audio frames and 7,321
spectrum rows without an IQ watchdog failure, including verification renewal
past the observed ten-minute expiry. Account-enabled native trials confirmed
an eligible sole client acquiring control, a secondary client receiving
without control, and an account with **Grant Control** disabled receiving
354,816 audio frames and 150 rows in 15 seconds. Missing credentials and an
incorrect password both terminated before receiver creation with distinct
errors. Evidence is in `aether-native-renewal-12min.json` and the
`aether-account-native-*.json` research results.

A separate 12-minute authenticated run with **Grant Control** disabled
delivered 17,272,320 audio frames and 7,322 spectrum rows without an error;
every source update retained owner=false. Desktop login through the new
username/password fields also succeeded. An antenna-B request was refused,
an out-of-capture tune left the observed slice frequency unchanged, and an
in-capture tune to 910.6 MHz succeeded while the shared source stayed at
910.55 MHz. The automation bridge's tune response echoes the request, so this
trial checks subsequent model readback rather than treating that response as
hardware acceptance. Evidence: `aether-authenticated-readonly-12min.json` and
`aether-account-ui-confirmed.json`. The temporary authentication requirement
and account control grant were restored to their original values afterward;
`admin-settings-restored-1101.json` records the final RF/authentication readback.

A final 45-minute native receive run delivered 64,792,064 audio frames and
27,463 spectrum rows with no reported error or shared-setting changes. The
backend and demodulator averaged 4.43% of one CPU core over the sampled
post-startup interval, with private memory between 34.56 and 34.66 MiB. This
excludes the GUI and speaker device and is not a controlled SDRconnect
comparison. Results and measurement limits are in
`aether-native-final-45min.summary.json`; a fresh connection afterward confirmed
the restored RF settings and authentication-off state in
`aether-final-radio-state.json`.

## Transport and framing

The observed service uses **TCP port 50000**, discovered as
`_sdrplay-nrsp-connect._tcp.local`. TCP read boundaries are unrelated to frames.

Every frame begins with a big-endian 32-bit word XORed with the current header
mask. After unmasking:

| Bits | Meaning |
|---|---|
| 31–24 | Message type |
| 23–4 | Padded payload length in bytes |
| 3–0 | Trailing padding count |

The padded length is a multiple of 16, no more than `0xffff0`. Padding must not
exceed that length. A client must cap its receive buffer, tolerate fragmented
and coalesced frames, and reject malformed lengths before allocating payloads.

Payload transformation is 32-round XTEA, using little-endian 32-bit block
halves. **Only the first half of the padded payload** is transformed, in
eight-byte blocks. The second half remains clear. Type **203** whole-source IQ
has an entirely clear payload, although its header is still masked. Remove
padding after the transformation; zero padding is used in outgoing commands.

The initial mask is `3f5e220a`; initial key words are
`fe45a343 2ef57aff aa500913 fec3326a`. These are protocol bootstrap constants,
not a mechanism for keeping LAN traffic confidential. Type 201 replaces them
with a connection-specific key and mask immediately, including for another
frame already buffered in the same TCP read.

Scalar 32-bit integers are big-endian. A 64-bit integer is **low 32-bit word
first, each word big-endian**. Signed values use two's complement. IQ samples
are an exception: signed 16-bit little-endian I,Q pairs.

The IQ orientation is conventional `I + jQ`: raising a receiver's tuned
frequency moves the same RF content toward negative baseband frequency. In
`live-slice-config-20261022-181100Z`, paired simultaneous receivers measured
−50000.85 Hz for a +50 kHz retune and +49999.77 Hz for a −50 kHz retune, with
complex correlations of 0.79 after timing/frequency alignment. The native
adapter conjugates Q when handing these samples to WDSP, whose receive-mode
convention is opposite. Synthetic positive/negative tones pin USB/LSB selection
and at least 30 dB of opposite-sideband rejection.

## Hello and source state

Type **201** has a 172-byte observed payload starting `SDRP`. Offsets are hex.

| Offset | Encoding | Meaning |
|---|---|---|
| `08` | u64, low word first | Shared source frequency, Hz |
| `10` | u32 | Shared source sample rate, samples/s |
| `14` | u32 | Front-end AGC enabled |
| `18` | u32 | Stored manual IF reduction |
| `1c` | u32 | LNA index |
| `20` | u32 | Antenna: 0=A, 1=B, 2=C |
| `24` | u32 flags | bit 0 source owner; bit 1 DAB notch; bit 2 RF notch |
| `2c` | u32 | Shared display cadence setting |
| `30` | u32 | Unresolved; 131072 observed |
| `48` | four u32 | Session XTEA key words |
| `58` | u32 | Session header mask |
| `5c` | u32 flags | bit 7 (`0x80`): streaming account login required; other bits unresolved |
| `6c` | 64 ASCII bytes | Per-connection lowercase hexadecimal login nonce |

Type **202**, 64 observed bytes, starts with the same source fields at offset
zero: frequency at `00`, rate `08`, AGC `0c`, manual IF `10`, LNA `14`, antenna
`18`, flags `1c`. The tail remains unresolved. A 202 is asynchronous state,
**not an acknowledgement correlated to a particular request**.

In the captured trials, secondary clients received changes while the source
owner did not receive matching 202 notifications for its own commands. Confirm
an owner's changes with a fresh type-201 hello on a short-lived read-only
connection. That observer's owner bit belongs to the observer, not the main
session: copy the shared RF fields while retaining the main session's authority.
The native worker pauses delivery during this readback, caps it at five seconds,
and closes the observer without creating a receiver or sending a command.

Source control belongs to one client. Non-owner source requests were ignored.
Ownership can change during uninterrupted IQ delivery, both when the owner
disconnects and when an administrator gives control to another session. Honor
each 202 owner-bit change; never cache permission solely from the hello.

## Streaming account login

With **Require authentication** enabled in NRSPAdministrator, the client must
complete account login **before** sending the server verification challenge.
Sending type 51 first produced no type 52 during three bounded four-second
trials. The hello flags changed from `0x7f` to `0xff` when authentication was
enabled and returned to `0x7f` when the original policy was restored.

Type **58** has an 80-byte payload:

| Offset | Length | Content |
|---|---|---|
| `00` | 16 | UTF-8 username, zero-padded; reject embedded NUL or more than 16 bytes |
| `10` | 64 | Lowercase ASCII hex challenge response |

Compute the response as:

```text
passwordHash = lowercase_hex(SHA256(UTF8(password)))
response = lowercase_hex(SHA256(helloNonceASCII || ASCII(passwordHash)))
```

The 64-byte hello nonce is ASCII, not hex-decoded binary. Fresh connections
have different nonces. Type **221** carries one u32: **1** accepted the tested
account, while **0** rejected an intentionally wrong password. Other status
values remain unresolved and must be reported as failures. Account credentials
and captured login responses are excluded from repository fixtures.

**Accepted login does not mean source control.** With the test account's
**Grant Control** disabled, type 221 still returned 1, type 202 reported owner
false, and the client received type 216 IQ and type 212 waterfall rows. The
same account with a wrong password returned 0 and was not started as a receiver.
Evidence: `sdrconnect-account-login.pcapng`,
`account-login-direct-login-first.json`, and `account-login-no-control.json`
in the separate research workspace.

The native client accepts transient `nrsp.username` / `nrsp.password` connect
parameters. It sends type 58 only when the hello requires it, applies a
five-second login deadline, and reports missing or rejected credentials
separately from receiving without control. Credentials are not persisted or
retained for automatic reconnect. RF authorization continues to follow source
state, including changes during an active session.

## Session verification and liveness

The 51/52 exchange verifies the device and maintains the stream independently
of account login. Complete account login first when the hello requires it.

Type **51** is a client challenge: current Unix milliseconds in the 64-bit
format above, followed by 256 cryptographically random bytes. Type **52**
echoes the eight timestamp bytes, followed by a Base64 RSA-2048 signature.
Verify PKCS#1 v1.5 / SHA-256 over **the 256-byte nonce only**. Reject a timestamp
mismatch, malformed Base64, wrong signature size, or failed verification.

The public key is preserved in the fixture. SHA-256 of the research PEM file:
`68b530ac261151bc2da59cf62afa90a6738e125c9f2208c4f1a4e39f6fb2da6c`.
No private key is needed or included. Signature verification uses Windows CNG,
macOS Security, or OpenSSL Crypto on Linux. Platform API references:
[BCryptVerifySignature](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptverifysignature)
[SecKeyVerifySignature](https://developer.apple.com/documentation/security/signing-and-verifying),
and [EVP_DigestVerify](https://docs.openssl.org/3.0/man3/EVP_DigestVerifyInit/).

Send type **17** pings about once a second; type **207** is the observed reply.
Renew verification with a fresh 51/52 exchange every **300 seconds**. A
40-minute direct receive trial sustained IQ with these renewals. A single
verification expired around 601–602 seconds, with type **222**, u32 value 1.
Streaming without 51/52 verification lasted only about 14 seconds. A challenge sent after
expiry did not recover the session in the five-second observation window;
a new connection did. The full 222 status vocabulary is unknown.

Use separate deadlines for hello, outstanding verification, and IQ delivery.
Pongs and waterfall rows do not prove that IQ is alive. An administrator's
Disconnect action produced TCP FIN/EOF without a 222; avoid automatically
reconnecting in a loop against that operator action.

## Receiver creation and direct IQ Lite

All receiver IDs below are u32. The observed working sequence is:

1. Complete hello, required 58/221 account login, and 51/52 verification.
2. Send **54** `{rid}` to create a raw receiver.
3. Send **55** `{rid, decimationIndex}` and **56** `{rid, outputHz}`.
4. Send **31** `{rid, displayBins}`; configure display levels if needed.
5. Send **30** `{rid, 1}` to enable.
6. Send **32** `{rid, absoluteFrequencyHz:u64}` **after enabling**.

Disable with **30** `{rid, 0}` and remove with **29** `{rid}`. Type **15**
`{enabled}` controls whole-source IQ; IQ Lite clients do not need that stream.

Decimation indices 0–6 were verified: input rate is source rate divided by
`2^index`. Type 56 enables a receiver resampler; zero disables it. Observed
outputs include 12k, 32k, 32768, 44.1k, 48k, 96k, and 192k samples/s, with
sufficient decimated input bandwidth. Live 32 tuning works without stopping.

| Type | Payload after framing |
|---|---|
| 216 | u32 receiver ID, then interleaved s16le I,Q |
| 217 | u32 receiver ID, then one Zstandard frame yielding s16le I,Q |
| 212 | u32 receiver ID, then one Zstandard frame yielding unsigned display bins |
| 203 | Whole-source IQ; no receiver-ID prefix; payload transformation bypassed |

Dispatch compression by message type. It is not safe to infer it from output
rate: 32001 compressed and 32700 uncompressed were both observed. Require one
complete Zstandard frame with no trailing or concatenated data, bound output
to 1 MiB for IQ / 16384 bytes for display, and require IQ length divisible by
four. The 16-bit transport container does not imply a 16-bit ADC.

For a per-receiver rate change, disable, drain queued data, update 55/56/31,
enable, then retune. Another receiver continued throughout these trials. No
sample timestamp or exact rate-transition boundary has been established.

For source-rate changes, remove all receivers owned by this client, send 3,
wait for source-state readback, then recreate/configure them. A tested
2→10→4→2 MS/s sequence used this lifecycle. Merely retaining disabled receiver
objects caused a service interruption in an earlier trial; its cause remains
unresolved. Concurrent source-rate changes with other clients' receivers
have not been qualified.

## Source controls

| Type | Payload | Verified meaning |
|---|---|---|
| 2 | u64 frequency Hz | Shared source tuning |
| 3 | u32 samples/s | Source rate; 2M, 2.048M, 4M, 6M, 8M, 10M tested |
| 7 | u32 bool | Front-end AGC |
| 8 | u32 reduction | Manual IF reduction; 21/22 transitions tested |
| 9 | u32 index | LNA stage index; 0/1 transitions tested |
| 10 | u32 bool | DAB notch |
| 11 | u32 bool | RF notch |
| 12 | u32 antenna | A=0, B=1, C=2 |

Antenna C is limited to 200 MHz. Tune below that limit **before** selecting C;
select A or B **before** tuning above it. Apply the same ordering when restoring
test settings. LNA range varies with frequency; do not advertise one universal
dB scale or extrapolate beyond verified stages. Source controls require the
current owner bit, and source readback supersedes local requested values.

Bias-T (26), reference clock (27), network, accounts, and firmware settings
are outside this receive integration. Hardware band-filter selection is left
automatic, as requested by the operator.

## Waterfall and display controls

At 2 MS/s, native desktop captures at 14.1, 430.5, and 910.55 MHz showed the
outer 5% of the display approximately 20 relative dB below its center. The
edges of the central 80% were within about 1 dB. These are observations of
display noise-floor shape, not calibrated receiver passband measurements.
The initial view crops to that central 80% while preserving frequency mapping;
full source span remains selectable. Other source rates need characterization.

The desktop seeds software automatic black level from the first absolute-dB
waterfall row. Starting from the positive Flex tile-intensity seed otherwise
blanks the initial nRSP rows until smoothing converges. A matched before/after
desktop trial found zero nonblack pixels among 9,000 sampled waterfall pixels
at four seconds before this correction, versus 8,715 afterward; the unchanged
display settings and continued row delivery are recorded in
`aether-waterfall-levels*.json` and `waterfall-startup-pixel-evidence.json`.

Type **31** `{rid, width}` selects display bins; 512, 628, and 1024 were verified.
Changing width while receiving can leave one old-width row queued. Bound and
handle that transition; do not reinterpret a row using a different width.

Types **37** and **38** carry `{rid, signedLevel}` for top/bottom display levels.
Moving both by +12 shifted 8-bit rows by approximately −25.5 counts for a
120-unit span without changing IQ. This establishes relative scaling, **not
absolute dBm calibration**.

Type **42** `{cadence}` is shared and owner-controlled. Settings 3/12/24 were
read back in a fresh hello and measured around 3.38/10.2/20.4 rows/s at 2 MS/s.
The number is not an exact FPS guarantee. Secondary-client requests were
ignored; 202 does not report this setting. Preserve the existing value on
connect instead of imposing a saved client default.

Type **36** `{rid, zoom:u32, sourceRelativePanHz:i64}` and type **35**
`{rid, sourceRelativePanHz:i64}` control display geometry independently of
receiver IQ. Observed zoom 0 spans the source; zoom 1 is about 70.8%; zoom 2
about 50.15%. At zoom 2, ±250 kHz pan changes left the IQ unchanged. Higher
zoom geometry is not yet qualified.

Type **43** accepts an FFT-size-shaped u32, but the requested 32768/65536/131072
values had no established effect. The hello field and type 218's second word
stayed 131072. Do not expose this as a confirmed FFT-resolution control.

## Demodulation, filters, and status

For raw IQ Lite, final USB/LSB/CW/AM/FM demodulation and passband filtering
belong in AetherSDR's host DSP. Type 33 with a 6000-Hz width did not alter raw
receiver IQ. A separate compact receiver mode (28 create, 34 demodulation,
33 width, 211 Opus audio) does filter in the radio: USB 2400→1800→2400 tests
attenuated 2050–2300 Hz by about 46 dB. That path is optional and not a
substitute for direct IQ Lite.

| Type | Observed payload / qualification |
|---|---|
| 208 | Gain components including signed whole/fraction fields; calibration unresolved |
| 209 | u32 overload state; only clear state observed |
| 215 | u32 unknown, i32 gain/256, u32 reported IF; not the stored manual IF value |
| 218 | rid, u32 unknown (131072), i32 whole, i32 fraction/1000; retain components |
| 222 | u32 connection status; 1 accompanied verification expiry |
| 223 | u32 connection count, u32 zero-based client index, unresolved tail |

Type **45** has a structurally decoded `{rid, u64 unknown, i32 fixed8 scale,
u32 enabled}` payload, but enabled parameters are unverified. HDR controls
are unverified. Neither should become an apparently working AetherSDR control.

## Administrator observations

The administrator service on port 8081 used TLS 1.3. Its application messages
were opaque in the captures; this document does not invent HTTP endpoints.
Observed administrator actions were correlated with the receive connection:
Give Control changed both clients' 202 owner flags without interrupting IQ;
Disconnect closed the selected connection and transferred ownership back to
the remaining client. Authentication and the test account's control grant were
temporarily changed for permission tests and restored afterward. No changed
administrator configuration was left in place.
