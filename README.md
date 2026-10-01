# Nocturne Recomp (KnightStar)

A native C++23 port of Nocturne using SDL2 and OpenGL.

The engine and platform code are tracked directly under `src/`. Builds use that
source and four pinned external dependencies; game assets are supplied separately.

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

## Windows

### 1. Setup

TODO: Document supported versions, toolchain and required packages.

### 2. Build & Run

TODO: Add verified build and launch instructions.

### 3. Distribution

TODO: Document packaging and distribution after validation.

### 4. Locations

TODO: Verify and document save, configuration and log locations.

## Linux

### 1. Setup

TODO: Document supported distributions, compilers and required packages.

### 2. Build & Run

TODO: Add verified build and launch instructions.

### 3. Distribution

TODO: Document packaging and distribution after validation.

### 4. Locations

TODO: Verify and document save, configuration and log locations.

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
