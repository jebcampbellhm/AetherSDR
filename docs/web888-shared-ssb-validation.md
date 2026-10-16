# Shared interactive SSB validation — 2026-10-16

## Implementation

Two software SSB listeners can share one existing 24 kHz RX/WF lease. Two
panadapters therefore support up to four listeners while retaining two reserved
hardware slots. The ordinary createSlice/removeSlice hooks drive the existing
Add Slice UI. Each listener owns a DSP session, filter, envelope AGC and PCM
resampler; its controls never retune a shared capture. USB and signed-passband
LSB are supported. Closing the child restores normal primary PCM and tuning.
The receive-only implementation does not modify persistent receiver settings.

## Socket-free evidence

Windows MSVC/Qt 6.12 build of AetherSDR and affected tests succeeded.
The rebuilt Web-888 regression selection plus kiwi_sdr_session_test passed
17/17 tests in 79.51 seconds before the final observer-reentrancy hardening.

The new shared-SSB test exercises:
- Two complex RF tones with a 10:1 amplitude difference in one 24 kHz capture.
  USB output desired tone amplitude 0.0500004; measured other test frequency
  0.000021335. LSB desired tone 0.499969; other test frequency 0.000153298.
- Independent AGCs leveling the two outputs, off-mode unity gain, and distinct
  fast/slow recovery. These are synthetic DSP checks, not RF performance claims.
- Unknown/12 kHz rate refusal, out-of-capture refusal, independent mode/filter/
  AGC control, exactly one client for two listeners, two clients for four
  listeners across two pans, removal and stale-output rejection at disconnect.
- Observer-triggered recursive creation and disconnect during lifecycle changes.

## Live receive-only evidence

The receiver at the user's supplied private endpoint had users=0 before testing.
No RF antenna qualification is assumed. The manual web888_live_probe --shared-ssb
mode was used; credentials were supplied only through a process environment
variable, consumed/unset by the probe, and are not present in this evidence.

First 25-second run:
- shared_ssb=1; primary_frames=542206; secondary_frames=541692
- waterfall_rows=564; waterfall_span_hz=480000; failed=0; process exit 0
- Two independently tuned/filtered software audio feeds; one RX/WF lease.

Second 20-second run added return-to-PCM validation four seconds before stopping:
- shared_ssb=1; primary_frames=423949; secondary_frames=326140
- waterfall_rows=444; waterfall_span_hz=480000; failed=0; process exit 0
- The primary kept delivering after child removal; the retired child's sample
  count did not advance. The probe checks both conditions before reporting success.

