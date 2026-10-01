# Nocturne Recomp (KnightStar) validation

Validated on Apple Silicon macOS with Apple Clang 21, C++23 and Ninja Multi-Config.
The OpenGL driver reports `GL 4.1 Metal - 90.5` on
Apple A18 Pro; the application uses OpenGL. Windows and Linux builds remain
unvalidated.

## Build and test results

ARM64 Debug and Release builds succeed, and all 21 CTest suites pass in each
configuration. Offline and clean rebuild checks preserve the external dependency
cache and original assets. An offline Release build from a source-only export
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

The runtime/test compile commands, excluding dependency builds, use
`-std=c++23`, `-fwrapv` and `-fno-strict-aliasing`, with no x86 instruction-set
flags. This does not establish identical floating-point results to the original
x86 binary.

The controller regression test covers stick deflection without overriding the
Run binding or Always Run.
Xbox controller checks confirm walking normally and running while L3 is held.

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

Bounded scripted captures and Release LaunchServices runs exited normally.
Recorded observations include:

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
- Headquarters created `AUTO.NOC`; restoring it has not been tested. Cloth assets
  reached the recovered loaders, but detailed motion and collision behavior remain
  unverified. The flashlight toggle was delivered without a conclusive visual result.

No full mission or campaign has been completed during testing. Remaining gameplay,
audio and physical-input checks are tracked in [PROGRESS.md](PROGRESS.md#remaining-validation).
Evidence is local and ignored under `.local-memory/`; it is not part of a clone.

## Known rendering issues

- The nonfatal `GLD_TEXTURE_INDEX_2D is unloadable` diagnostic appeared in tested
  3D sessions. Binding texture zero for untextured draws is a possible explanation,
  but neither the cause nor its visual impact is confirmed.
- Upstream research reports overbright chapel glass from duplicate draws and
  possible polygon loss after entering Options. These have not been independently
  reproduced or repaired here.
- Fog remains disabled by default. macOS uses the validated 640×480 framebuffer;
  higher resolutions remain experimental pending checks against fixed-size arrays.
- Fallback font packaging passes; exact optional OS-font glyph parity is unverified.

The recovered CPU physics and cloth solver retain their existing algorithms and
timestep behavior.
