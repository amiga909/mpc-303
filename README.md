# Acid3: TB-303 style bass line synthesizer

Monophonic acid bass synth for macOS (VST3 + Standalone, universal arm64/x86_64). It models the 303 at circuit-behaviour level:
- diode-ladder VCF (zero-delay feedback, Newton-solved)
- saw-derived square
- MEG/VEG envelopes
- accent sweep capacitor driven by Resonance
- RC slide on the pitch CV
- 16-step sequencer with host sync

See `SPEC.md` for the full specification.

> **Calibration status:** **uncalibrated** against real hardware. Constants come from published measurements (Open303) and circuit analysis, and some are estimates. See `CALIBRATION_LOG.md` for which is which.

MPC / Force standalone: see [`mpc/README.md`](mpc/README.md).

## Build

Requirements: macOS 12+, Xcode Command Line Tools, CMake ≥ 3.24, Ninja (`brew install cmake ninja`).

JUCE 8.0.15 and Catch2 v3.16.1 are downloaded by CMake on the first configure (pinned tags). A checkout in
`extern/JUCE` / `extern/Catch2` is used instead when present (offline builds).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Outputs:
- `build/Acid3_artefacts/Release/VST3/Acid3.vst3`
- `build/Acid3_artefacts/Release/Standalone/Acid3.app`

If your Command Line Tools install has an incomplete `usr/include/c++/v1`, `CMakeLists.txt` detects it and uses the SDK's libc++ headers instead.

Install the VST3:
```sh
cp -R build/Acid3_artefacts/Release/VST3/Acid3.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

## Test and verify

```sh
./build/acid3_tests                                   # 22 DSP/sequencer test cases (SPEC §9.2)
./build/acid3-hostcheck_artefacts/Release/acid3-hostcheck build/Acid3_artefacts/Release/VST3/Acid3.vst3
```

`acid3-hostcheck` loads the built VST3 through JUCE's hosting code and checks:
- every sample rate and block size, with MIDI
- the host-synced sequencer
- state round trip
- CPU budget
- that the editor opens

For full format validation, also run [pluginval](https://github.com/Tracktion/pluginval) at strictness 8:
```sh
pluginval --strictness-level 8 --validate build/Acid3_artefacts/Release/VST3/Acid3.vst3
```

## Render CLI

```sh
./build/acid3-render --pattern "n0- n0 n12as n0 n3 n0-s n7 n0a" --bpm 130 --bars 4 \
    --params "cutoff=0.3,reso=0.85,envmod=0.7,decay=0.4,accent=0.8,wave=saw" \
    --out out.wav [--sr 48000] [--seed 1] [--no-drift]
.venv/bin/python tools/analysis/analyze.py out.wav --bpm 130    # per-step level, centroid, f0
```

Pattern tokens are space-separated, one per step, in the form `<gate><note>[octave][a][s]`:
- gate: `n` = note, `t` = tie, `r` = rest
- note: 0–12 semitones above C
- octave: `-` or `+` (optional)
- `a` = accent, `s` = slide into the next step

Demo renders of the 8 factory patterns are in `renders/`.

## Playing it
- **Knobs:** Tuning, Cut Off, Resonance, Env Mod, Decay, Accent, Waveform and Volume, as on the original. Also Swing, Analog Drift, Tempo and Transpose.
- **Sequencer:** follows the host transport. When the host is stopped, or in the Standalone, press PLAY to use the internal clock.
- **Step grid:**
  - Click a note cell to set the note. Clicking the lit note makes the step a rest.
  - The OCT row cycles 0 → up → down.
  - ACC and SLIDE toggle.
  - GATE cycles note → tie → rest.
- **MIDI:**
  - Velocity ≥ 100 (adjustable) plays an accent.
  - Overlapping notes slide.
  - Pitch bend covers ±2 semitones.
  - CC74 cutoff, CC71 resonance, CC75 decay, CC76 env mod, CC77 accent, CC7 volume.

## Layout
```
src/dsp      Oscillator, DiodeLadder, Filters, Voice, Calibration.h  (pure C++)
src/seq      Pattern, Sequencer, Engine                               (pure C++)
src/plugin   JUCE processor, editor, parameters, presets
tools/       render CLI, host check, python analysis
mpc/         MPC OS port (Linux ARM VST2 + MPC skin), see mpc/README.md
tests/       Catch2 tests
```
