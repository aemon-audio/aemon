## Aemon Suite

Free, open-source mixing and mastering plugins for Windows (VST3, 64-bit): **Utility, EQ, Compressor and Limiter**.

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
VST3 folder that every DAW on the computer shares. **Signing is coming soon**: we will apply for free open-source
signing as soon as the code is public. Windows may still ask for a few weeks after that.

To check that your download is the real file, compare it with `SHA256SUMS.txt`:

```powershell
Get-FileHash .\Aemon-Suite-Setup.exe
```

### Good to know
The installer replaces the earlier single-plugin test versions, keeps your presets and never closes your DAW.
The plugins collect nothing and never go online.
