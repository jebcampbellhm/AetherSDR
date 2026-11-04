# Web-888 digital frequency coverage

Hardware note (2026-10-16): 12/24 kHz passed live transport rate checks, but the
installed v2026.919 firmware at 36 kHz delivered about 40 ksample/s while
advertising 36.014 ksample/s. The 36 kHz column below is an unqualified planning
target pending resolution. At 24 kHz the 12-capture all-band base profile leaves
only one of 13 RX channels free; reserve two listeners by reducing the digital
band profile. See [live validation](web888-live-validation.md).

Planning revision: 2026-09-29. Receive only; calculated coverage, not a hardware
measurement. Eight bands, 80–10m; 60m and DXpedition/contest alternate frequencies
are outside the base profile. Presets remain editable.

## Frequency and bandwidth assumptions

All dial frequencies below are USB references in **kHz**. FT8/FT4 software slices
cover dial + 0…3000 Hz; WSPR covers dial + 1400…1600 Hz. A capture must contain
the complete RF intervals plus **1000 Hz at each outer edge**: 500 Hz for the
requested server filter edge and another 500 Hz for transition/uncertainty.
This is a provisional design margin, to be measured when the receiver arrives.
Never calculate coverage from dial-frequency separation alone.

The base frequencies follow the [WSJT-X frequency list](https://github.com/WSJTX/wsjtx/blob/master/models/FrequencyList.cpp),
checked 2026-09-29. The optional 80m WSPR dial 3592.6 comes from the inspected
[Web-888 firmware WSPR centers](https://github.com/RaspSDR/server/blob/a66101952c8dec3394e8cb55b3bac64decae680e/extensions/wspr/wspr_main.cpp)
(3594.1 kHz RF center minus 1.5 kHz). It is not the current WSJT-X base preset.
Firmware FT4 defaults differ on 80m (3575.5); use WSJT-X's 3575.0 here.

## FT8 + WSPR base profile

“Required span” includes both 1 kHz outer margins. When the two portions do not
fit together, allocate two captures; do not scan or retune during decode periods.
Centers are suggestions at 36 kHz, not commands that change the server rate.

| Band | FT8 dial | WSPR dial | RF interval enclosing both | Required span (kHz) | Captures at 12 / 24 / 36 kHz | Suggested 36 kHz center(s) |
|---|---:|---:|---|---:|:---:|---|
| 80m | 3573.0 | 3568.6 | 3570.0–3576.0 | 8.0 | 1 / 1 / 1 | 3573.0 |
| 40m | 7074.0 | 7038.6 | 7040.0–7077.0 | 39.0 | 2 / 2 / 2 | 7040.1; 7075.5 |
| 30m | 10136.0 | 10138.7 | 10136.0–10140.3 | 6.3 | 1 / 1 / 1 | 10138.15 |
| 20m | 14074.0 | 14095.6 | 14074.0–14097.2 | 25.2 | 2 / 2 / 1 | 14085.6 |
| 17m | 18100.0 | 18104.6 | 18100.0–18106.2 | 8.2 | 1 / 1 / 1 | 18103.1 |
| 15m | 21074.0 | 21094.6 | 21074.0–21096.2 | 24.2 | 2 / 2 / 1 | 21085.1 |
| 12m | 24915.0 | 24924.6 | 24915.0–24926.2 | 13.2 | 2 / 1 / 1 | 24920.6 |
| 10m | 28074.0 | 28124.6 | 28074.0–28126.2 | 54.2 | 2 / 2 / 2 | 28075.5; 28126.1 |
| **Total** | | | | | **13 / 12 / 10** | **16 software outputs** |

Adding the optional 3592.6 WSPR dial while retaining 3568.6 extends 80m coverage
to 3570.0–3594.2 (26.2 kHz including margins). Use center **3582.1** at 36 kHz.
That gives 17 software outputs and totals **14 / 13 / 10 captures** at 12/24/36 kHz.

## FT4 and additional modes

| Band | FT4 USB dial (kHz) | Added captures at 36 kHz |
|---|---:|---:|
| 80m | 3575.0 | 0 |
| 40m | 7047.5 | 0 (fits the WSPR capture) |
| 30m | 10140.0 | 0 |
| 20m | 14080.0 | 0 |
| 17m | 18104.0 | 0 |
| 15m | 21140.0 | 1 (suggested center 21141.5) |
| 12m | 24919.0 | 0 |
| 10m | 28180.0 | 1 (suggested center 28181.5) |

Thus the selected FT8 + WSPR + FT4 profile needs **12 background captures** at
36 kHz. Other modes are added by explicit RF intervals and decoder requirements;
“the digital portion of each band” is not assumed to fit one capture.

## Capacity and listening policy

The [firmware release notes](https://www.rx-888.com/web/downloads) advertise
24/36 kHz RX bandwidth without reducing channel count. They also distinguish
13-channel 32 MHz coverage from the reduced channel count in 64 MHz operation.
80–10m fits 32 MHz coverage. Available capacity must still come from this unit's
configuration, reservations and other users; advertised capacity is not a lease.

At a confirmed 13 usable channels and 36 kHz rate:

- FT8 + WSPR: 10 background captures + 2 interactive listeners = 12; one spare.
- Adding all listed FT4 slices: 12 background captures + 1 listener = 13; no spare.
- At 24 kHz the base profile uses 12; at 12 kHz it uses 13. Reduce selected bands
  or modes to preserve listening capacity; never quietly steal an active capture.

Background captures request SND only, with no waterfall. Interactive listening
uses its own paired SND/W/F session and can tune freely. A software listener
inside an existing capture may eventually avoid a new lease; outside its coverage
it needs a separate lease. Waterfall suppression saves traffic/work, not another
receiver channel. Decoder audio is independent of speaker volume and mute.

## Implementation and qualification

`Web888IqCapture` owns one sound-only independent session and emits separate
12 kHz mono outputs from `Web888IqSplitter`. The local implementation validates
the negotiated rate, bounds frames/slice count, separates positive and negative
RF offsets, and resets on gaps or changes. It does not change administrator
settings to obtain 36 kHz.

`Web888CapturePool` now applies plans transactionally, retaining existing windows
until all new captures produce valid IQ. It counts pending leases and reserved
listening slots, and can refuse replacement if temporary overlap exceeds budget.
It does not reserve channels against other receiver clients or alter server settings.

Channelization now uses a bounded worker shared by the pool, with overflow gaps
reported to consumers. UI controls, internal FT8/WSPR decoder adapters and
WSJT-X audio-device routing are subsequent work. WSJT-X UDP control/spot messages
do not carry this PCM; external instances need their own audio input mapping.
The current desktop connector still opens the existing interactive receiver.

A bounded period-assembly primitive is now tested with explicit synthetic clock
anchors. It does not establish live UTC timing or invoke a decoder; see
[period validation](web888-decoder-period-validation.md).
The routing primitive reserves two periods per slice; all 25 outputs require
120.96 MB of float audio storage before IQ queues or decoder workspaces. See
[routing bounds](web888-decoder-router-validation.md).

Before hardware acceptance: confirm IQ handedness and effective passband at each
advertised rate; measure gain/AGC effects and sample timing; verify known FT8/WSPR
recordings through actual decoders; qualify CPU and bounded worker delivery under
the chosen multi-band load; verify competing clients, busy refusal and independent
listening. GPS header bytes alone do not establish trustworthy UTC sample timing.
The raw ten-byte timing header is preserved for diagnostics; its week/date and
quality remain unqualified. See [timing findings](web888-timing-qualification.md).
