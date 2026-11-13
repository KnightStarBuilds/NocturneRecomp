# Nocturne Recomp (KnightStar)

A native C++23 port of Nocturne using SDL2 and OpenGL.

The engine and platform code are tracked directly under `src/`. Builds use that
source and pinned external dependencies; game assets are supplied separately.

## Submodules

This repo uses git submodules. Use the following command:

```
git clone --recurse-submodules https://github.com/KnightStarBuilds/NocturneRecomp.git
```
> For *kstar_gfx* build validation a custom *shadPS4* fork is required which needs a separate clone and build; it isn’t included by recursive submodule initialization.

## Prerequisites

- An owned copy of Nocturne.
- A C++23-capable compiler.
- CMake 3.24+, Ninja and Python 3.9+ with `lzma` support.
- Bash, `make` and `curl`.
- A checkout path without spaces and internet access for the first build.

## Adding Your Game Data

Place the contents of your Nocturne installation in `data/original/` inside the
project. For a distributed build, use the same directory beside the app or
executable. Keep all POD archives and the directory structure intact:

```text
data/original/
  ACT1.POD
  STARTUP.POD
  ... other POD archives ...
  video/
    opening.avi
    ... other media ...
  SYSTEM/
    nocturne.ini
```

POD archives must be directly inside `data/original/`. See the
[asset audit](docs/assets.md) for archive checks and lookup-order details.

## Quickstart (if all prerequisites are met)

```
cd NocturneRecomp
./build.sh --config=Release
./deps/kstar_gfx/build.sh --config=Release
```

## macOS — Apple Silicon

### 1. Setup

Use Apple Clang from Command Line Tools. Install the tools if needed, and finish
that installation before building:

```sh
xcode-select --install
```

Install CMake and Ninja with Homebrew:

```sh
brew install cmake ninja
```

### 2. Build & Run

From the project root:

```sh
./build.sh --config=Release
./run.sh --config=Release
```

The macOS app is `build/macos-arm64/bin/Release/NocturneRecompKnightStar.app`.

For an incremental Debug build:

```sh
./build.sh
./run.sh
```

Add `--clean` to rebuild from scratch, or `--run` to launch after building.

### 3. Distribution

The complete Release `NocturneRecompKnightStar.app` bundle includes runtime libraries,
the fallback font, controller database and license notices. Game assets remain
separate from the bundle and tracked source.

See the [dependency licensing and distribution notes](deps/README.md).

### 4. Locations

Default profile: `~/Library/Application Support/NocturneRecompKnightStar/`.

| Files within the profile | Contents |
| --- | --- |
| `save/` | Manual saves, quicksaves and autosaves |
| `system/nocturne.ini` | Game settings |
| `nocturne_*.log`, `stderr.txt` | Runtime logs |
| `console.txt` | Console log, when enabled |
| `data-directory.txt` | Remembered game-data location |

Use `./run.sh -- --user-dir /path/to/profile` to select a different profile folder.

## PS4 homebrew

An experimental native PS4 target uses the recovered CPU renderer at 640×480.
The console test target is firmware 9.00 with GoldHEN. Building and
packaging are separate from the desktop build:

```sh
./ps4/build.sh --config=Release
./ps4/package.sh --config=Release
```

The package includes the original assets you supplied in `data/original/`.
Scripts, setup instructions, console limitations and validation details are in
[ps4/README.md](ps4/README.md). Outputs remain under `build/ps4-x86_64/`.

Debug and Release installation, native keyboard controls and save persistence
have passed directed console checks. See [validation](docs/validation.md) for
tested scenarios and limitations, and [progress](docs/PROGRESS.md) for remaining
hardware acceptance.

PS4 builds force the CMake option `GAMEPAD_ONLY=ON`. It hides the native cursor,
software pointers and cursor crosshair, and locks the control selector to
Gamepad. Other platforms default to `OFF`; configure with `-DGAMEPAD_ONLY=ON`
to enable the same behavior there.

## Windows and Linux

Native Windows and Linux build, launch, distribution and profile behavior remain
unvalidated. Their toolchain paths are retained; remaining setup and validation
work is tracked in [progress](docs/PROGRESS.md).

## Project details

| Path | Contents |
| --- | --- |
| `src/game/` | Active recovered game and engine implementations |
| `src/include/` | Shared declarations and recovered type layouts |
| `src/shims/` | Platform, renderer, audio and compatibility code |
| `src/main/`, `src/platform/` | Entry point and native startup support |
| `src/checks/`, `src/tests/` | Layout checks and automated tests |
| `src/cmake/`, `src/resources/` | Build helpers and controller resources |
| `deps/` | External dependency pins, licenses and preparation tooling |
| `ps4/` | PS4 cross-build, platform support and homebrew packaging |
| `build/<platform>-<arch>/` | Generated files, dependency build copies and binaries |

Edit implementations in `src/` and rebuild with `./build.sh`. Each recovered
function has one active implementation; source filenames and comments retain
their origin for reference. Dependency build copies and caches are generated
material. See [AGENTS.md](AGENTS.md) for contribution and validation conventions.

The game modules are `cockpit`, `core`, `engine`, `globals`, `shape`, `sound`,
`support` and `wincore`. CMake discovers their source files directly.

- [Progress and remaining work](docs/PROGRESS.md)
- [Validation and known issues](docs/validation.md)
- [License](docs/LICENSE.md)
