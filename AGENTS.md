# Project instructions

Nocturne Recomp (KnightStar), C++23 with SDL2 and OpenGL. Apple Silicon macOS
is the first target; preserve Linux and Windows paths and report their
validation separately. Preserve recovered gameplay, CPU physics and cloth.

## Build and source

- Require CMake 3.24+, C++23 without extensions, and Ninja Multi-Config. Select
  Debug/Release with `--config`; default to incremental Debug.
- Preserve `./build.sh [--config=Debug|Release] [--clean] [--run]` and
  `./run.sh [--config=Debug|Release] [-- arguments]`. Validate before mutation.
- Use Apple Clang through xcrun on macOS. Keep required integer wrap and aliasing
  semantics in both configurations. Do not add x86 flags to ARM64 builds.
- Maintain the active source directly in src/: recovered modules in src/game/,
  declarations in src/include/, compatibility code in src/shims/, startup in
  src/main/ and src/platform/, tests in src/checks/ and src/tests/, build helpers
  in src/cmake/ and controller resources in src/resources/.
- Keep one active implementation of each recovered function. Edit that file
  directly and retain its identifying filename and provenance comments. CMake
  discovers source files by module; keep new files in their owning module.
- Keep external dependency records in deps/ and outputs in build/<platform>-<arch>.
  Clean only that build tree, preserving dependency caches, game data and ignored
  local investigation notes.
- Pin external dependency archives, SHA256 hashes, provenance and licenses.
  Cached external sources are immutable; generated dependency build copies
  contain compiler output. Keep original game assets outside tracked source.
- Preserve POD archive order; do not flatten assets into one directory.

## Implementation and documentation

- Keep compatibility changes narrow. Preserve upstream conveniences and recovered
  physics; do not replace the solver or change simulation timing speculatively.
- Keep concise comments explaining ownership, coordinate conventions, platform
  workarounds and test intent. Preserve attribution, licenses and directives.
- Track current setup, architecture, limitations and validation procedures.
  Put transient investigation logs in ignored .local-memory/, outside build/.
- Match each document's existing language and structure. State verified behavior
  directly; omit conversation history, user/agent attribution and per-event date
  narration.
- Keep user data separate from immutable game assets. Finder launch must not
  depend on the working directory. Bundle runtime dependencies and fallback font.

## Validation

- For runtime/build changes build Debug and Release and run relevant tests in
  both configurations. Check offline rebuild and clean cache preservation.
- Cover Darwin text streams, case-insensitive lookup, paths with spaces, Retina
  input mapping and bundled dependency resolution with meaningful tests.
- Keep OpenGL pixel checks and renderer regression tests. Startup is not proof of
  rendering, physics, media, save/load or campaign correctness.
- Document unavailable checks, campaign coverage and inherited defects honestly.
  Do not claim Windows/Linux validation from a macOS build.
