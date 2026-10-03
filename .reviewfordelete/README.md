# Review for delete

Files that are obsolete or have been merged into the manuals. Look through them, then delete the folder (or
pull back anything still needed). Git history keeps the tracked ones either way.

| File | Moved | Why | Content now lives in |
|---|---|---|---|
| `build.log` | 2026-10-03 | Stray build output ("ninja: no work to do."), tracked by mistake | – |
| `ESP01_STM32_INTEGRATION_GUIDE.md` | 2026-10-03 | Written before the STM32 side existed: generic "copy this example" code for a colleague, old paths (`Documents\PlatformIO\...`), personal SSID/IP | Protocol: `docs/esp01-protocol.md`. STM32 implementation: `src/main.c`, SPM §5 |
| `WINDOWS.md` | 2026-10-03 | Submodule instructions are obsolete (HAL is vendored); toolchain paths outdated | SPM §4.1 |
| `WSL.md` | 2026-10-03 | Same submodule instructions; WSL flow unused | SPM §4.1 (short WSL note) |