The waterfall requested 350 kHz centered at 14.175 MHz; the firmware supplied a
480 kHz discrete zoom span. This is independent of the nominal 24 kHz IQ capture
(approximately 22 kHz usable after the splitter's edge guards).
Subsequent status returned users=0, users_max=13 and adc_ov=0.

## Limits

No intelligible on-air SSB, calibrated per-slice meter, decoder success, sample
UTC, GPS qualification or cross-platform runtime claim follows from these tests.
The Windows desktop was subsequently verified with the supported software renderer
(see UI follow-up below); GPU and other platform rendering remain unverified.
Only the active slice feeds speakers; simultaneous mixing is not implemented.
The original listener owns the lease; ordinary removeSlice closes the added
software listener, while closing the pan retires the entire lease.

Engine boundary, capability-record, command-plane and test-registration checks
passed with existing tracked warnings. The touchpoint manifest was regenerated
with Python UTF-8 mode and verified. git diff --check passed.

Final observer-lifecycle hardening was rebuilt into the desktop executable;
web888_backend_test and web888_shared_ssb_test then both passed (0.90 seconds).
This includes recursive-create refusal and no removal signals after an
observer-triggered disconnect. Final manifest and whitespace checks also passed.


## Multi-slice UI follow-up (2026-10-16)

Used an isolated automation/settings profile, TX disabled, the private receiver
at its unchanged 24 kHz setting, and the supported Windows software renderer.
Qt offscreen rendering was insufficient; those initial images are not rendering
validation. The native software-rendered screenshots show both live waterfalls.

Found and fixed two functional UI defects:
- Rejected shared-capture tuning left the optimistic desktop frequency outside
  the actual capture. Frequency, mode and filter refusals now republish the
  accepted DSP state; observer-triggered disconnect is generation guarded.
- The backend implemented setActiveSlice but omitted the neutral typed
  ActiveSlice request hook. Desktop selection could leave both models active,
  with subsequent controls targeting the original listener. The hook now
  forwards valid selection and clears the prior active slice.

Socket-free coverage now drives SliceModel tune, filter, mode and selection
intents into the backend and checks the resulting accepted model state.
Rebuilt AetherSDR, web888_backend_test and web888_shared_ssb_test; both tests
passed (0.77 seconds). Engine-boundary, command-plane, test-registration and
capability-record checks passed with the existing baselines.

Live desktop assertions passed for four listeners on two independent pans:
- First capture: 14.200 MHz USB, 300..2800 Hz, medium AGC; child 14.207 MHz
  LSB, -2400..-200 Hz, fast AGC. A request for 14.230 MHz restored 14.207 MHz.
- Second capture: 7.150 MHz USB with medium AGC; child 7.157 MHz USB,
  400..2600 Hz, slow AGC. Exactly this child was active after selection.
- Removing the second child and closing its pan preserved the first two
  listeners and their frequencies. Recreating the second pan assigned a fresh
  slice ID (4), without replacing either surviving listener.

Inspected screenshots in the ignored build-web888-evidence directory:
shared-ui-settled-pan0.png and shared-ui-settled-pan1.png. Both show incoming
waterfall data and slice controls. Closely spaced floating slice controls can
occlude each other; this check establishes selection and state routing, not a
new layout policy. Shared-slice meters remain uncalibrated.

Disconnected and closed only the isolated test instance; receiver status then
reported users=0, users_max=13, adc_ov=0. No persistent receiver setting changed.
No RF intelligibility, GPS timing or decoder qualification is claimed.


## Digital monitoring coexistence — 2026-10-16

The isolated Windows desktop ran the default 40/30/20/10m FT8+WSPR profile
alongside two waterfalls and four shared SSB listeners. Preview showed seven
captures, two reserved listeners, eight digital outputs; live receiver occupancy
was nine of thirteen slots (adc_ov=0). Status showed seven active, zero pending.
Independent SSB mode/filter/AGC/tune changes succeeded. Monitoring samples grew
from 7,964,160 to 8,148,736 through closing the second listening pan, and to
8,358,144 after recreating that pan and its software child. No receiver/admin
settings or reporting options were changed.

The first automation attempt to click the generic Stop label resolved a hidden
unrelated button and was refused. It did not stop monitoring; no stop claim is
based on that UI action. Disconnect released that test session. The engine stop
path was then explicitly exercised in the combined probe below.

Added opt-in web888_live_probe --combined (minimum 20 seconds), using the existing
private receiver, seven monitoring captures and two paired listening captures.
It checks each of four PCM outputs, both waterfall outputs, and every one of the
eight digital audio outputs, plus recent delivery. Halfway through it retunes
and changes local SSB controls. Four seconds before finish it stops monitoring,
checks digital delivery stops, and requires all four interactive PCM counts to
continue advancing. It disconnects on its bounded final timer. It also confirms
no timed decoder job is produced without an explicitly qualified UTC anchor.

A 40-second run exited 0, combined_failed=0:
- Listener frame counts (IDs 0,1,2,3): 855550, 855550, 855036, 855036.
- Waterfall rows (pans 0,1): 809, 807.
- Digital sample counts: IDs 10,11,20,21,30,31,70 each 380416; ID 71 380672.
- Seven active captures before monitoring stop, zero pending; zero captures
  after stop and disconnect. Four listener streams advanced after monitoring stop.

No production coexistence defect was found. This is bounded transport/DSP routing
validation, not long-duration load, RF decoding, GPS or UTC qualification. Digital
monitoring still supplies audio only; it does not automatically run timed decoders.

Six targeted socket-free regressions passed: backend, shared SSB, capture pool,
decoder router, decoder period and decoder pipeline (24.22 + 38.08 seconds).
Final receiver status: users=0, users_max=13, adc_ov=0; persistent rate untouched
at 24 kHz. The combined probe rebuilt successfully; whitespace checks passed.
