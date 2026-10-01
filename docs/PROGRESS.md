# Nocturne Recomp (KnightStar) progress

The Apple Silicon macOS build is implemented and packaged. Gameplay validation
is incomplete; Windows and Linux have not been built or validated. See
[validation.md](validation.md) for test results and observed scene coverage.

## Completed and verified

- [x] Preserve and checksum original assets separately from source and builds.
- [x] Maintain active game and platform source under `src/`, with C++23, CMake,
  Ninja Multi-Config, pinned external dependencies and build/launch scripts.
- [x] Adapt Darwin text streams, executable lookup, asset lookup and user paths.
  Configuration, logs and saves use the user profile.
- [x] Implement OpenGL 4.1 core startup, required entry-point checks, native
  fullscreen, 4:3 presentation and Retina transforms at 640×480. Preserve recovered
  CPU physics and cloth without timestep changes.
- [x] Build ARM64 Debug and Release apps and pass automated tests, including OpenGL
  pixel checks. Verify offline rebuilds and clean cache preservation.
- [x] Bundle runtime libraries, fallback font and notices. Verify bundle identity,
  relative linkage, Finder launch and relocation without Homebrew runtime libraries.
- [x] Play the complete opening movie to the menu and verify PCM reaches SDL.
- [x] Verify manual save/load, quicksave/load, Continue across Debug/Release
  relaunches, movement and normal quit. Observe autosave creation.
- [x] Fix controller Run inversion and cover bindings, stick deflection and
  Always Run with an automated regression test. Verify walk/Run behavior with
  an Xbox controller.
- [x] Render the menu software pointer and suppress the native cursor. Keep the
  dialog pointer steady with Gamepad selected. Cover the SDL cursor contract with
  regression tests and inspect menu/dialog transitions, sensitivity motion and
  gameplay pause/resume captures for pointer artifacts.
- [x] Audit archives, mission/script references and cloth definitions while
  preserving archive priority. Findings and limits are in [assets.md](assets.md).

## Remaining validation

- [ ] Play headquarters and representative missions from every act to completion;
  exercise mission transitions and the remaining mission scripts.
- [ ] Restore an autosave and verify persistence across transitions and relaunch.
- [ ] Check coat/cape motion, skeletal attachments, wind, body/floor collisions
  and movable objects during directed gameplay.
- [ ] Check flashlight correctness, fog, mirrors and in-scene transparency.
- [ ] Listen to opening audio, dialogue and music; check synchronization and
  playback under normal load.
- [ ] Check physical mouse alignment in fullscreen on the Retina display.
- [ ] Validate retained gamepad, automap and other gameplay conveniences.
- [ ] Investigate the [known rendering issues](validation.md#known-rendering-issues):
  the GL texture diagnostic, chapel glass and possible polygon loss after Options.
- [ ] Validate fog and higher internal resolutions before enabling them by default;
  check optional OS-font glyph parity against the original bitmap font.
- [ ] Build and validate Windows and Linux on those platforms.

See [README.md](../README.md) for setup and use.
