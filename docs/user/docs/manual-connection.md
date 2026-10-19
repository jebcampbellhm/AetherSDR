---
title: "Manual Connection"
slug: "/manual-connection"
description: "Connect to a radio by its IP address when automatic discovery cannot reach it, such as across a VPN or a routed network."
---

Connect to a radio by its IP address when automatic discovery cannot reach it, such as across a VPN or a routed network.

Use **Connect by IP** for:

- A radio on a different VLAN or subnet
- A radio behind a VPN, including a FLEX-8000/Aurora on your tailnet (see [Tailscale Remote Access](./tailscale-remote-access.md))
- Remote access via a public IP with port forwarding (without SmartLink)
- A networked Icom, which is always reached this way (see [Networked Icom](./networked-icom.md))
- A standalone Web-888, including one on your local LAN (see [KiwiSDR and Web-888](./kiwisdr-and-web-888.md))

## Setup

<img src="/img/screens/connect-by-ip-card.png" width="760" alt="Connect to Radio window with the Connect by IP card selected. The Radio IP address group has Radio type: FlexRadio, Radio IP: 192.0.2.25, Network Diagnostics and Connect by IP buttons, and an Advanced: choose the VPN source path expander. Below are Connection options for slower links with a Use low bandwidth mode checkbox, and the shared checkboxes for the adaptive frame-rate throttle, connecting to the last radio, waking an Icom and showing the demo simulator." />

*The Connect by IP card, with a documentation address typed in.*

1. Open the **Connect to Radio** window: **File → Connect to Radio...**, or the **+** button beside the title bar's radio tabs → **Connect manually…**.
2. Choose the **Connect by IP** card.

The **Radio IP address** group starts with two rows:

| Row | What to enter |
|-----|---------------|
| **Radio type:** | The radio family to connect: **FlexRadio**, **Hermes-Lite 2**, **ANAN-G2**, **Icom (network)**, **Web-888** or **RTL-SDR (USB)**. Only this family is contacted. |
| **Radio IP:** | The IP address or host name, for example `10.0.0.25`. |

**RTL-SDR (USB)** appears only in builds that include the RTL-SDR backend. Choosing **Icom (network)** adds its connection rows. **ANAN-G2** shows a pointer to **Settings → Radio Setup... → Radio → ANAN Front End**, where its receive options are configured before connecting; see [Networked Icom](./networked-icom.md) and [ANAN-G2](./anan-g2.md). For every family's status, see [Supported Radios](./supported-radios.md).

## Using Connect by IP

### Connecting to a Web-888

Choose **Web-888**, enter its IP address and HTTP port (usually **8073**),
then enter its user password if needed. Leave the password blank to use the
saved password or connect without one. Click **Connect by IP**, or press Enter
in the password field. The receiver opens directly with one receive slice
and one spectrum/waterfall; no Demo session is needed. Passwords are saved
only in the operating system keychain after an accepted connection.

Leave **Source path** on **Auto**: Web-888 follows the system's network route.
**Use low bandwidth mode** is unavailable for this receiver transport.
Use a numeric IPv4 or IPv6 address in this form; enter the port separately.
The receive tuning range is **1 Hz–30.72 MHz**.
Speaker playback starts with about **360 ms** of buffered audio to smooth
bursty network delivery. This adds a short delay when starting or recovering
an exhausted stream. Audio resumes when the reserve refills.

### Connecting to a FlexRadio

1. Set **Radio type:** to **FlexRadio**.
2. Enter the radio's IP address in **Radio IP:**.
3. Click **Connect by IP**. AetherSDR probes the radio on TCP 4992.
4. If the probe fails, the panel shows the reason. **Network Diagnostics**, beside the button, helps you check the path.

If your VPN creates more than one active network adapter, open **Advanced: choose the VPN source path** and pick the **Source path:**. Most users can leave this on Auto.

### Port forwarding (FlexRadio remote access)

For FlexRadio remote access without SmartLink, forward these ports on your router:
- **TCP 4992** → radio's LAN IP (command channel)
- **UDP 4991** → radio's LAN IP (VITA-49 audio/spectrum)

Then enter your public IP in **Radio IP:**.

For a networked Icom behind NAT, see the **Icom ports:** row on [Networked Icom](./networked-icom.md).

### Slower links

**Use low bandwidth mode** and **Enable adaptive frame-rate throttle** are in the same window, under "Connection options for slower links". See [Low Bandwidth Connections](./low-bandwidth-connections.md).

### Auto-reconnect

If a FlexRadio connection drops unexpectedly, AetherSDR retries every five seconds. During a radio reboot, **Connection refused** can appear until the radio's command service is ready. The retries continue in the same running app. If an explicitly selected source interface is unavailable, AetherSDR keeps retrying that source instead of switching interfaces. Disconnecting deliberately stops these retries.

Connect by IP remembers the radio type you chose and the addresses you have used. With **Connect to last radio on start up** checked, AetherSDR reconnects to the last radio on the next launch. If startup auto-connect gives up, the Connect to Radio window opens again.

## Reference

### Connect by IP compared with SmartLink

This comparison applies to FlexRadio. Other radio families do not use SmartLink.

| Feature | Connect by IP | SmartLink |
|---------|---------------|-----------|
| Authentication | None | FlexRadio account |
| Port forwarding | TCP 4992 + UDP 4991 | TCP 4994 + UDP 4993 |
| TLS encryption | No | Yes |
| NAT traversal | No (needs a static IP or port forward) | Built-in |
| FlexRadio account | Not required | Required (SmartSDR+) |

See [SmartLink Setup](./smartlink-setup.md).

## Troubleshooting

### A Web-888 connection fails

Check the receiver's IP address and HTTP port, then check its user password.
An admin password is not needed. If a saved password is outdated, type the
current user password into the form and reconnect. Keep **Source path** on
**Auto**. The FlexRadio ports and MTU advice below do not apply to this
WebSocket receive stream.

To remove a saved password, enter the receiver's address and port, then click
**Forget** beside the password field. This clears that endpoint's saved and
session password without disconnecting an active receiver. Leave the field
blank on your next connection if the receiver no longer requires a password.

### The FlexRadio probe fails

AetherSDR could not reach the radio on TCP 4992. The panel shows the reason.

1. Check that **Radio type:** matches the radio; only that family is probed.
2. Click **Network Diagnostics** beside the button to check the path.
3. For access from outside your network, check that TCP 4992 and UDP 4991 are forwarded to the radio's LAN IP.

### A FlexRadio connection fails over a VPN with several network adapters

AetherSDR may be using the wrong adapter to reach the radio.

1. Open **Advanced: choose the VPN source path**.
2. Pick the VPN's adapter in **Source path:** and connect again.

### FlexRadio audio works over a VPN, but the spectrum and waterfall stay empty

Spectrum and waterfall packets are larger than the tunnel allows, so they are dropped while the smaller audio packets get through.

1. Lower **Network MTU**; see [Low Bandwidth Connections](./low-bandwidth-connections.md).

## See also

- [Tailscale Remote Access](./tailscale-remote-access.md)
- [SmartLink Setup](./smartlink-setup.md)
- [Low Bandwidth Connections](./low-bandwidth-connections.md)
- [First Connection](./first-connection.md)
- [Supported Radios](./supported-radios.md)
