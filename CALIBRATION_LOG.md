# Calibration log

All `[cal]` constants live in `src/dsp/Calibration.h`. **No reference recordings have been used yet**, so every value is either taken from published measurements or is an engineering estimate. Each constant in the file carries a source tag:

| Tag | Meaning |
|---|---|
| `[O303]` | Measured values published in Open303 (Robin Schmidt, MIT). Constants only; no code was copied. |
| `[KVR]` | 303 diode-ladder topology from the "mystran & kunn" KVR thread, as credited in Open303 |
| `[CIRC]` | Derived from the circuit description (dual-gang resonance pot, slide RC on the pitch CV, and so on) |
| `[EST]` | Estimate. Calibrate this first once reference audio exists. |

## 2026-11-03: initial model (uncalibrated)

### Filter: cutoff definition corrected
- **Before:** `cutoffHz` was mapped to the −3 dB point of the linear ladder (k = 0).
- **Finding:** in this topology the poles are widely spread (normalised poles 0.15, 1.23, 2.77, 3.85). The −3 dB point therefore sits **9.5× below** the resonance (−180°) frequency. With cutoff defined at −3 dB, resonance appeared far above the nominal cutoff, and the slope directly above "cutoff" was only 6 dB/oct.
- **After:** `cutoffHz` = resonance frequency. Measured:
  - critical loop gain **16.96** (the known ≈17 of the 303 diode ladder)
  - slope from 2× to 4× cutoff **18.7 dB/oct**, which matches the "18 dB" 303 lore
  - resonant peak within ±5 % of the requested cutoff
- Unit tests: `tests/TestFilter.cpp`.

### Accent sweep RC (`accentSweepTauMinMs` = 5, `accentSweepTauMaxMs` = 100)
- Chosen so that at Resonance 0, four consecutive accented 16ths at 130 BPM differ by < 5 % in sweep peak, while at Resonance 0.9 each accent climbs higher than the previous one.
- `tests/TestVoice.cpp` "Accent sweep" pins this behaviour.
- **This is the most uncertain constant in the model.** Calibrate it against a real accent run at several resonance settings.

### Slide (`slideTauMs` = 20)
- Reaches 95 % of the interval in about 60 ms, the same for a 1-semitone and a 12-semitone jump (an RC on the pitch CV).
- Open303 uses τ = 60 ms on frequency, which gives a much slower glide. Both are estimates.

### Gain staging (`outputGain` = 0.8, `ladderDrive` = 1.6, `filterInputLevel` = 0.5)
- Set so that accents peak around −2 to −6 dBFS with the factory presets.
- The drive amount sets how much the input pair saturates at high resonance (the "squelch"). It is unverified.

## How to calibrate once references exist (SPEC §9.3)
1. Record a real unit with known knob positions. Single notes and an accent run at 3 resonance settings are the minimum.
2. Render the same pattern and settings with `acid3-render --no-drift`.
3. Compare spectral-centroid trajectories, RMS envelopes and the pitch track during slides (`tools/analysis/analyze.py` gives per-step numbers).
4. Change one constant at a time and append the before/after metrics here.
