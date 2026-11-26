# Changelog

## 0.3.0

- Six new plugins: Tune, Rider, Transient, Low End, Reverb and Delay. Fifteen in all. They passed every test.
- Style presets in every plugin, for vocals and for beats. 130 of them in the first nine. No old preset was changed.
- EQ: fixed a freeze. Closing the EQ while the computer was very busy could hang the DAW, because it stopped a
  background thread by force. This was in 0.1.0 and 0.2.0. If you use the EQ, update.
- A plugin that delays the sound says so: “live” or “mixing” with the milliseconds in the bar at the bottom, and a
  card the first time.
- The installer updates what you have and adds the six new plugins. Anything you left out on purpose stays out.

## 0.2.0

- Five new plugins: De-esser, Multiband, Saturation, Imager and Clipper. Nine in all.
- The installer finds the Aemon plugins you already have, marks them as updates and adds the new ones.
- Presets in the De-esser and the Multiband set themselves to your sound.
- Build: pluginval only finished for the first plugin (the step did not wait for it). It now runs for all nine and stops the
  build if one fails. The four plugins in 0.1.0 had passed it locally.
- EQ: clearer description for "808: audible on phone speakers".

## 0.1.0

First public release.

- Utility, EQ, Compressor and Limiter (VST3, Windows 64-bit).
- One installer for all four, and a ZIP.
- Not signed yet. Windows asks for confirmation when installing.
