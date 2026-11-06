# SPEC: Acid Bassline Synthesizer (TB-303 Emulation)

> Audience: an autonomous AI coding agent. Read the whole document before writing code.
> Goal: a macOS **VST3 plugin** and **Standalone app** that sounds as close as possible to an original Roland TB-303 Bass Line, including its quirks.
> "TB-303" and "Roland" are trademarks. Never use them in the product name, UI, bundle IDs or plugin metadata. Working product name: **"Acid3"** (`com.acid3.synth`). Use them only in code comments and docs that describe what is being emulated.

---

## 1. Scope

### 1.1 In scope
- Monophonic synth voice that models the 303 analog signal path at circuit-behavior level: VCO → waveshaper → diode-ladder VCF → VCA, plus the envelope, accent and slide circuits.
- A built-in 16-step sequencer with note, octave, accent, slide and rest/tie per step, synced to the host or an internal clock.
- MIDI input that plays the voice directly. Velocity sets accent and overlapping notes set slide.
- VCO, VCF and VCA front-panel controls: Waveform, Tuning, Cutoff, Resonance, Env Mod, Decay, Accent, Volume.
- Plugin formats: **VST3** and **Standalone** on **macOS** (universal binary: arm64 + x86_64).
- An offline render CLI and an automated test suite. The agent cannot listen to audio, so it verifies the sound with measurements (see §9).

### 1.2 Out of scope (v1)
- Windows/Linux, AU, AAX, CLAP (keep the code portable so these can be added later).
- Devil Fish or other mods, built-in distortion, and effects. §11 reserves an optional, clearly separated "Mods" section for these. Do not build it in v1.
- Exact replica of the original's pattern-write workflow (pitch mode / time mode). A modern step grid is fine.

---

## 2. Tech stack and project layout

| Item | Choice |
|---|---|
| Language | C++20 |
| Framework | JUCE 8.x (git submodule or CMake `FetchContent`, pinned to a tag) |
| Build | CMake ≥ 3.24, Ninja or Xcode generator |
| Formats | `juce_add_plugin(... FORMATS VST3 Standalone)` |
| Min macOS | 12.0 |
| Architectures | `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` |
| Tests | Catch2 v3 |
| Analysis scripts | Python 3 + numpy/scipy (in `tools/`, used only for verification) |

```
/CMakeLists.txt
/SPEC.md
/README.md
/src/
  dsp/            # pure DSP, no JUCE dependency except juce::dsp math is allowed
    Oscillator.h/.cpp
    WaveShaper.h/.cpp
    DiodeLadder.h/.cpp
    Envelopes.h/.cpp      # MEG (filter env), VEG (amp env)
    AccentCircuit.h/.cpp
    SlideCircuit.h/.cpp
    Voice.h/.cpp          # wires everything, sample-accurate events
    Oversampler.h/.cpp
  seq/
    Pattern.h/.cpp
    Sequencer.h/.cpp
  plugin/
    PluginProcessor.h/.cpp
    PluginEditor.h/.cpp
    Parameters.h/.cpp
/tests/            # Catch2 unit + golden tests
/tools/
  render/          # CLI: render pattern/MIDI → WAV (links the dsp lib)
  analysis/        # python: spectra, envelopes, comparisons
/reference/        # reference WAVs of a real 303 (user-supplied, may be empty)
```

Build `src/dsp` and `src/seq` as a static library (`acid3_core`) with **no** JUCE GUI/plugin dependency. The plugin, the render CLI and the tests all link that library.

---

## 3. Signal flow (must match)

```
            ┌────────────── Slide RC ◄── step/MIDI pitch CV
            ▼
 Tuning ─► VCO (saw core) ─┬─► saw ────────────┐
                           └─► square shaper ──┴─► Waveform switch
                                                         │
                                                         ▼
 Cutoff, EnvMod, MEG, Accent sweep ──► Diode ladder VCF (4-pole, nonlinear, resonant)
                                                         │
                                                         ▼
                                VEG × (1 + accent) ──► VCA ──► output HPF/DC block ──► Volume ──► out
```

