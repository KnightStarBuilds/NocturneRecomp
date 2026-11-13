# Nocturne Recomp (KnightStar) progress

The Apple Silicon macOS build is implemented and packaged. An experimental PS4
cross-build and local installer workflow are implemented under `ps4/`. Debug and
Release installation, menu navigation, basic text entry and manual save/load are
verified on the console, including loading across package replacement and
application relaunch. One Release controller reconnect cycle also passes.
Full PS4 hardware acceptance remains unverified; Windows and Linux have not
been built or validated.

## Completed and verified

Detailed results, scope and evidence limits are in [validation.md](validation.md).

- [x] Maintain one active recovered source tree, C++23/Ninja Multi-Config builds,
  pinned dependencies and original assets with preserved POD priority.
- [x] Implement Darwin paths, case-insensitive lookup and text streams, keeping
  writable profiles separate from immutable assets.
- [x] Build and package macOS ARM64 Debug/Release; pass automated tests, offline
  and clean/cache checks, bundled dependency resolution, relocation and Finder launch.
- [x] Implement OpenGL 4.1 core presentation, 4:3 letterboxing, Retina input
  transforms and pixel regression checks without changing recovered physics or cloth.
- [x] Verify desktop opening-to-menu playback and queued PCM, manual/quick saves,
  Continue across Debug/Release relaunches, movement and normal quit.
- [x] Fix controller Run handling and menu cursor artifacts; test the input
  pipeline and inspect menu/dialog transitions. Verify Xbox walk/Run behavior.
- [x] Audit archives, mission/script references and cloth definitions; record
  coverage and limitations in [assets.md](assets.md).
- [x] Cross-build and package PS4 Debug/Release with pinned OpenOrbis dependencies,
  required startup modules, software presentation and offline/cache checks.
- [x] Enforce PS4 controller-only menus and pointer suppression. Cover R3 item
  descriptions, Touchpad mapping/migration and duplicate-binding cleanup with
  production backend and recovered-function regression fixtures.
- [x] Validate PS4 SDL timer initialization, filesystem/path policy and logging
  ownership with host fixtures. Preserve CPU calibration work under optimization and guard its
  same-tick division without changing simulation timing.
- [x] Stage persistent app0 trees and run bounded shadPS4 checks with isolated
  profiles, matching executable hashes, complete capture delivery and clean exits.
- [x] Verify Debug/Release opening-to-menu playback, Chicago save/load, native
  IME edit/accept/cancel and host-key Touchpad rebinding in shadPS4.
- [x] Resolve live Debug guest source/backtraces in `main` and `SDL_TicksInit`
  through LLDB, then detach before rendering and complete captures normally.
- [x] Briefly attach/detach ps4debug on hardware; verify continued menu input,
  matching live Debug code samples and an advancing animation timer.
- [x] Verify PS4 Debug/Release installation, manual save/load and application
  relaunch. Preserve the Debug save across Debug replacement and installation of
  Release; create and reload a separate Release save.
- [x] Set package SFO `ATTRIBUTE=0x2` and test package/app0 metadata in both
  configurations. Verify Cross character selection and Circle cancellation in
  the native keyboard on PS4 Debug and Release.
- [x] Verify one PS4 Release controller off/on cycle with neutral input and the
  same user: input recovers without a game restart, stuck movement or repeated actions.

## Remaining validation — backlog

shadPS4 checks are part of PS4 validation and can be completed on any supported
host OS. A successful check on Linux, Windows or macOS closes that item; repeating
it on every host is not required. Record the host OS, emulator revision, game
build and evidence. Keep observed host-specific limitations scoped to that
environment. Native desktop builds and physical PS4 acceptance remain separate.

- [ ] TODO(Backlog): Play headquarters and representative missions from every act
  to completion; exercise mission transitions and the remaining mission scripts.
- [ ] TODO(Backlog): Verify autosave persistence across mission transitions;
  isolated shadPS4 relaunch and reload are verified.
- [ ] TODO(Backlog): Check coat/cape motion, skeletal attachments, wind,
  body/floor collisions and movable objects during directed gameplay.
- [ ] TODO(Backlog): Check flashlight correctness, fog, mirrors and in-scene
  transparency.
- [ ] TODO(Backlog): Listen to opening audio, dialogue and music; check
  synchronization and playback under normal load. Queued PCM and emulator
  captures do not establish audible output.
- [ ] TODO(Backlog): Check physical mouse alignment in fullscreen on the Retina
  display.
- [ ] TODO(Backlog): Validate retained automap and gameplay conveniences during
  directed play; menu rebinding alone does not establish their in-scene behavior.
- [ ] TODO(Backlog): Investigate the
  [known rendering issues](validation.md#known-rendering-issues): the desktop OpenGL
  texture diagnostic and inherited chapel-glass/polygon-loss reports.
- [ ] TODO(Backlog): Validate fog and higher internal resolutions before enabling
  them by default; check optional OS-font glyph parity against the bitmap font.
- [ ] TODO(Backlog): Build and validate native Nocturne desktop versions on
  Windows and Linux.
- [ ] TODO(Backlog): Validate physical controller buttons, sticks and reconnects,
  plus IME boundary/error cases in shadPS4 before the corresponding PS4 checks.
  Host-key controller mappings cover menu navigation, Touchpad capture and basic
  IME acceptance/cancellation, but do not exercise a physical device. Keep guest
  Debug first and confirm successful checks with Release. The one verified PS4
  Release reconnect does not cover emulator/Debug, held-input or repeated cycles.
- [ ] TODO(Backlog): Set up and validate a Debug build of shadPS4 itself with
  matching source symbols on any supported host OS. Nocturne guest Debug symbols
  alone do not complete this emulator-debugging setup.
- [ ] TODO(Backlog): Verify live guest movie/renderer breakpoints and continued
  execution with a suitable debugger attached to shadPS4 on any supported host.
  The recorded macOS/Rosetta limitation does not block completion elsewhere.
- [ ] TODO(Backlog): Complete PS4 acceptance on firmware 9.00 with GoldHEN beyond
  the verified Debug/Release menus, basic text entry, manual save/load,
  application relaunch, package replacement and one Release reconnect cycle:
  rendering, broader controller/reconnect coverage, text boundaries, movies/audio,
  mission-transition autosaves and power-cycle persistence. Run the hardware
  filesystem/clock/thread diagnostics and check sustained gameplay performance.
  Use shadPS4 as the first gate; record console scene coverage separately from desktop results.

## Standalone graphics library

The lighting/effects implementation and its remaining rendering, packaging and
platform validation work are tracked in
[kstar_gfx progress](../deps/kstar_gfx/docs/PROGRESS.md). The library remains
independent of Nocturne's active renderer; its checks do not close the gameplay,
physics, cloth or campaign acceptance items above.

See [README.md](../README.md) for setup and use.
