# SolidClip

![SolidClip](docs/screenshot.png)

A transparent, **linked** soft clipper / clipper-limiter hybrid (VST3 + Standalone, AU on macOS), built with JUCE 8 and CMake.

SolidClip holds peaks at a solid, consistent ceiling while adding as little audible distortion as possible. In particular, it adds none to the side channel (L − R) when you solo it. It never shapes the channels separately. Instead it computes **one gain** and applies it to both channels, so the ratio between them, and with it the stereo image, is never changed.

The GUI uses the same look and feel as *Virus Delay*: brushed metal, grey header panels, chrome and blue knobs, blue LCDs. It is freely resizable: drag the bottom-right corner, and everything scales together and stays sharp.

---

## Building

You need CMake ≥ 3.22, a C++17 compiler and Git. JUCE 8.0.4 is downloaded automatically with FetchContent. To use a local JUCE checkout instead, pass `-DJUCE_DIR=/path/to/JUCE`.

### macOS (Xcode or Ninja)

```bash
cmake -B build -G Xcode                    # or: -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Output: `build/SolidClip_artefacts/Release/VST3/SolidClip.vst3`, `.../AU/SolidClip.component`, `.../Standalone/SolidClip.app`.
To build a universal binary, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.

### Windows (Visual Studio 2022)

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Output: `build\SolidClip_artefacts\Release\VST3\SolidClip.vst3` and `...\Standalone\SolidClip.exe`.
Copy the `.vst3` folder to `C:\Program Files\Common Files\VST3`, or configure with `-DSOLIDCLIP_COPY_AFTER_BUILD=ON`.

### Linux

```bash
sudo apt install libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev \
                 libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build --output-on-failure
