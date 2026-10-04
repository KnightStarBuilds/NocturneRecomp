# Nocturne Recomp (KnightStar) validation

Validated on Apple Silicon macOS with Apple Clang 21, C++23 and Ninja Multi-Config.
The OpenGL driver reports `GL 4.1 Metal - 90.5` on
Apple A18 Pro; the application uses OpenGL. Windows and Linux builds remain
unvalidated.

## Build and test results

ARM64 Debug and Release builds succeed offline, and all 39 CTest suites pass in
each configuration, including native-display and renderer checks.
Offline and clean rebuild checks preserve the external dependency cache and
original assets. An offline Release build from a source-only export
with a prepared external dependency cache also passes. Runtime and test targets
compile the tracked source under `src/`. Direct edits persist through dependency
preparation and the edited translation unit recompiles successfully offline.

Both bundles pass display-name, identifier, executable, ARM64, relative-linkage,
ad-hoc signature, bundled-font and relocation checks. No linked Homebrew runtime
libraries remain. Finder/LaunchServices runs reached the menu and closed normally
from `/` with a system-only `PATH`, including a relocated app whose path contains
spaces and whose profile remembers the selected assets.

| Area | Checks |
| --- | --- |
| Renderer logic | Vertex conversion, batching, texture/state mapping, recorded GL calls, exports and golden traces |
| Native OpenGL | Core context, required entry points, production shaders, framebuffer creation, pixel orientation, depth preservation/occlusion and alpha blending |
| Presentation input | 4:3 letterboxing, Retina scaling, odd padding and inverse cursor recentering |
| Menu cursor | Native cursor visibility and requests at startup, motion, window entry and focus regain; preserved mouse coordinates through the SDL message bridge |
| Controller input | Real binding pipeline with simulated SDL input: partial/full stick movement, Run bindings, Always Run, analog speeds and input ownership |
| Filesystem | Paths with spaces, case folding, POD priority, asset overlays, write separation and configuration persistence |
| Text streams | Binary passthrough, CRLF translation, physical POD offsets and seek/tell across buffer boundaries |
| Scripts | Argument validation, quoting, cache preservation and launcher fixtures for each OS |
| PS4 host checks | Software-renderer settings, package/user path separation, component traversal and symlink rejection, SDK file layout, clock mapping, SDL timer initialization, dialog text limits, framebuffer scaling, controller axes and recovered key capture |

The runtime/test compile commands, excluding dependency builds, use
`-std=c++23`, `-fwrapv` and `-fno-strict-aliasing`, with no x86 instruction-set
flags. This does not establish identical floating-point results to the original
x86 binary.

The controller regression test covers stick deflection without overriding the
Run binding or Always Run.
Xbox controller checks confirm walking normally and running while L3 is held.
The PS4 variant exercises R3 item descriptions through the recovered inventory
input path, Touchpad map naming, and migration of saved PS-button defaults while
preserving custom bindings and existing R3 assignments. A separate fixture
compiles the native PS4 backend and real recovered binding dialog: Touchpad
produces one capturable input, saved SDL Touchpad codes migrate to SDL Back while
retaining their actions, and duplicate bindings and held/released input are
handled by the real capture path. Desktop PlayStation defaults and labels are
checked separately with simulated SDL input.

`GAMEPAD_ONLY` tests compile the recovered cursor and control-menu functions
with the flag both enabled and disabled. They cover native cursor visibility
through focus changes, all software pointer shapes and the crosshair, the locked
and dimmed Gamepad selector, navigation around it, INI load/save, defaults and
calibration. The real controller helper verifies migration of keyboard-only
profiles while preserving custom pad/mixed bindings. Mouse coordinates remain
unchanged. A PS4 configure request with `-DGAMEPAD_ONLY=OFF` still produces an
`ON` cache value and `GAMEPAD_ONLY=1` in every game, shim and startup compile
command.

## PS4 cross-build validation

### Cross-build and host tests

