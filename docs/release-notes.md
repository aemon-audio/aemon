## Aemon Suite

Free, open-source mixing and mastering plugins for Windows (VST3, 64-bit): **Utility, Tune, EQ, De-esser, Compressor, Rider,
Multiband, Transient, Saturation, Low End, Imager, Reverb, Delay, Clipper and Limiter**.

### New in 0.3.0
- Six new plugins: **Tune, Rider, Transient, Low End, Reverb and Delay**.
- **Style presets** for vocals and beats in every plugin.
- **EQ: a freeze is fixed.** Closing the EQ on a very busy computer could hang the DAW (0.1.0 and 0.2.0). Please update.
- A plugin that delays the sound says so: "live" or "mixing" in the bar at the bottom, and a card the first time.

Already have 0.1.0 or 0.2.0? Run the new setup over it. It updates what you have and adds the new ones.

### Install
1. Close your DAW and run **Aemon-Suite-Setup.exe**.
2. "Windows protected your PC": choose **More info**, then **Run anyway**.
3. "Do you want to allow this app to make changes to your device?": choose **Yes**.
4. Tick the plugins you want, press **Install**, then scan for new plugins in your DAW.

No installer? **Aemon-Suite-VST3.zip** holds the plugins: copy the `.vst3` folders to
`C:\Program Files\Common Files\VST3`.

### Why Windows asks
Aemon is **not signed yet**. A signature tells Windows who made a file. Without one, Windows warns about any new app
that few people have installed, and shows "Unknown publisher". The second question appears because plugins go in the
VST3 folder that every DAW on the computer shares. **Signing is coming soon**: next, we apply for free open-source
signing. Windows may still ask for a few weeks after that.

To check that your download is the real file, compare it with `SHA256SUMS.txt`:

```powershell
Get-FileHash .\Aemon-Suite-Setup.exe
```

### Good to know
The installer replaces the earlier single-plugin test versions, keeps your presets and never closes your DAW.
The plugins collect nothing and never go online.