Every element runs at the oversampled rate (§8) except the control-rate smoothers, which may run per sample at the base rate.

---

## 4. DSP modules: required behavior

Numbers marked **[cal]** are starting values. Calibrate them against reference recordings (§9.3). Each one must be a named `constexpr` in one `Calibration.h` file so it can be tuned in one place.

### 4.1 VCO
- The core is a **sawtooth** from an integrator/capacitor reset. On the real unit the ramp is not perfectly linear: it bends slightly with an exponential (RC) curvature. Model this with a curvature parameter **[cal]**, default a mild exponential bend (≈ 5–10 % deviation from linear at the end of the ramp).
- The reset is fast but not instantaneous. Model a short reset slope (≈ 1–2 µs **[cal]**) in addition to band-limiting.
- **Band-limited:** PolyBLEP at minimum, or minBLEP (preferred), together with the oversampling. No audible aliasing at C5 at 44.1 kHz.
- Pitch is exponential (1 V/oct style). Quantized note pitch comes from the sequencer or MIDI and then passes through the Slide circuit (§4.6) before the exponential converter.
- **Tuning** knob: ±12 semitones, continuous, default 0. (The original's range is narrower but not standardized. Keep this wide.)
- Playable range: MIDI notes 24–84. The sequencer's native range is 3 octaves plus the transpose.
- Add slight analog drift: very-low-frequency pitch wander, ≤ ±3 cents, default ON, switchable in an "Analog" setting. Make it deterministic per seed so tests stay reproducible.

### 4.2 Square waveshaper
- On the 303, the square wave is **derived from the saw** through a single-transistor shaper. It is **not** an ideal 50 % pulse. Its traits:
  - duty cycle a bit off 50 % **[cal ~ 45–50 %]**
  - the top/bottom are not flat: a visible **tilt/sag**, because the shaper is AC-coupled and follows the saw shape
  - softened edges
- Implement it as: saw → static nonlinearity (soft-clip transfer curve, `tanh`-like with offset) → one-pole HPF (AC coupling, ≈ 10–20 Hz **[cal]**). Then band-limit, either through oversampling or BLEP on the comparator crossing.
- The Waveform switch picks saw or square, with a ~5 ms crossfade to avoid clicks.

### 4.3 VCF: diode ladder (most important module)
The 303's filter is a **4-pole diode ladder**: transistors wired as diodes, with coupled stages. It is **not** a Moog transistor ladder. A Moog ladder model is a failing implementation.

Required properties:
- **Coupled stages.** Each stage loads its neighbors, so the stages are not buffered. Solve the coupled system together, not as 4 cascaded independent one-poles.
- **Unequal capacitor:** the bottom capacitor differs from the other three. The implemented topology has the bottom stage at twice the integration rate, following the mystran/kunn KVR model. The stages are unbuffered, so the poles are widely spread (normalised 0.15 / 1.23 / 2.77 / 3.85). As a result the slope 2–4× above cutoff is ≈ 18.7 dB/oct, and the loop gain needed for self-oscillation is ≈ 17 (Moog: 4).
- **Cutoff definition:** "cutoff" means the **resonance frequency**, where the ladder's phase is −180°. The −3 dB point of the k = 0 response lies about 9.5× lower. Do not calibrate cutoff to the −3 dB point.
- **Nonlinearity:** use `tanh` per stage, or a more accurate diode-pair approximation, in the ladder solver. Input drive level matters. Expose an internal input gain **[cal]** so the "squelch" at high resonance and the slight grit on loud accented notes come out right.
- **Resonance:** feedback from output to input, with a **high-pass in the feedback path** (coupling capacitor, ≈ 100–200 Hz **[cal]**). Effects:
  - bass thins as resonance rises
  - at maximum resonance the filter rings strongly but **does not reliably self-oscillate into a clean sine**, unlike a Moog
- **Solver:** use a zero-delay-feedback (topology-preserving transform) discretization with a nonlinear solve: Newton-Raphson with 2–4 iterations, warm-started from the previous sample's state. A unit-delay feedback hack is not acceptable because it detunes the cutoff and changes the resonance character.
- Cutoff CV sums, in linear volts, then goes through one exponential conversion:
  `cv = cutoffKnob + envModDepth * MEG + accentSweep + keyTrack(=0)`
  The original filter does **not** key-track. Do not add key tracking.
- Approximate cutoff ranges **[cal]**: knob alone from ≈ 300 Hz to ≈ 2.5 kHz, with Env Mod and Accent reaching up to ≈ 5–8 kHz.
- **Env Mod pot quirk:** raising Env Mod also **lowers the base cutoff a little**, so the envelope sweeps around the knob value instead of only above it. Model this as `cutoffOffset = -k * envMod` **[cal]**.
- Stable at any parameter combination, sample rate (44.1–192 kHz) and modulation speed. No NaN or Inf (guard and reset the state if they appear).

### 4.4 Filter envelope (MEG, "Main Envelope Generator")
- Retriggers on every **non-slid** note. Attack is effectively instant: a few ms, with a smoothed edge to prevent clicks.
- Decay is an **exponential discharge** (RC).
- **Decay knob** range **[cal]**: ≈ 200 ms to ≈ 2 s, measured to −60 dB or 1/e (document which).
- **Accented notes ignore the Decay knob** and use the shortest decay (≈ 200 ms **[cal]**). This is essential to the 303 sound.
- During a slide the MEG is **not retriggered**.

### 4.5 Amp envelope (VEG) and VCA
- Fast attack (≈ 1–3 ms **[cal]**). The level then holds with a slow decay (≈ 3–4 s **[cal]**) while the gate is high.
- On gate off: a quick release, about 8–10 ms **[cal]**, without a click.
- The VCA has slight nonlinearity or soft saturation at high level **[cal]**, which is subtle.
- The output passes through a DC-blocking / output coupling HPF (≈ 5–20 Hz **[cal]**).

### 4.6 Slide circuit
- When a step has slide enabled, the **gate stays high** from that step into the next one. The pitch then glides via an **RC lag**: constant time, not constant rate, so large and small intervals take the same time.
- Time constant ≈ 60 ms to reach the target (≈ 3τ) **[cal]**. It is fixed, with no knob (a Mods setting may expose it later).
- No envelope retrigger on the slid-to note. Its accent still applies (see §4.7).

### 4.7 Accent circuit
- An accented step:
  1. boosts VCA level (Accent knob controls the amount, up to ≈ +6–10 dB **[cal]**)
  2. forces the MEG to the short decay (§4.4)
  3. injects an **accent sweep** into the filter CV through the **accent sweep capacitor**.
- **Accent sweep capacitor (key quirk):** the accent pulse charges a capacitor whose charge and discharge rate depends on the **Resonance** knob position. On the original, Resonance is a dual-gang pot and its second gang sets this RC.
  - At low resonance the sweep is a sharp spike that follows the MEG.
  - At high resonance it is smoothed and lagged, so **consecutive accented notes stack**: each accent starts before the cap has discharged, and the cutoff "climbs" over a run of accents (the classic "wow" build-up).
  - Model it as a one-pole RC with charge and discharge paths. Its time constant is a function of Resonance **[cal]**, and its input is `accentAmount * MEG`.
- Without an accent on the step, the accent circuit contributes 0 and its capacitor decays freely.

### 4.8 Gate timing
- Default gate length: ≈ 50 % of a 16th step **[cal]**. A slide extends it to full length plus overlap.
- Tie / held notes: the gate continues without retrigger (same as slide but without a pitch change).
- All events are **sample-accurate** inside the audio block.

---

## 5. Sequencer

- 16 steps per pattern (pattern length 1–16 is configurable), 4/4, 16th-note grid. The sequencer stores 16 patterns per plugin instance.
- Per step: `note (C..C, 13 semitones)`, `octave (-1, 0, +1)`, `accent (bool)`, `slide (bool)`, `gate: note | tie | rest`.
- Pattern-wide transpose: ±12 semitones.
- Clock:
  - **Plugin:** sync to the host transport (PPQ position, tempo, play state). Start on the host's play and align steps to the host bar.
  - **Standalone:** internal clock, 40–300 BPM, with Play/Stop buttons.
- Swing / shuffle: 0–75 %, applied to the off-beat 16ths.
- A "Sequencer On/Off" toggle. When it is off, the voice is played only by MIDI.
- Patterns are saved in the plugin state (§7.3).
- Extras: Randomize pattern, Clear, Shift left/right. Build these in v1 because they are cheap and useful for testing.

---

## 6. MIDI input

- Monophonic, **last-note priority** with a note stack (releasing a held note returns to the previous held note *with slide*).
- **Legato** (a new note-on before the previous note-off) → **slide**, with no MEG/VEG retrigger.
- **Velocity ≥ 100** (threshold param, default 100) → **accent**.
- Pitch bend ±2 semitones (configurable) goes into the pitch CV before the slide RC.
- CC mapping (defaults, MIDI-learnable later): CC74 Cutoff, CC71 Resonance, CC75 Decay, CC76 Env Mod, CC77 Accent, CC7 Volume.
- When both the sequencer and MIDI are active, a received MIDI note **transposes** the running pattern (as on many 303 clones). This is an optional mode toggle, default off.

---

## 7. Plugin parameters, GUI, state

### 7.1 Parameters (`juce::AudioProcessorValueTreeState`)
| ID | Name | Range | Default | Curve |
|---|---|---|---|---|
| `wave` | Waveform | Saw / Square | Saw | choice |
| `tune` | Tuning | -12..+12 st | 0 | linear |
| `cutoff` | Cut Off Freq | 0..1 | 0.5 | maps to CV linearly; CV→Hz exponential |
| `reso` | Resonance | 0..1 | 0.5 | slight log taper **[cal]** |
| `envmod` | Env Mod | 0..1 | 0.5 | linear |
| `decay` | Decay | 0..1 | 0.5 | maps to 200 ms–2 s, log |
| `accent` | Accent | 0..1 | 0.5 | linear |
| `volume` | Volume | -inf..+6 dB | 0 dB | dB |
| `drift` | Analog drift | on/off | on | — |
| `seqOn` | Sequencer | on/off | on | — |
| `swing` | Swing | 0..75 % | 0 | — |
| `accThr` | Accent velocity threshold | 1..127 | 100 | — |

- All continuous parameters are smoothed (≈ 10–20 ms) and fully automatable.
- **Knob tapers matter for feel.** Document every mapping function in `Parameters.cpp`.

### 7.2 GUI
- A single resizable window (default 900×520, scalable 75–200 %) with vector-drawn knobs. No bitmaps are required for v1.
- Top row: the 8 front-panel controls, in the original's left-to-right order: Tuning, Cut Off, Resonance, Env Mod, Decay, Accent, then Waveform and Volume.
- Bottom: the 16-step grid. Each step has a note picker, octave −/+ toggles, and Accent, Slide and Gate toggles. A playhead shows the current step.
- Original-inspired look (silver panel, dark knobs), but **no Roland logos or trade dress copied 1:1**.
- The GUI must never touch DSP state directly. Use parameters and lock-free FIFOs for pattern edits only.

### 7.3 State
- `getStateInformation` / `setStateInformation`: XML or ValueTree that includes a version number, all parameters, all 16 patterns and the selected pattern.
- Factory presets: at least 8 sound presets and 8 classic-style acid patterns (original patterns, not transcriptions of commercial songs).

---

## 8. Quality, performance, real-time safety

- **Oversampling:** 4× by default with an internal 2×/4×/8× setting, using polyphase IIR or FIR halfband filters (`juce::dsp::Oversampling` is acceptable). Keep the oscillator, shaper, VCF and VCA inside the oversampled section.
- **CPU:** < 3 % of one Apple M1 core at 48 kHz, 4× oversampling, 128-sample buffer (Release build).
- **Real-time safety in `processBlock`:** no allocation, no locks, no I/O, no logging. Prepare everything in `prepareToPlay`.
- **Denormals:** use `juce::ScopedNoDenormals` and flush state with tiny offsets where needed.
- **Latency:** report the oversampler latency to the host via `setLatencySamples`.
- **Sample rates:** 44.1, 48, 88.2, 96, 176.4 and 192 kHz. Block sizes 1–4096, including variable block sizes.
- **Determinism:** with drift off (or a fixed seed), the same input gives bit-identical output across runs on the same machine.

---

## 9. Verification (the agent must do this, it cannot "listen")

### 9.1 Render CLI
`tools/render/acid3-render`:
```
acid3-render --pattern patterns/test01.json --bpm 130 --bars 4 \
             --params "cutoff=0.4,reso=0.8,envmod=0.7,decay=0.3,accent=0.8,wave=saw" \
             --sr 48000 --out out.wav [--seed 1 --no-drift]
acid3-render --midi in.mid --params ... --out out.wav
```
Pattern text format (implemented in place of JSON): one space-separated token per step, `<gate><note>[octave][a][s]`. Gate is `n`/`t`/`r`, note is 0–12, octave is an optional `-` or `+`, `a` marks an accent and `s` a slide. Example: `n0- n12as t0 r0`. `--pattern-file` reads the same format from a file. The `--midi` input is **not implemented yet**.

### 9.2 Unit tests (Catch2): required
1. **Oscillator:** the fundamental frequency is within ±1 cent for MIDI 24–84 at 44.1/48/96 kHz. Aliasing products at C5, measured through the oscillator → 4× → decimator path, are < −80 dB relative to the fundamental (FFT test).
2. **Square:** duty cycle and tilt are within the calibrated ranges, and the waveform is not a clean 50 % pulse (even harmonics present at > −40 dB relative to the fundamental).
3. **Filter frequency response** (small-signal, linear region):
   - the resonant peak sits at the requested cutoff within ±5 %
   - the critical loop gain is 17 ± 0.2
   - at resonance 0, the slope from 2× to 4× cutoff is between 15 and 21 dB/oct (proves it is not a 24 dB Moog response)
4. **Resonance:** the peak height increases monotonically with `reso`. At `reso = 1` the impulse response rings for > 50 ms. Low-frequency (50 Hz) gain at `reso = 1` is lower than at `reso = 0` by at least 6 dB **[cal]**.
5. **Stability:** fuzz 10,000 random parameter/automation sequences at all sample rates and confirm no NaN/Inf and |out| < 4.0.
6. **MEG:** decay time constants match the knob mapping within ±10 %. Accented notes use the short decay regardless of the knob.
7. **Slide:** the pitch reaches 95 % of the interval in 60 ms ± 15 ms **[cal]**, the same for a 1-semitone and a 12-semitone jump. No envelope retrigger.
8. **Accent stacking:** with high resonance, a run of 4 consecutive accented 16ths at 130 BPM gives a measured peak filter CV (internal probe) that strictly increases from note 1 to note 4. With low resonance it does not increase by more than 5 %.
9. **Sequencer timing:** step onsets fall on exact sample positions for the host PPQ, including tempo changes mid-block and loop wrap-around.
10. **State roundtrip:** save → load → identical parameters and patterns.
11. **Determinism:** two renders with the same seed are bit-identical.

### 9.3 Reference comparison (when `/reference` has data)
The user may place recordings of a real TB-303 in `/reference/` with a sidecar JSON that documents settings, pattern and BPM. When present:
- Render the same pattern and settings with `acid3-render`.
- `tools/analysis/compare.py` reports:
  - log-spectral distance per note (averaged)
  - spectral centroid trajectory over each note (captures the envelope/accent sweep)
  - RMS envelope per note (VEG shape, accent level)
  - pitch trajectory during slides (YIN or similar)
- Tune the `[cal]` constants to minimize these distances. Log every calibration change with before and after metrics in `CALIBRATION_LOG.md`.
- If no references exist, use the published literature values cited in the code comments and mark them clearly as **uncalibrated** in `README.md`.

### 9.4 Format validation
- `pluginval` at strictness level 8 with no failures (VST3). Run it manually; the build sandbox could not execute downloaded binaries.
- `acid3-hostcheck` (built from source) loads the VST3 through JUCE's hosting code and checks:
  - all sample rates and block sizes, with MIDI
  - host sync and stop
  - parameter and state round trip, including patterns
  - CPU budget
  - that the editor opens
- The VST3 loads and plays in at least one host if available on the machine (e.g. Reaper or Ableton Live). Otherwise, document that this step was skipped.
- The Standalone launches, lists audio and MIDI devices, and plays the internal sequencer.

---

## 10. Milestones (do them in order, each must build and pass its tests)

1. **Skeleton:** CMake + JUCE, empty VST3 + Standalone build as a universal binary, the `acid3_core` library, the Catch2 harness, and `acid3-render` writing silence.
2. **Oscillator + shaper + VCA + VEG:** tests 1, 2, 11.
3. **Diode ladder VCF + MEG:** tests 3, 4, 5, 6.
4. **Accent + slide + gate logic:** tests 7, 8.
5. **Sequencer + MIDI + host sync:** tests 9. Render the factory patterns.
6. **Parameters, state, presets, GUI:** test 10.
7. **Oversampling, performance, real-time audit:** CPU budget, pluginval.
8. **Calibration pass:** §9.3 if references exist, plus the README and CALIBRATION_LOG.

At each milestone, render a short demo WAV into `renders/` and commit it if the repo uses git. Never break an earlier test.

---

## 11. Reserved: Mods (not v1)
Keep hooks in the code for these, but do not implement them:
- Devil Fish-style controls: separate accent decay, normal decay range extension, slide time, filter FM, overdrive, and the filter tracking switch.
- Built-in distortion stage.
Hide everything here behind a single compile-time flag `ACID3_MODS=0`.

---

## 12. References for the agent
Use these as behavioral references. **Do not copy GPL code into this project.** If you take reference code from a permissively licensed project (e.g. Open303, MIT), keep its license notice and record it in `THIRD_PARTY.md`.
- Tim Stinchcombe: analysis of the TB-303 diode ladder filter (transfer function, unequal capacitor).
- Robin Whittle, Devil Fish documentation: detailed description of the MEG, accent sweep circuit, slide and gate behavior.
- Roland TB-303 service notes / schematic: component values for the calibration starting points.
- Vadim Zavalishin, *The Art of VA Filter Design*: ZDF/TPT discretization and nonlinear ladder solving.
- Open303 by Robin Schmidt: an existing open-source behavioral model, useful for sanity-checking.

---

## 13. Definition of done
- `cmake --build` produces `Acid3.vst3` and `Acid3.app` (universal binary), with ad-hoc codesigning at minimum.
- All §9.2 tests pass and pluginval passes at level 8.
- CPU budget is met.
- `README.md` covers how to build, install (`~/Library/Audio/Plug-Ins/VST3/`), run the render CLI and run the tests, and states which `[cal]` values are calibrated against references and which come from the literature.
- Demo renders for each factory pattern are in `renders/`.