OpenOrbis 0.5.4 with upstream LLVM/LLD on Apple Silicon macOS produces Debug and
Release x86-64 ELF/SELF binaries and validated LibOrbisPkg installers. Python
build and SDK fixtures pass; the current 31 packaging and 14 validator tests pass
in both host configurations. An actual offline PS4 Debug clean rebuild preserves
cache and original-asset bytes, modes and symlink targets; Release also builds
offline. Other PS4 build hosts have not been tested. See the
[PS4 build and packaging instructions](../ps4/README.md).

The host fixtures cover packaging/staging rollback, asset layout, SDK modules,
symlink/component errors, writable-profile containment, failed directory changes,
log-stream ownership, SDL timer initialization and recovered controller capture.
Reintroducing each timer, duplicate mapping or saved-binding migration defect
independently fails its regression. Both configurations bundle the pinned
`sce_module/libc.prx` and `sce_module/libSceFios2.prx` bytes and reject missing or
empty modules before packaging.

The recovered CPU calibration retains its five benchmark loops under optimization
and guards same-tick measurements, preventing a Release startup divide-by-zero.
Tests cover ordinary, zero and wrapping intervals; optimized PS4 assembly retains
the work and guarded divisions. The game clock and physics are unchanged.

Package and app0 metadata tests enforce SFO `ATTRIBUTE=0x2`, selecting Cross for
native common-dialog confirmation. The former zero value fails all four
Debug/Release package/app0 cases. The corrected builds retain identical ELF/SELF
binaries; 119 of 120 package payload files are unchanged and only `sce_sys/param.sfo`
differs. Both installers pass package validation and full FTP SHA256 readback
before installation.
Host tests and a successful cross-link alone do not establish console behavior.

### shadPS4 runtime