```

### CMake options

| Option | Default | |
|---|---|---|
| `SOLIDCLIP_BUILD_STANDALONE` | ON | Standalone app for quick testing |
| `SOLIDCLIP_BUILD_AU` | ON | Audio Unit (macOS only) |
| `SOLIDCLIP_BUILD_TESTS` | ON | `SolidClipTests` console app, registered with CTest |
| `SOLIDCLIP_COPY_AFTER_BUILD` | OFF | Install into the user plugin folder |
| `JUCE_DIR` | empty | Use a local JUCE checkout instead of downloading it |

---

## Parameters

| Parameter | Range | Default | What it does |
|---|---|---|---|
| **Input** | −12 … +24 dB | 0 dB | Drive into the clipper. |
| **Ceiling** | −12 … 0 dBFS | −0.3 | Maximum output level. The Stereo stage clips to it, and the True-Peak Guard holds the L/R output at it in every mode. |
| **Knee** | 0 … 12 dB | 3 dB | The soft knee starts at *ceiling − knee*. Below that the signal is **untouched**: gain is exactly 1.0. 0 dB = hard knee. |
| **Shape** | 0 … 100 % | 30 % | 0 % = instantaneous gain (pure clipper). Higher values smooth the gain envelope over up to 3 ms, with matching lookahead (clipper/limiter hybrid). |
| **Oversampling** | 1x / 2x / 4x / 8x / 16x | 4x | Rate at which detection and gain are computed, so inter-sample peaks are caught. |
| **Filter** | Min Phase IIR / Linear Phase FIR | IIR | IIR = low latency. FIR = linear phase, and the output is **bit-identical** to the input below the knee. |
| **Mode** | Stereo / Mid-Side / Stereo + M/S | Stereo | See below. |
| **Stereo Stage**, **M/S Stage** | on/off | on | Engage each stage in *Stereo + M/S* mode. |
| **Mid** / **Side** (enable) | on/off | on | Off = that channel passes untouched (gain 1.0) and stops contributing to the shared detector. |
| **Mid Ceiling / Knee** | −24 … 0 dB / 0 … 12 dB | −0.3 / 3 | Curve for M = (L + R) / 2. |
| **Side Ceiling / Knee** | −24 … 0 dB / 0 … 12 dB | −6 / 3 | Curve for S = (L − R) / 2. |
| **M/S Link** | 0 … 100 % | 100 % | 100 % = one shared gain for Mid and Side, so the S/M ratio is preserved. 0 % = independent gains. Clipping Side on its own is what causes audible side-channel distortion, so keep this high. |
| **Output** | −24 … +12 dB | 0 dB | Gain after clipping. Raising it moves the output above the ceiling. With **Link** on, it is a trim on top of the automatic compensation. |
| **Link** (In/Out) | on/off | on | Links Input and Output: the output automatically undoes the input gain, so driving harder clips more but **doesn't change the level**. Bypass and Delta stay level-matched. Turn it off to use Input as plain drive. |
| **Delta** | on/off | off | Outputs *dry − wet*, latency aligned: exactly what is being removed. |
| **TP Guard** | on/off | on | True-peak guard (see below). |
| **Bypass** | on/off | off | Latency-compensated bypass. This is also the host's bypass parameter. |

All continuous parameters are smoothed, and stage, mode, delta and bypass changes crossfade over about 10 ms. Every knob supports drag, shift-drag (fine), the scroll wheel and double-click to reset, and shows its value on hover.

## Modes

- **Stereo**: one gain from `max(|L|, |R|)` is applied to both channels. Feed it L = R and the side output is exactly zero. Feed it any stereo signal and the L/R ratio of every sample is unchanged.
- **Mid/Side**: the signal is encoded to M/S, clipped, and decoded (L = M + S, R = M − S). Mid and Side each have their own enable, ceiling and knee. With **Link** at 100 %, a single gain (from the tighter of the two curves) goes to both, preserving the S/M ratio. In between, the gains are blended.
- **Stereo + M/S (serial)**: the Stereo stage (global Ceiling/Knee) runs first, then the M/S stage (its own settings). Each has its own stage button.

Every mode runs the same signal path with the same latency. A stage that isn't used just applies gain 1.0, so switching modes never shifts timing and is click-free.

---

## How it works

```
input gain ─► up-sample ─► Stereo stage ─► M/S stage ─► down-sample ─► TP guard ─► delta / output gain ─► bypass mix
```

**Gain-based clipping.** Each stage builds a detector (e.g. `max(|L|,|R|)`) and runs it through the soft-clip curve. The curve is linear up to *T = ceiling − knee*. Above that it is `T + K·tanh((d − T)/K)`, which bends only the overshoot and approaches the ceiling asymptotically. Gain = target / detector, exactly 1.0 below *T*. Because the curve acts on a magnitude and the gain multiplies the signed signal, it is odd-symmetric: no DC and no even harmonics.

**Shape (no-overshoot smoothing).** The required gain goes through a lookahead **min-hold**, then **two cascaded moving averages** whose combined length equals the hold window. Every averaged value then comes only from holds that cover the sample it is applied to, so the smoothed gain can never be above that sample's required gain, and peaks never pass the ceiling. The cascaded averages give an S-shaped attack and a fast, smooth release (≤ 3 ms), with no pumping. The averages run on fixed-point integer sums, so there is no floating-point drift, and the gain returns to exactly 1.0.

**Bit-transparency (Linear Phase FIR, and 1x).** Only the *removed* part of the signal (clipped − unclipped) goes through the oversampler. The dry signal is just delayed by exactly the reported latency. When nothing is clipped, the removed part is exactly zero, so the output nulls bit-for-bit against the latency-aligned input. Min-phase IIR filters shift the phase of the whole band, so that path can't be bit-exact by design. In IIR mode the full signal is oversampled, and the result is transparent in level but phase-shifted at high frequencies.

**True-Peak Guard.** Clipping at the oversampled rate creates harmonics above Nyquist, and the down-sampling filter removes them. That leaves small overshoots (up to about 2 dB on brutal material). The guard looks ahead at the down-sampled signal's 4x true peak, using the **ITU-R BS.1770-4** interpolator. It applies a short, smooth, linked dip (about 0.7 ms) only where the ceiling would be exceeded. Elsewhere its gain is exactly 1.0. The guard is linked across L/R, so it never changes the stereo image.

**Latency.** Latency = oversampling filters (padded to whole samples) + 2 × 3 ms lookahead (Stereo and M/S stage, only while Shape > 0) + the guard's ~1 ms. At 44.1 kHz / 4x / IIR that is 311 samples (7.1 ms). With Shape = 0 it is 47 samples. The value is reported with `setLatencySamples` and updated whenever oversampling, filter or lookahead changes. Those changes do a quick fade-out → switch → fade-in so they don't click.

**Real-time safety.** Everything, including all 10 oversampler variants up to 16x, is allocated in `prepareToPlay`. `processBlock` has no allocations, locks or logging, handles any block size (processed internally in chunks of 256 or fewer), and uses `ScopedNoDenormals`. Meter values reach the UI through atomics.

## Source layout

```
Source/
  PluginProcessor.*      APVTS, bus layout (stereo only), latency, state
  PluginEditor.*         resizable Virus-style editor (fixed design size, scaled by a transform)
  CustomLookAndFeel.*    procedurally drawn knobs, panels, LCDs, buttons (from Virus Delay)
  Parameters.h           parameter IDs, layout, settings snapshot
  UI/SolidComponents.*   knob, LCD, bar meters (peak hold), help overlay
  DSP/
    SoftClipCurve.h      linear + tanh soft-knee curve as a gain
    GainSmoother.h       min-hold + cascaded box smoothing with a no-overshoot guarantee
    Lookahead.h          integer delay line
    LinkedClipper.h      Stereo stage
    MidSideCodec.h       M/S encode / decode
    MidSideClipper.h     Mid/Side stage with per-channel enable and Link
    TruePeakGuard.h      BS.1770 true-peak detector + guard
    ClipEngine.*         complete signal path, oversampling, crossfades, latency
Tests/SolidClipTests.cpp
```

## Tests

`SolidClipTests` (run by `ctest`) drives the real processor with irregular block sizes and checks the following (604 checks):

- **Bit-exact null** below *ceiling − knee*, latency aligned. Covers all modes, Shape 0 / 50 %, 1x, and Linear Phase at 2x–16x.
- **Ceiling holds**: sample peak ≤ ceiling, and BS.1770 4x true peak ≤ ceiling + 0.01 dB. Signals are sine sweeps, white noise and drum hits at +12 dB, across all oversampling factors and filters (measured worst case: +0.002 dB).
- **Linking**: L = R gives a side output of exactly zero at every factor and filter, and the L/R ratio is unchanged on every sample.
- **Mid/Side at 100 % link**: the S/M ratio is preserved.
- **Latency**: reported = measured for every oversampling × filter × lookahead combination, at 44.1 and 96 kHz.
- **In/Out Link**: below the knee, the level is unchanged for any input gain. Driven, the output peaks at ceiling − drive.
- **Mode switching**: no latency change and no clicks.
- **Unit tests** of the gain smoother (never above the required gain, returns to exactly 1.0) and of the stages at the oversampled rate.

It also prints a CPU benchmark. `SolidClipTests --snapshot ui.png [width] [mode]` renders the editor to a PNG.
