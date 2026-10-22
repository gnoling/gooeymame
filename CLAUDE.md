# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

GooeyMAME = official MAME 0.288 + a Qt 6 front-end compiled in as its own OSD target (`OSD=qtui`).
One executable: no arguments launches the Qt browser GUI; any arguments behave like command-line
MAME; `--gooey <system> [software]` boots straight into an embedded play window.

- Branch `gooeymame` (based on the `mame0288` tag). `origin` = github.com/gnoling/gooeymame,
  `upstream` = mamedev/mame. Don't commit to the legacy `gnoling/mameui` checkout.
- Everything GooeyMAME-specific lives in `src/osd/qtui/`, `scripts/src/osd/qtui*.lua`, and a small
  set of additive OSD modules (`src/osd/modules/input/input_qt.cpp`, `input_sdlgame.cpp`,
  `monitor/monitor_qt.cpp`, `OSD_QT_GL` branches in `render/drawogl.cpp` / `drawbgfx.cpp`,
  register blocks in `lib/osdobj_common.cpp`). The rest is stock MAME; keep shared-file edits
  minimal and additive (`#if defined(OSD_QT_GL)`) so upstream rebases stay clean.
- `tools/` (media import/optimise scripts) is untracked personal tooling.

## Build & run

```sh
# Linux, full driver set (binary: ./mame)
make OSD=qtui SUBTARGET=mame -j"$(nproc)"
# Fast compile/smoke target (binary: ./mametiny)
make OSD=qtui SUBTARGET=tiny -j"$(nproc)"

./mame                       # GUI
./mame pacman                # CLI passthrough through the Qt-native OSD
./mame --gooey pacman        # embedded play window; --opengl/--bgfx/--bgfx-backend/--shader
QT_QPA_PLATFORM=offscreen ./mame -help   # headless smoke test
```

- After adding/removing source files or touching any `scripts/src/osd/qtui*.lua`: rebuild with
  `REGENIE=1`. GENie/make do NOT rebuild unchanged TUs when only flags/defines changed, so for a
  flag-only .lua change also delete `build/*/obj/*/osd_qtui`, `ocore_qtui`, `qtdbg_qtui`.
- Release builds: `SYMBOLS=0 STRIP_SYMBOLS=1` (dev builds keep symbols; the unstripped binary is
  ~560 MB and that is expected).
- `NO_QTPDF=1` drops the PDF manual viewer; otherwise Qt PDF is auto-detected via `qmake6`.
- Windows: MSYS2 **UCRT64** (not mingw64; qt6-pdf only exists there), same make line with `-j16`.
  Run `dist\mame.exe`, never the repo-root exe (no Qt DLLs beside it). After a plain rebuild just
  `cp -f mame.exe dist/`; redo windeployqt + deep-ldd only when the toolchain/Qt changes.
- There are no qtui unit tests. MAME's `tests/` is upstream's core test suite and does not cover
  the front-end. Verification is: build, offscreen `-help`, then on-screen use.

## Architecture (read this before touching emulator.cpp or mainwindow.cpp)

### Two worlds, one macro boundary
MAME core headers (`emu.h`) and Qt headers clash on macros, so the code is split strictly:

- **Qt side** (`qtmain.cpp`, `mainwindow.*`, models/proxies, panels, dialogs): never includes
  MAME core headers.
- **Core side** (`emulator.{h,cpp}`): Qt-free bridge to the MAME core. `emulator.h` exposes plain
  `std::string`/callback APIs (`qtui_run_args_native`, `qtui_run_embedded_native`,
  `qtui_load_software`, `qtui_audit_all*`, `qtui_read/write_options`, `qtui_load_asset`, ...).
- **Qt-free shim headers** carry data across the boundary: `embedsession.h` (command queue +
  snapshots), `qtembedtarget.h`, `qtglprovider.h`, `qtnativewindow.h`, `qtbgfxchains.h`,
  `qtinput.h` (`QtInputBus`). `qtwindow.*` / `qtglcontext.h` are the one place both meet; they
  include `emu.h` before Qt, same discipline as MAME's Qt debugger module.
