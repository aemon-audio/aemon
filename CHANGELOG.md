# Changelog

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
