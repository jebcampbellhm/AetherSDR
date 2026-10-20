# Web-888 S-meter calibration with TinySA Ultra

Prepared for Jeb Campbell, W4JEC, for the evening of October 20, 2026.

Start with an unmodulated **14.200 MHz carrier at -73 dBm at the Web-888
antenna input**. This is a practical 20 m reference for our HF listening setup,
not a manufacturer-required calibration frequency. Verify other bands afterward:
one frequency cannot establish accuracy across 80–10 m.

## Equipment and initial settings

- TinySA **Ultra**, charged, using its signal-generator output.
- Short 50-ohm coax and adapters to the Web-888 HF antenna input.
- Optional characterized 20 dB attenuator; useful for checking weaker levels.
- Disconnect the receiving antenna for this conducted test. Connect only the
  generator to the receiver, not to an antenna or transmitter.
- Keep Web-888 at the selected **12 kHz** rate.
- Record the original S-meter calibration, waterfall calibration, RF attenuation,
  ADC PGA and dithering settings before changing anything.
- For the baseline, use 0 dB RF attenuation and ADC PGA off; record this exact
  configuration. Later test the gain settings normally used for reception.
  Leave clock/GPS and dithering settings unchanged.

## TinySA Ultra setup

Select **MODE → Signal Generator**. Use the generator's RF output connector,
not the separate CAL reference output. Set:

| Setting | Value |
|---|---|
| Frequency | 14.200 MHz |
| Level, direct connection | -73 dBm, allowing for cable loss |
| Modulation | Off |
| Frequency/level sweep | Off |
| External gain compensation | 0 dB for the direct connection |

The Ultra documentation lists sine-wave generation at HF and output levels
from -123 to -18 dBm. Its frequency resolution is approximately 44 Hz, so
locate the actual carrier rather than assuming the displayed frequency is exact.
[TinySA Ultra signal generator documentation](https://tinysa.org/wiki/pmwiki.php?n=TinySA4.SignalGenerator)

If using a 20 dB external pad with external-gain compensation at zero, set
the generator to **-53 dBm** for nominal **-73 dBm** at the receiver.
The accounting is:

`receiver input dBm = generator output dBm - pad loss dB - cable loss dB`

The Ultra can instead account for an external attenuator through its External
Gain setting. Use either that feature or manual arithmetic, not both.
Generator level accuracy and pad/cable uncertainty limit the resulting
calibration; this is a useful field calibration, not a traceable laboratory one.
An analyzer self-calibration alone does not independently verify generator output.

## Measure and adjust

1. Turn on the carrier. In Aether select USB, initially tune to **14.199 MHz**,
   and use a roughly 300–2700 Hz audio passband. The carrier should be near
   1 kHz audio, comfortably inside the filter and away from zero frequency.
   Adjust tuning slightly if necessary. Use this wider filter initially so
   generator frequency error cannot place the carrier on a narrow filter edge.
2. Leave audio AGC enabled at a comfortable setting; lower speaker volume if
   desired. These are not calibration controls. Disable optional audio noise
   reduction for a simple listening check.
3. Allow the meter to settle and record Aether's **S-meter dBm reading**.
   Do not use the waterfall trace peak or its noise-floor value.
4. Compare with the Web-888 browser S-meter using the same frequency and filter
   if an RX slot is available. This comparison should agree approximately;
   it checks client interpretation, not absolute RF accuracy.
5. In Web-888 **Admin → Config → S-meter calibration**, calculate:

   `new offset = old offset + (known input dBm - displayed dBm)`

   Example: known input -73 dBm, displayed -63 dBm → subtract 10 dB from the
   existing offset. Do not replace the offset with -73.
6. Verify the result is approximately **-73 dBm / S9**.
7. Check levels -63 and -83 dBm at the receiver. Expect changes of +10 and -10 dB.
   Optionally check -93 dBm if sufficiently above receiver noise. A reading near
   the receiver noise floor cannot accurately validate a weak carrier.
8. Check approximately 3.6, 7.1, 10.12, 18.1, 21.2, 24.94 and 28.2 MHz at -73 dBm.
   Keep the carrier centered within the receive filter at each frequency.
   Record differences before deciding whether a single global offset is adequate.
9. With a fixed input, check the RF attenuation and ADC PGA settings normally
   used. Record how the displayed level changes; do not assume the firmware
   compensates for those controls. Restore the intended listening configuration
   and retain its calibration record.
10. Turn off the generator, disconnect its cable, and reconnect the antenna.

On HF, S9 corresponds to -73 dBm and each S-unit below S9 is 6 dB. Thus
-85 dBm is S7 when measured by a calibrated S-meter.
[IARU Region 1 S-meter recommendation](https://www.iaru-r1.org/wp-content/uploads/2021/03/VHF_Handbook_V9.01.pdf)

## Waterfall and reporting

Web-888 provides separate S-meter and waterfall calibration offsets.
Waterfall floor/ceiling adjust presentation; they do not calibrate RF power.
Do not force the waterfall noise floor to match the passband S-meter:
their measurement bandwidths differ.
[Web-888 configuration guide](https://www.rx-888.com/web/guide/admin/config.html)

Our inspected server source calculates the S-meter from filtered IQ before audio
AGC, then adds S_meter_cal (Web888/rx/rx_sound.cpp). Aether forwards the reported
level and explicitly marks hardware calibration unverified
(src/core/backends/web888/Web888Backend.cpp).

For CW QSO reporting, narrow the filter to isolate the station after calibration
and assess readability and tone as well as signal strength. For FT8/FT4, use
the decoder's SNR report instead of converting the S-meter.

## Record tonight

| Item | Measurement |
|---|---|
| TinySA Ultra firmware / output calibration history | |
| Cable and pad loss | |
| Receiver firmware / 12 kHz confirmed | |
| RF attenuation / ADC PGA / dithering | |
| Original S-meter offset | |
| 14.200 MHz, -73 dBm: original reading | |
| New offset / corrected reading | |
| -63 / -83 / -93 dBm checks | |
| Other-band checks | |
| RF attenuation / PGA effects | |
| Final restored listening configuration | |

Status: procedure prepared; no calibration measurements or receiver changes
performed as part of preparing these notes.
