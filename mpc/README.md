# Acid3 on MPC OS (Akai MPC / Force standalone)

The Acid3 DSP core (`src/dsp`, `src/seq`: no JUCE) built as a native MPC OS plugin with the
[mpc-vst-plugins](https://github.com/open-mpc/mpc-vst-plugins) toolchain. The toolchain is not part of this repo: clone it
anywhere (tested with commit `e4edb54`); the commands below assume `./mpc-vst-plugins` (gitignored), any path works.
The MPC build is a Linux **VST2 `.so`** for ARM, loaded by MPC's built-in plugin host, with a native MPC touchscreen page.
The JUCE plugin, its editor and the macOS VST3/Standalone builds are not used here.

| File | What |
|---|---|
| `acid3_mpc.cpp` | `mpc_engine()` adapter: params, MIDI, song position, `state` chunk, value text |
| `vst.json` | port description (sources, defines, targets) |
| `params.json` / `params.lock` | parameter list in VST index order. **Append only**: projects and Q-Links bind by index |
| `presets.json` | the 8 factory sounds in MPC's PRESET menu |
| `layout.conf` | the MPC page (knobs, popup, toggles, Q-Link order) |

## Build and test (needs Docker)
```sh
git clone https://github.com/open-mpc/mpc-vst-plugins.git && git -C mpc-vst-plugins checkout e4edb54
# offline x86 test under ASan (on macOS the SDK's libc++ headers have to be named, see ../CMakeLists.txt)
CPLUS_INCLUDE_PATH="$(xcrun --show-sdk-path)/usr/include/c++/v1" mpc-vst-plugins/tools/test_port.sh mpc/vst.json
# device builds: build/acid3.so (armv7: Gen1 Live/One/X/Key 61, Force) and build/aarch64/acid3.so (Gen2), plus the skin
mpc-vst-plugins/tools/build_port.sh mpc/vst.json
# skin preview
.venv/bin/python mpc-vst-plugins/tools/studio.py preview "mpc/build/skin/Acid3 - VST - Acid3/Plugin Skins" -o mpc/build/preview/page_%d.png
# install zips (dist/), one per CPU
cd mpc && for so in build/acid3.so build/aarch64/acid3.so; do ../.venv/bin/python <path-to>/mpc-vst-plugins/tools/release.py --so $so \
  --skin "build/skin/Acid3 - VST - Acid3" --entry build/pluginlist-entry.xml --version 1.0.0 --about "..." -o dist; done
```
Installing needs root SSH on the device (or the toolchain's installer app / USB-stick routes): see the zip's `INSTALL.md`.

## How it behaves on the MPC
- **Play it from MPC:** pads, keys and MIDI clips. Velocity ≥ ACCENT VEL (100) = accent; overlapping notes slide; pitch bend ±2 st.
  Program the bass line in MPC's own sequencer: there is no step grid on the MPC page (MPC skins have no custom-drawn widgets).
- **SEQ on:** the internal 16-step sequencer plays the chosen factory pattern, locked to MPC's song position (starts and loops with MPC PLAY).
- **Q-Links:** bank 1 = Cut Off, Resonance, Env Mod, Decay, Accent, Waveform, Tuning, Volume; bank 2 = the sequencer controls.
- CC 74/71/75/76/77/7 move Cut Off/Resonance/Decay/Env Mod/Accent/Volume and the page follows. CC 20-35 drive the Q-Links (toolchain default).
- 44.1 kHz, notes start sample-accurately (`SAMPLE_ACCURATE`).

## Status (2026-11-06)
- Offline host test: PASSED (ASan/UBSan, x86). Device builds: armv7 (highest glibc 2.29) and aarch64; only `VSTPluginMain` exported.
- **Not yet run on a device.** Still to do: `tools/bench.sh build/acid3.so <ip>` on a Gen1 unit (the 4x-oversampled diode ladder is
  the cost to watch; x86 ≈ 2.1 % of a core), then the device smoke test (docs/RELEASING.md step 5).
- The skin is MPC OS 3.x format; `SHADOW_SKIN_MPC_OS=2` at build time writes a 2.x skin (untested).
- Not catalog-listed: that needs a public repo, `release.py --repo owner/name --license <SPDX>` and `catalog_check.py --catalog`.
