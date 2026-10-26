---
title: "SDRplay nRSP-ST"
slug: "/nrsp-st"
description: "Receive IQ Lite audio and waterfall directly from an SDRplay nRSP-ST."
status: "Experimental, receive-only"
applies_to: ["SDRplay nRSP-ST"]
platforms: ["Windows", "Linux", "macOS"]
---

:::info[Status]

**Status:** Experimental, receive-only · **Applies to:** SDRplay nRSP-ST · **Platforms:** Windows, Linux, macOS

:::

AetherSDR connects directly to the nRSP-ST for a waterfall and one receive
slice. SDRconnect and its WebSocket exporter are not required. The radio
sends 48 ksample/s IQ Lite; AetherSDR demodulates and filters it locally.

This is an independent implementation of the observed radio protocol.
Firmware 1.0.15 is the development target. Windows is the current native
validation platform; Linux and macOS builds still need validation.

## Requirements

- An nRSP-ST reachable from this computer on TCP port **50000**.
- An antenna connected to the appropriate radio input.
- A streaming account if **Require authentication** is enabled on the radio.
- Source control ownership to change shared tuning or antenna selection.

The first eligible client normally owns source control. An account without
**Grant Control**, or a session connected while another client owns control,
can still receive. Use **Give Control** in NRSPAdministrator to transfer
ownership to an eligible session.

## Setup

1. Open **File → Connect to Radio...** and select **Connect by IP**.
2. Set **Radio type:** to **SDRplay nRSP-ST**.
3. Enter the radio's address in **Radio IP:**.
4. If authentication is required, enter the streaming **Username:** and
   **Password:** configured in NRSPAdministrator.
5. Click **Connect by IP**, then select the receive mode and passband.

The password is cleared from the field when submitted. After a successful
connection it is saved in the operating-system keychain, scoped to the receiver
address, port and streaming username. Enter the same username and leave the
password blank to reuse it. **Forget** removes the saved password for that
receiver and username. A rejected replacement does not overwrite the saved
password. Without keychain support, credentials remain session-only.

The connection adopts the radio's current source frequency, sample rate,
and antenna. It does not restore a saved source configuration over the radio.
Manual connection is currently required; automatic discovery is not included.

## Using the receiver

**Tune the slice** with the VFO or spectrum. Inside the current source window,
slice tuning does not move the shared source. Outside that window, the client
holding source control retunes the source and reads its resulting state back
through a brief second connection. Shared changes can momentarily pause the
display and audio while that readback completes.
A client without source control can tune only inside the existing capture
window. Audio, waterfall, demodulation, and local receive filters remain usable.
An attempt to change shared settings leaves the observed radio state intact.
If source control is transferred during reception, AetherSDR reports the
change and updates the available controls without disconnecting.

**Select an antenna** using the slice's RX antenna selector. Inputs are **A**,
**B**, and **C**. Tune the source to 200 MHz or below before selecting C. A
tune above 200 MHz while using C selects A first, then retunes. When this
session lacks source control, only the current antenna is offered.

**Adjust the receive filter** with the normal passband controls. USB, LSB,
AM, SAM, FM, CW, DIGU, and DIGL use AetherSDR's local DSP. The hardware band
filter remains automatic. FM here means narrowband FM; broadcast WFM is not
included in this integration.

**Zoom the waterfall** within the current source window. The view crops the
radio's full-source display and initially shows its central 80% to avoid edge
roll-off (1.6 MHz of a 2 MS/s source). You can zoom out to see the full source
span. Its available detail is limited by that display's
bin spacing; zooming does not create additional FFT resolution. Display levels
are relative and must not be treated as calibrated antenna-input dBm.
Software automatic black level starts from the first received waterfall row.
Use **WtrFall Gain** and **Black Level** in the Display panel to adjust contrast.

AF gain, balance, and mute affect the speaker monitor. Per-slice audio exports
receive the audio before those monitor controls.

## Reference

| Function | Current behavior |
|---|---|
| Receiver/slice count | One |
| Source sample rate | Adopted from the radio; not changed by connecting |
| IQ Lite rate | 48 ksample/s |
| Audio rate | 24 ksample/s stereo |
| Waterfall | Radio-generated display; initially the central 80%, with full span available |
| Antenna | A/B/C; shared source owner controls selection |
| Reconnect | Operator initiated |
| Transmit, firmware update, network administration | Unavailable |

## Troubleshooting

- **Connection fails:** check the address, radio power, and TCP port 50000.
  A server signature failure ends the connection; AetherSDR does not bypass it.
- **Login rejected:** re-enter the streaming username and password. An
  accepted account without **Grant Control** can receive; lack of control is
  separate from a rejected login.
- **Tuning outside the captured source fails:** another client may own source
  control, or the account may lack **Grant Control**. Tune inside the current
  window or have an administrator grant control. The refusal notice shows the
  receiver's current allowed frequency range.
- **Antenna C is refused:** first tune the shared source below 200 MHz and
  confirm that this client owns source control.
- **The session closes or IQ stops:** reconnect manually. An administrator
  disconnect closes the same connection as a network interruption, so
  AetherSDR does not retry automatically.
- **Another client changes the source sample rate:** reconnect to adopt the new
  IQ timing. AetherSDR closes the old stream rather than playing it at an
  assumed rate.
- **Narrow zoom looks coarse:** it is a crop of the available radio display;
  widen the view to see the source spectrum at its native detail.

## See also

- [Manual Connection](./manual-connection.md)
- [Supported Radios](./supported-radios.md)
- [Audio Settings](./audio-settings.md)