- `threadutil.*` deliberately includes neither (so `<windows.h>` can't collide).

### The Qt-native OSD
`qt_osd_interface : osd_common_t` (anon namespace in `emulator.cpp`) is a full SDL-free OSD:
MAME renders into a `QWindow` through OpenGL or BGFX and takes keyboard/mouse/lightgun/text from
Qt events via `QtInputBus` (providers registered as `"qt"`). The only SDL left is the Linux
game-controller joystick module (`input_sdlgame.cpp`, no SDL video/window); Windows uses
winhybrid/xinput/dinput. Sound = pulse/pipewire/portaudio on Linux, WASAPI/XAudio2/PortAudio on
Windows. The native run forces its providers on the command line it builds.

### Threading model
- **GUI thread** owns the Qt event loop and the `QWindow`. The **emulation runs on a worker
  `std::thread`** (both embedded play and CLI passthrough); MAME's `update()/draw()` run there,
  so the `QOpenGLContext` is created/made-current/swapped on the worker (Qt threaded-GL model).
- **In-game menus** post `EmbedCommand`s (`embedsession.h`) from the GUI thread; `EmbedController`
  drains and applies them on the emulation thread, the only thread allowed to touch
  `running_machine`. State flows back as published snapshots (sliders, DIPs, inputs, cheats,
  shader chains, plugin menus). Add new in-game features by extending that enum + apply() +
  a snapshot, never by calling the core from Qt.
- **`core_guard`** (`emulator.cpp`) = a FIFO ticket mutex + forced `"C"` locale. Every
  machine_config build / audit / options read-write must hold it. Hold it per item, never across
  a whole sweep (bulk audits used to starve the GUI for minutes). Background sweeps (ROM audit,
  software audit, screenless scan) call `lower_current_thread_priority()` first; the on-demand
  software loader stays at normal priority and is retired without joining (`softwareloader.*`),
  since joining a worker parked on the core lock froze the GUI.
- **Locale:** `QApplication` switches the process to the user's UTF-8 locale and MAME's ini parser
  infinite-loops under it. Anything that runs core code must go through `core_guard` or otherwise
  set `LC_ALL=C` around the call and restore afterwards.

### Front-end data flow
- `GameListModel` is built straight from the driver list; `GameListProxy` ANDs the folder-tree
  filter (structural categories only) with status modifiers, column value filters and search.
  "Arcade" = `game_driver::compatible_with == nullptr` (official MAME has no arcade flag).
- `FamilyTreeModel` / `GridView` are the grouped and tile views over the same model.
- Software lists: `SoftwareLoader` (threaded, two-phase: entries then availability) ->
  `SoftwareModel` / `SoftwareProxy`. `AuditManager` / `SoftwareAuditManager` batch results to
  the models through a timer flush.
- Art/info: `ArtworkPanel` (art tabs, video, info tabs, PDF manual, and the pane that hosts the
  running game), `ArtLoader` / `IconLoader` / `ThumbnailLoader` / `InfoLoader` on worker threads
  taking no core lock. All art lookups resolve through `artSearchPaths(key)` in `frontendpaths`:
  the key's `;`-separated list of folders/zips in priority order, EXTRAs layout
  (`<key>/<machine>.png`, `<key>/<list>/<software>.png`). `FRONTEND_FOLDERS` types each key
  (SearchPath / Dir / File) and lists name aliases used by Options ▸ Auto-fill and by the
  startup settings migration; there is no separate "secondary root" setting any more.
- `OptionsDialog` edits mame.ini through the core bridge (`qtui_read_options` returns groups of
  `qtui_option` with value, description and, except for slot/image entries, MAME's default) and
  the front-end settings through QSettings. Every form row is registered with `registerRow` so
  the search box can filter it; new pages should use `makeIntro` / `makeMuted` /
  `makeOrderedList` / `makeSearchPathEditor` for a consistent look. `mainwindow` re-applies art
  sources after Options closes.
- Headless UI check: run `./mame` under `xvfb-run` and drive it with `xdotool` (focus the window,
  `alt+t` then `o` opens Options), then `import -window <id> out.png`. There is no window manager,
  so use `xdotool windowfocus`, not `windowactivate`.

### Persistence (user requirement: everything persists)
Every control, filter, view mode and splitter position is saved to QSettings
(`~/.config/GooeyMAME/GooeyMAME.conf` on Linux, `%APPDATA%\GooeyMAME\GooeyMAME.ini` on Windows,
IniFormat everywhere) and restored in `MainWindow::restoreSettings()`. New UI state must do the
same. Restore pattern: `blockSignals(true)` on the whole group, set each value, unblock, then
call the apply slot once. Long-lived caches (`availability.cache`, `software_availability.cache`,
`screenless.cache`, `controls.cache`) live in `QStandardPaths::AppDataLocation` and are
invalidated when the ROM/hash paths change.

## Gotchas that have bitten before
- `-fPIC` is mandatory on Linux/BSD for anything linked with Qt (set in `qtui_cfg.lua`). Without
  it copy relocations duplicate Qt metaobjects and dlopened style plugins (adwaita-qt) crash on
  first `show()`. Likewise `-Wl,--exclude-libs,ALL` stops MAME's static zlib/libpng interposing
  over Qt's.
- `qtui_cfg.lua` intentionally does NOT dofile `sdl_cfg.lua`; it replicates the non-SDL parts and
  omits `OSD_SDL`/`SDLMAME_SDL2` so the SDL modules compile as stubs. Don't "fix" that.
- Bundled data (BGFX chain textures, ctrlr, hash, plugins, language) resolves relative to the
  launch cwd. Path fixes belong in mame.ini (e.g. `artpath <extras>;artwork`), not injected at
  launch by the UI; the user wants paths visible in the inis.
- Full-keyboard machines gate MAME's Tab menu behind the "Toggle UI Controls" key (ScrLk).
- Heavy xBR BGFX chains can segfault inside the GPU driver on the worker thread; not our bug.
