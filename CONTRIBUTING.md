# Contributing

Thanks for helping. The most useful thing you can do is tell us what you hear.

## Reporting a problem
Open an [issue](https://github.com/aemon-audio/aemon/issues) with your DAW and its version, the plugin, sample rate, what you did, and what you heard
or saw. A short audio file or screenshot helps a lot.

## Changing code
- Keep the sound engines free of allocations and locks once prepared.
- Every change to the sound needs a measurement in `tests/`. Don't loosen a test to make it pass.
- Build and run the tests before you open a pull request:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target AemonTests AemonEqTests AemonCompTests AemonLimTests AemonEqEngineTests AemonLinearPhaseTests AemonCompEngineTests AemonLimEngineTests
```

- Text in the plugins is plain language: say what a control does for the music.

By contributing you agree that your work is licensed under the AGPL-3.0-or-later.
