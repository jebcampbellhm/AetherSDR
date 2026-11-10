---
title: "iPad (experimental)"
slug: "/ipad"
description: "Use the local development iPad build with touch controls and direct network radio connections."
status: "Experimental"
applies_to: ["Network radios", "Demo"]
platforms: ["iPadOS"]
---

:::info[Status]

**Status:** Experimental · **Applies to:** Network radios, Demo · **Platforms:** iPadOS

:::

The iPad target runs the radio engine on the iPad and connects directly to your
network radio. It is a local Xcode development build. A display client for a
hosted AetherSDR instance is not included.

## Requirements

- iPadOS 18 or later, and an app built and signed in Xcode.
- Local Network permission and a network route to the radio.
- Keep AetherSDR in the foreground. Background radio/audio operation is not supported.

## Setup

1. Run the development build from Xcode and allow **Local Network** access.
2. Tap **Radio → Connect by IP**.
3. Choose your radio type, enter its address, and connect.
4. To explore without hardware, select **AetherSDR Demo** under **On This Network**.

Automatic discovery on physical iPads requires Apple's multicast entitlement
in the signed app. Direct-IP connections do not require that discovery entitlement.
Hardware receive and transmit behavior still needs testing on the actual iPad
and radio pair.

## Using the touch controls

| Control | Action |
| --- | --- |
| Frequency display | Open a large numeric keypad; enter MHz and tap **Tune** |
| **RX** selector | Choose the receiver controlled by the tablet toolbar |
| Mode selector | Change the selected receiver's mode |
| **−** / **+** | Tune by the receiver's current tuning step |
| **Mute** | Mute the selected receiver |
| **Lock** | Lock frequency changes; unlock before tuning |
| **Radios** | Add independent receivers and select which radio you hear |
| **Receiver** | Adjust audio level, AGC threshold, and tuning step |
| **Noise reduction** | Select NR2, RN2, NR4, NNR, or MNR; open **NR4 settings** |
| **Copy Assist** | Show or hide the received-speech transcript |
| **Settings** | Open the existing Radio Setup pages |
| **All controls** | Show or hide the existing desktop applet panel |

Pinch the spectrum to zoom around the point between your fingers. The bottom
toolbar scrolls horizontally if its controls do not fit. The main tuning and
receiver sheets have large touch controls; advanced desktop dialogs still
vary in their suitability for touch.
NR2 may show an FFT optimization window the first time it is enabled. Let it
finish; the result is cached for subsequent use. In Demo mode, tuning-step
changes are not echoed by the simulated radio.

## Using speech recognition

Tap **Copy Assist**, open its settings gear, and select a Whisper model.
The model downloads on first use. Tiny and Base are useful starting points
for evaluating performance. Recognition runs locally; Metal acceleration is
available in builds that enable it. Model downloads need internet access.
The transcript starts with larger text on iPad. Drag its labeled resize bar
to adjust its height; swipe the control row horizontally to reach more options.

ONNX VAD, speaker labels, sherpa speech models, DeepFilterNet and NVIDIA BNR
are not included in this initial target. See [Copy Assist](./copy-assist.md)
for the shared speech controls and [DSP Noise Mitigation](./dsp-noise-mitigation.md)
for the algorithms.

## Troubleshooting

### The radio does not appear

1. Check **Settings → Privacy & Security → Local Network** on the iPad.
2. Tap **Radio → Connect by IP** and enter the radio address.
3. Verify that the iPad's Wi-Fi network or VPN can reach that address.

### Copy Assist falls behind

1. Select a smaller Whisper model in Copy Assist settings.
2. Check the transcription backlog shown below the transcript.
3. Compare performance on the physical iPad; Simulator speed is not representative.

## See also

- [First Connection](./first-connection.md)
- [Demo Mode](./demo-mode.md)
- [Copy Assist](./copy-assist.md)
- [DSP Noise Mitigation](./dsp-noise-mitigation.md)

## Web-888 and SDRplay nRSP-ST

Open **Radio → Connect by IP** and select **Web-888** or **SDRplay nRSP-ST**.
Both receivers connect directly from the iPad. The build includes the native
Web-888 IQ/audio path and nRSP-ST authentication, IQ Lite transport,
decompression and receive DSP. nRSP-ST signature checks use Apple's Security
framework. These receiver paths are receive-only.

The desktop external FT8/FT4/WSPR decoder executables cannot run on iPadOS.
Native in-process decoder integration remains separate work. Credentials are
session-only in this development build, which has no QtKeychain integration.

Copy Assist's toolbar scrolls horizontally with complete numeric values and
units. Its settings scroll vertically and have a **Done** button. Dialogs
recenter and fit the available screen when the iPad rotates.

Radio Setup places its scrollable category list above the settings in portrait,
and beside them in landscape. Forms retain readable field widths; narrow
multitasking windows can also scroll horizontally when needed.

## Multiple radios

Tap **Radios** in the bottom toolbar, then **Add receiver…** to connect up to
two additional receivers without disconnecting the primary radio. Each has its
own tuning controls and waterfall. Swipe a receiver's control row horizontally
when it does not fit in portrait. **Listen to** selects one radio for speaker
audio, or **Off**; **PC Audio** must also be enabled. **Done** closes the panel
while the receivers remain connected.

Additional receivers cannot transmit. Transmitting on the primary radio requires
explicitly selecting **Allow transmit on primary radio**; permission clears when
the primary disconnects. Selecting an audio source does not enable transmission.
