# GooeyMAME application icon

Master artwork: **`gooeymame.png`** (878×878 RGBA, transparent background).
Everything else in this directory is generated from it by `regenerate.sh`.

## Files

| File | Platform | Notes |
|------|----------|-------|
| `gooeymame.png` | — | Canonical master; edit/replace this and re-run `regenerate.sh`. |
| `icons/gooeymame_{16,24,32,48,64,128,256,512,1024}.png` | all | Per-size PNGs (Lanczos). |
| `gooeymame.qrc` | all (Qt) | Compiled by `rcc` into the binary; supplies the Qt window/taskbar icon via `QApplication::setWindowIcon` (`qtmain.cpp`). Sizes 16–512. |
| `gooeymame.ico` | Windows | Multi-res (16–256). Copied to `scripts/resources/windows/mame/mame.ico`, which `mame.rc` embeds into `mame.exe`. |
| `gooeymame.icns` | macOS | 11 slots, 16–1024 (icp4/5/6, ic07–ic14). For a future `.app` bundle (`CFBundleIconFile`). |
| `../../../scripts/resources/unix/gooeymame.desktop` | Linux | freedesktop entry; installed by `install-desktop.sh`. |

## Regenerating

```sh
./regenerate.sh            # rebuilds every size + .ico + .icns from gooeymame.png
```

Requires ImageMagick (`magick`), `icotool` (icoutils), and `python3`.
After changing the icon, rebuild GooeyMAME; the build re-runs `rcc` automatically
(the PNGs are dependencies of the resource rule in `scripts/src/osd/qtui.lua`),
and copy `gooeymame.ico` over `scripts/resources/windows/mame/mame.ico` if the
`.ico` changed.