shadPS4 is the first runtime gate before physical PS4 acceptance. Checks may pass
on any supported host OS; record the host, emulator revision, game configuration
and evidence without requiring repetition on every host. Use the Debug game and
matching unstripped ELF first, then confirm with Release. Native desktop checks
and physical PS4 results remain separate. Procedures are in the
[emulation workflow](../ps4/README.md#emulation-shadps4).

The tested emulator is shadPS4 0.18.0, revision
`e3ce810f3a653f43ac64ebab63023de281a4103a`, on macOS ARM64 through Rosetta.
A runner pass establishes valid capture delivery, scheduled key attempts,
requested quit and clean exit; inspected captures establish the visible scene.

| Check | Verified result |
| --- | --- |
| Main menu | Debug and Release each deliver four valid 640×480 captures and exit cleanly; inspected images show menu labels and moon animation with the corrected package metadata. |
| Complete opening | Seven captures and clean exit in each configuration. Debug presents 1,527 frames and drops none over 101.785 s; Release presents 1,521 and drops six over 101.772 s. Captures show movie playback and arrival at the menu. |
| Save/load | Debug creates `AUTO.NOC` and `SLOT01.NOC` in Chicago. Release loads the copied autosave; directed Debug/Release runs select manual Slot 1 and restore the dock scene. Copied profiles retain save timestamps. |
| Filename IME | Debug/Release accept an edited basename and create distinct `SLOT01GG.noc` saves through the Ctrl+F2 path. Captures show the entered text and return to gameplay. |
| Controller binding | Host keys routed through ScePad bind Touchpad to Forward in Debug (`keyWalk=356`, `keyMap=599`). Release imports it, then reassigns Touchpad to Back (`keyBackup=356`, `keyWalk=599`), confirming persistence and duplicate cleanup. |
| Server-address IME | Both configurations open, edit and cancel the dialog before a network connection begins. |

These are emulator results. Physical host-controller reconnects, audible output,
synchronization and mission completion are not established by these runs. A
separate bounded macOS software-renderer run reaches Chicago at 640×480×32 and
creates `AUTO.NOC`; its inspected frames provide a host reference for PS4 comparisons.

### Emulator debugging

Apple LLDB resolves Debug guest source and backtraces at the observed `0x400000`
load slide, stops in `main` with `argc=9`, and inspects `SDL_TicksInit` before and
after epoch initialization. Detaching before framebuffer creation permits captures
and clean exit. Guest symbols do not provide a Debug emulator: the installed
shadPS4 lacks usable host source DWARF.

Continuous renderer/movie debugging remains unverified. On the tested
macOS/Rosetta setup, LLDB intercepts `EXC_BAD_ACCESS` in shadPS4's protected GPU
output memory despite exception-forwarding settings. Movie playback succeeds
without attachment. Equivalent successful debugging on another host completes
the general check without requiring this local issue to be resolved. See the
[LLDB procedure](../ps4/README.md#debug-symbols-and-lldb).

### Physical PS4

Firmware 9.00 with GoldHEN is the tested hardware target. Directed console checks
establish the results below; they do not certify full gameplay or every control.

| Area | Verified result | Scope |
| --- | --- | --- |
| Installation and menus | Debug and Release install, launch and navigate the main menu. Corrected Debug replaces the existing package; Release installs over Debug. | Current title/version on firmware 9.00. |
| Debug save | Slot 2 restores the saved position in-session, after application relaunch, after Debug package replacement and in Release after installation/relaunch. | Retrieved `SLOT02.NOC` is 34,105 bytes with the expected compressed header. Debug load logs reference `HQ.SET` and `HQ-ACT1.scr`. |
| Release save | **NEW SAVE** preserves Slot 2 and creates a separate save that restores the position after moving away and after application relaunch. | New slot number and scene were not recorded; no file or logs retrieved for this save. |
| Native keyboard | Cross selects characters and Circle cancels in the server-address keyboard, matching the game menus and system UI. | Corrected Debug and Release packages; text limits, submission and error cases remain unverified. |
| Controller reconnect | A controller turned off for five seconds and reconnected to the same user recovers menu navigation, Confirm/Back, movement, turning and pause without restarting, stuck movement or repeated actions. | One Release cycle, paused with controls released before disconnect; no Debug/shadPS4, held-input or repeated-cycle result. |

Normal controller saving uses numbered slots without filename entry; the emulator
filename-IME checks use the separate Ctrl+F2 path. Manual-save success does not
establish mission-transition autosaves or persistence across a console power cycle.

Kernel logs establish that omitting Fios2 terminates startup before `main` with
`PRX_SCE_MODULE_LOAD_ERROR` (`0xa0020102`) and a corrupted-application message.
Both startup stubs are now bundled. Retrieved Debug load logs contain no new
blocking diagnostic. The existing 8-bit surface message is covered under
[known rendering issues](#known-rendering-issues).

Remaining hardware acceptance includes movies and audible audio/synchronization,
representative missions and transitions, directed rendering/physics checks,
sustained performance, broader controller/text coverage and the filesystem,
clock and thread diagnostics in `ps4/tests/sdk_probe.cpp`. The
probe writes `sdk-probe.log`; the historical `runtime-probe.log` does not establish
coverage of these checks. The [backlog](PROGRESS.md#remaining-validation--backlog)
tracks the remaining work.

### Hardware debugging

A brief GoldHEN ps4debug v1.1.19 attachment establishes the reverse callback and
detach acknowledgment. Menu animation/input continue afterward; three live code
samples match the retained Debug ELF, and the menu timer advances with SDL's timer
initialized to a stable epoch. Breakpoints, stepping, thread snapshots and
sustained attachment remain unverified. A failed callback attempt was followed
by stale debugger state and a console freeze requiring restart; the cause is
unconfirmed.

## Repeatable checks

From the repository root, build Debug and Release using the
[README instructions](../README.md), then run:

```sh
ctest --test-dir build/macos-arm64 -C Debug --output-on-failure
ctest --test-dir build/macos-arm64 -C Release --output-on-failure
```

The `native-display` test label requires WindowServer access. A sandbox without
display access cannot run that test; the remaining tests can run without it.
Renderer test registration is maintained in `src/tests/renderer.cmake`.

See [dependency checks](../deps/README.md) for offline/cache verification and
[the asset audit](assets.md) for archive, mission and cloth checks.

## Integration captures

The smoke runner starts its own game process, injects scheduled SDL keys, captures
the GL backbuffer and records PCM queued to the movie audio device. Choose a new
`--output` directory for every run; an existing directory is rejected. By default,
the runner creates a fresh profile under that directory. Use `--user-dir` to reuse
a test profile for save/relaunch checks.

```sh
python3 src/tools/smoke_run.py \
  --app build/macos-arm64/bin/Release/NocturneRecompKnightStar.app \
  --data-dir data/original --output .local-memory/opening-check --scenario opening
```

A successful run requires a zero exit status, no timeout and at least one captured
frame. Inspect the captures to verify the intended scene; queued PCM does not
establish audible output. Scenarios and key schedules are in
[smoke-scenarios.json](../src/tests/smoke-scenarios.json).

Use `--mouse-at '8:320:240,10:400:260'` to schedule motion in logical framebuffer
coordinates without warping the physical pointer. `result.json` records SDL's
native cursor visibility at each capture (`0` hidden, `1` shown); the GL images
exclude the native cursor. These checks do not establish physical Retina alignment.

## Observed gameplay coverage

The desktop macOS bounded scripted captures and Release LaunchServices runs
exited normally. These observations do not establish PS4 gameplay coverage:

| Scene | Configuration | Observed |
| --- | --- | --- |
| Headquarters (`hq-act1`) | Debug | Intro cameras, characters, shadows, cloth assets and autosave creation |
| Act I (`forest`) | Release | Forest scene, Stranger coat, Svetlana cape/hair, rain and normal quit |
| Act II (`town`) | Release | Cinematic, characters, lit street, shadows and normal quit |
| Act III (`chicago1`) | Debug and Release | Dock scene, manual/quick save-load, Continue after relaunch, movement and normal quit |
| Act IV (`mansion`) | Release | Basement/prison scene, characters, shadows, background and normal quit |

- The opening movie played through to the menu in a 112.33-second Debug capture.
  Its log records 1,515 presented frames and 12 dropped while Release compiled.
  SDL received 8,978,760 PCM samples, of which 8,573,412 were nonzero. Frame-perfect
  playback and audio synchronization under normal load remain unverified.
- In Chicago, F2/F3 created and loaded `SLOT01.NOC`. Release Continue loaded the
  Debug profile; F6 and F9/Y created and restored `quicksavedgame.noc`.
- Closing during video, gameplay and the Volume picker completed normally.
- Debug and Release Chicago captures verified menu, pause and resume transitions
  with normal quit. Release Main, Options, Controls and sensitivity captures showed
  the software pointer without trails or a pointer left behind in child dialogs.
  SDL reported the native cursor hidden at every sampled capture. The sensitivity
  pointer retains the existing clipping to its dialog.
  With Gamepad selected, dialog pointers stay white; Keyboard and Mouse controls
  retain the original color cycle.
- Desktop headquarters created `AUTO.NOC`; restoring that particular save has
  not been tested. Cloth assets reached the recovered loaders, but detailed motion
  and collision behavior remain unverified. The flashlight toggle was delivered
  without a conclusive visual result.

No full mission or campaign has been completed during testing. Remaining gameplay,
audio and physical-input checks are tracked in [PROGRESS.md](PROGRESS.md#remaining-validation--backlog).
Evidence is local and ignored under `.local-memory/`; it is not part of a clone.

## Known rendering issues

- The nonfatal `GLD_TEXTURE_INDEX_2D is unloadable` diagnostic appeared in desktop
  OpenGL 3D sessions; it is not a PS4 software-renderer result. Binding texture zero
  for untextured draws is a possible explanation, but neither the cause nor its
  visual impact is confirmed.
- PS4 logs contain `CreateSurface: OUT_OF_MEMORY` for an 8-bit surface. A host
  reproduction with the same 32-bit channel masks returns SDL `Unknown pixel format`
  at depth 8 and succeeds at depth 32; the wrapper labels every creation failure
  as out of memory. Console logs also show successful 32-bit creation, so this
  nonfatal message does not establish memory exhaustion.
- Upstream research reports overbright chapel glass from duplicate draws and
  possible polygon loss after entering Options. These have not been independently
  reproduced or repaired here, including on PS4.
- Fog remains disabled by default. macOS uses the validated 640×480 framebuffer;
  higher resolutions remain experimental pending checks against fixed-size arrays.
- Fallback font packaging passes; exact optional OS-font glyph parity is unverified.

The recovered CPU physics and cloth solver retain their existing algorithms and
timestep behavior.
