# Third-party components

| Component | Version | License | Use |
|---|---|---|---|
| JUCE | 8.0.15 (`extern/JUCE`) | AGPLv3 **or** commercial JUCE license | Plugin wrapper, GUI, VST3 hosting in `acid3-hostcheck` |
| Catch2 | 3.16.1 (`extern/Catch2`) | Boost Software License 1.0 | Unit tests |
| VST3 SDK | bundled with JUCE | MIT (since VST3 SDK 3.8) | VST3 format |

**JUCE licensing:** distributing a binary built against JUCE requires either releasing the source under AGPLv3 or holding a JUCE license (a free tier exists for small revenue). Settle this before distributing builds.

## Referenced (no code copied)
- **Open303** by Robin Schmidt, MIT license, Copyright (c) 2009 Robin Schmidt (www.rs-met.com). Several measured constants are taken from it: cutoff range, env-mod scaler/offset fit, envelope times, and the output filter chain. Each is tagged `[O303]` in `src/dsp/Calibration.h`. The DSP implementation in this project is independent.
- **KVR forum, "mystran & kunn" 303 diode ladder topology:** the linear coupling structure of the ladder in `src/dsp/DiodeLadder.h`.
