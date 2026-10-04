# PS4 homebrew

The PS4 target is a native OpenOrbis cross-build for a jailbroken PS4 running
firmware **9.00 with GoldHEN**. The local installer is a `.pkg`, commonly called
an FPKG. A desktop executable cannot be converted into this installer; build the
PS4 executable first. Debug and Release installation, menu navigation, native
keyboard input and manual-save persistence have been checked on that hardware.
Full runtime and gameplay acceptance remains open; see the
[validation record](../docs/validation.md#ps4-cross-build-validation) and
[remaining work](../docs/PROGRESS.md).

## Build and package

The first build host is Apple Silicon macOS. Install CMake 3.24 or later, Ninja,
Python 3.9 or later, GNU Make, and upstream LLVM with LLD. Homebrew installs LLVM
and LLD separately:

```sh
brew install cmake ninja llvm lld
```

The PS4 build uses upstream LLVM/LLD; desktop macOS builds continue using Apple
Clang through `xcrun`. The pinned OpenOrbis SDK includes Intel macOS host tools,
so Apple Silicon needs Rosetta to execute them. The build script verifies tool
execution before removing any previous build.

Place original game assets in `data/original/`, retaining directories, POD
filenames, and archive order. That directory must contain the game's POD files
directly; select the game installation directory, not its parent. From the
repository root, run:

```sh
./ps4/build.sh --config=Debug
./ps4/package.sh --config=Debug
```

Both commands default to **Debug** when `--config` is omitted. The scripts resolve
the repository from their own location, so absolute script paths also work from
other directories. The build uses Ninja Multi-Config. `ps4/build.sh --clean`
removes only `build/ps4-x86_64/` after validating arguments, tools, and dependency
archives.
Dependency caches, original data, desktop builds, and local investigation notes
remain separate.

Dependencies are downloaded and checked against [the dependency manifest](../deps/manifest.json)
on the first build. Verified source archives and extracted caches live in
`deps/cache/`; disposable dependency build copies live inside the PS4 build
tree. An offline rebuild uses:

```sh
NOCTURNE_OFFLINE=1 ./ps4/build.sh --config=Debug
NOCTURNE_OFFLINE=1 ./ps4/build.sh --config=Release
```

The packager consumes the selected configuration's `nocturne.elf`, `eboot.bin`,
and runtime resources. It never invokes the compiler or downloads tools.
An alternate original-data directory can be selected with
`--data-dir "/path/with spaces/original"`.

| Output | Location |
| --- | --- |
| ELF and PS4 SELF | `build/ps4-x86_64/bin/<Config>/` |
| Runtime resources | `build/ps4-x86_64/resources/<Config>/` |
| Persistent emulator staging | `build/ps4-x86_64/app0/<Config>/` |
| Temporary package staging | `build/ps4-x86_64/stage/<Config>/` |
| Installer | `build/ps4-x86_64/pkg/<Config>/IV0000-NCTR00001_00-NOCTURNEKNIGHT00.pkg` |

The package title ID is `NCTR00001`; application version `01.02` and content ID
are defined in [package.json](package.json). Retain these identifiers when
replacing an installation. The installer includes original assets, the fallback
font, controller resources, license notices, and a manifest of staged file
hashes. It also includes the pinned OpenOrbis startup stubs `sce_module/libc.prx`
and `sce_module/libSceFios2.prx`, which firmware loads before `main` even though
the game uses static musl. Packaging rejects missing or empty startup stubs.
It does not include the console's writable user profile. Packaging stays
local; the scripts do not upload, install, or launch anything on a console.
Install the generated `.pkg` through the console's GoldHEN package installer,
then launch Nocturne Recomp (KnightStar) from the dashboard.

Metadata, PS4 executable headers, required resources, and original POD files are
checked before staging. Symlinks and case-ambiguous package paths are rejected.
Failed generation or package validation preserves the previous successful
installer. Temporary staging is removed on completion or failure.

## Tool overrides

- `PS4_LLVM_DIR`: directory containing upstream LLVM executables. LLD may be
  found on `PATH` if it is installed separately.
- `OO_PS4_TOOLCHAIN`: existing OpenOrbis SDK root containing `lib/crt1.o`.
  Otherwise the verified SDK is prepared in the PS4 build tree.
- `PS4_PKG_TOOL`: absolute path to a host-compatible LibOrbisPkg `PkgTool.Core`
  executable. Otherwise the packager uses the selected SDK's host tool.
- `CMAKE_BUILD_PARALLEL_LEVEL`: positive build job count, default `8`.

No proprietary Sony SDK is used. The package format and tool commands follow
[OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) and
[LibOrbisPkg](https://github.com/maxton/LibOrbisPkg). Their notices and provenance
remain separate from the project's [license](../docs/LICENSE.md).
Additional musl and LLVM runtime notices omitted from the SDK archive are kept
under `ps4/licenses/`, with source URLs and hashes in
[deps/ps4-notices.json](../deps/ps4-notices.json).

The SDK archive declares a 32-bit `mode_t` despite libkernel and its bundled
libc++ using the native 16-bit field. Preparation generates a header overlay in
`build/ps4-x86_64/sdk-include/` without changing the SDK or dependency cache.
Compile-time checks enforce the native 120-byte `stat` layout. SDL, game code,
and FFmpeg use the overlay; a changed overlay rebuilds FFmpeg's disposable
objects to prevent stale file-size and timestamp offsets.

On macOS, `PkgTool.Core pkg_build` accesses system cryptographic services.
A restricted process sandbox can cause `CSSM_ModuleLoad` / `AppleCrypto` errors
even when `PkgTool.Core version` succeeds. Run packaging in a regular terminal
with access to those services. The packager treats these errors as failures and
preserves an existing installer.

## Runtime behavior

The PS4 target presents the recovered CPU renderer at **640×480×32**, scaled to
fullscreen with the original aspect ratio. Desktop OpenGL/window controls are
unavailable. Recovered gameplay, CPU physics, cloth, and simulation timing stay
in the maintained game sources.

PS4 builds always enable `GAMEPAD_ONLY`, including when a CMake cache requests
`OFF`. Native and software mouse pointers, including the cursor crosshair, are
hidden. The control selector remains visible as `Control: Gamepad`, dimmed and
unselectable. Loading old settings, restoring defaults or disconnecting the pad
cannot switch the input mode to keyboard or mouse. Gamepad binding customization
remains available.

Installed original assets resolve to `/app0/data/original/`; fonts, controller
resources, and notices resolve beneath `/app0/`. Saves, settings, and logs use
`/data/NocturneRecompKnightStar/`, independent of the launch directory. This
writable profile is separate from package staging and installed assets.

The SDK clock adapter translates the Linux clock identifiers used by OpenOrbis
musl and its precompiled libc++ into the firmware's BSD identifiers. In
particular, monotonic time uses native clock `4`; native clock `1` measures CPU
time. This preserves elapsed-time semantics for the game's performance counter
and FFmpeg without changing simulation timing. The mapping follows the
[PS4 clock definitions](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/kernel/time.h)
and [clock implementation](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/kernel/time.cpp).
The adapter supports the realtime and monotonic domains used by this build;
other SDK clock identifiers return `EINVAL`.
SDL initializes its elapsed-time epoch on the first tick request if necessary;
repeated initialization preserves that epoch until shutdown. The clock source
and units remain unchanged.
Early startup diagnostics are written to `startup.log` in that user directory;
the game's later stderr output uses `stderr.txt`. A fresh profile selects
Stranger when no hero selection exists. Existing selections are preserved.
Console profiles and installed assets must use regular files and directories;
the filesystem adapter rejects symbolic links and checks path components before
normalizing `..`. Only the emulator's missing root-open operation may fall back
to a real root `stat`; other open errors remain errors. Runtime preparation
rejects a user directory inside original assets and reports failed directory
changes. The adapter tracks successful directory changes because the SDK's
`getcwd` and `realpath` implementations are unsupported.

For console diagnostics, enable GoldHEN's FTP server and KLog TTY redirect.
Use an FTP client or `curl` to list the root directory:

```sh
curl "ftp://<PS4_IP>:2121/"
```

Connect from the same local network to FTP port `2121` for the profile logs and
KLog port `3232` for live launch diagnostics. Capture the kernel log before
launching; `startup.log` is replaced each time `main` runs. The dashboard's
corrupted-application message can indicate `PRX_SCE_MODULE_LOAD_ERROR` for a
missing startup stub. The package icon appearing does not establish that
`main` ran.

The native DualShock 4 backend uses the existing gamepad bindings: Cross confirms
menus, Circle backs out, Options opens/closes the pause menu, and the touchpad
button opens the automap, displayed as **Touchpad** in Controls. **R3** (clicking
the right stick) shows item descriptions. Saved PS-button defaults migrate to
R3 when it is not already assigned to another action; custom bindings remain
intact. Touchpad emits one SDL Back input, so the recovered binding dialog can
capture it. Saved PS4 Touchpad bindings using the former SDL Touchpad code are
migrated to that same input, preserving their assigned actions. Gameplay bindings
remain configurable in Controls.
Text and filename prompts use the PS4 keyboard dialog with the recovered text
limits. Console keyboard input currently accepts Basic Latin characters.
Package metadata sets SFO `ATTRIBUTE=0x2` so native common dialogs use the same
Cross-confirm convention as the game menus.
Normal controller saving uses numbered slots: **NEW SAVE** creates the next
`SLOTnn.NOC`, with no naming or rename prompt. The filename-IME path requires the
separate keyboard Ctrl shortcut used by emulator checks. For controller-accessible
text entry, **Multiplayer → Join Game** opens the server-address keyboard; canceling
that dialog returns before a network connection is started.

The CPU renderer has inherited visual limitations, including reflective effects
that the desktop OpenGL renderer implements separately. Keep desktop pixel and
renderer regression checks when changing shared code. Dashboard launch alone
does not verify rendering, physics, movies, audio, save/load, or campaign progress.

## Validation

### Emulation (shadPS4)

shadPS4 is the first runtime validation gate before physical PS4 testing. These
checks can be completed on any supported host OS: a successful check on Linux,
Windows or macOS closes the corresponding PS4 validation item. Record the host
OS, emulator revision, game build and evidence; testing every host is not required.
Keep observed host-specific failures scoped to that environment. Native desktop
validation and physical PS4 acceptance remain separate.

Start with the Debug game build and retain its unstripped `nocturne.elf` for
source debugging. The validation runner stages the complete package layout and
creates an isolated portable emulator profile:

```sh
./ps4/build.sh --config=Debug
python3 ps4/validate.py --config=Debug --emulator /absolute/path/to/shadps4 \
  --output .local-memory/ps4-debug-menu --scenario menu
```

Choose a new output directory for each run. `--emulator-profile` copies selected
emulator settings and input configuration; `--game-profile` copies a saved game
profile for migration and persistence checks. The copied emulator's home is
redirected inside the output profile, with default and configured user directories
created before launch. Source profiles remain separate.
The runner retains logs, captured frames and profile files beside `result.json`,
which records hashes, command, evidence metadata and exit status. A passing
result establishes capture delivery, scheduled key attempts, a requested quit
and clean exit. Inspect the images and profile evidence to establish scene and
save/load behavior; a scenario name or scheduled key event alone does not
establish gameplay coverage.

The current runner accepts macOS and Linux hosts; its execution has been verified
on macOS. Windows checks can use an equivalent manual workflow with the same
evidence requirements and still complete the general shadPS4 item.

Repeat successful Debug checks with `Release` for release acceptance. For manual
launches, `ps4/package.sh --stage-only --config=Debug` creates persistent app0
staging. `--stage-only` defaults to Debug and accepts `--data-dir`. It uses the
same input checks and file layout as packaging, including fonts, controllers,
startup modules, SFO metadata and hashes. It needs an existing build but no
PkgTool, compiler invocation or downloads. Refresh staging after every rebuild;
rebuilding alone does not refresh it. A failed validation or copy preserves the
previous staging, and a successful refresh removes obsolete files.
`ps4/build.sh --clean` removes these generated directories with the PS4 build tree.

The local validation emulator is shadPS4 0.18.0, revision
`e3ce810f3a653f43ac64ebab63023de281a4103a`, running through Rosetta on macOS.
For this revision, launching from a directory containing `user/` selects a
portable emulator profile. Use a fresh directory under ignored `.local-memory/`
and absolute paths for the emulator and app0; copy settings or game profiles
there when testing migration. Retain the existing emulator profile separately.
Manual profiles also need `General.home_dir` set to their absolute `user/home`
path and home directories for users 1000–1003 plus any configured users; otherwise
this emulator revision may display a save-migration dialog before guest startup.
The runner prepares this layout automatically.
The pinned [CLI](https://github.com/shadps4-emu/shadPS4/blob/e3ce810f3a653f43ac64ebab63023de281a4103a/src/main.cpp)
forwards arguments after `--` verbatim, including `argv[0]`. Supply `nocturne`
before the native diagnostic options. Its `-e` injection does not populate this
executable's static musl environment; use the game's diagnostic CLI instead.
For a manual run from the portable profile directory, create
`user/data/captures/` first, then launch:

```sh
/absolute/path/to/shadps4 -g /absolute/path/to/build/ps4-x86_64/app0/Debug \
  -- nocturne --smoke-dir /data/captures --smoke-seconds 90 \
  --smoke-capture-at 1,5,20,60 --smoke-keys 3:Escape
```

`--smoke-dir` must name an existing absolute guest directory. Duration accepts
1–600 seconds; capture and key schedules accept times from 0–600 seconds. These
native options are handled before the recovered game's command-line parser.

Record the emulator and eboot hashes, build configuration, launch arguments,
profile location, screenshots, logs and exit status for each run. Check visible
rendering, navigation, Touchpad rebinding, fresh/imported profiles, text entry,
gameplay, save/load, relaunch, movies/audio and normal exit. Keep any unsupported
emulator checks explicit. Audio-device initialization and queued PCM alone do
not establish audible output. Resolve emulator-reproducible regressions before
requesting physical PS4 checks.

Recorded Debug and Release runs on macOS cover menu/opening presentation,
Chicago save/load, native filename entry and Touchpad rebinding through the
emulator's ScePad path. Physical controller and reconnect checks in shadPS4,
audible output and broader campaign coverage remain open. See
[shadPS4 results](../docs/validation.md#shadps4-runtime) for exact coverage,
artifacts and host limitations.

For directed input checks, `controller-menu` leaves a bounded session open after
skipping the movie. Host keys mapped by the emulator exercise its ScePad path;
smoke-script keys alone bypass that path. Return to the main menu before the
scheduled quit, since nested binding dialogs can defer it. Inspect both the
visible binding and the copied profile's `system/nocturne.ini` after leaving
Options, then relaunch to check persistence.

`manual-slot-reload` and `manual-slot-ime` require a copied profile whose save
directory contains only `AUTO.NOC` and `SLOT01.NOC`. Inspect the selected Slot 1
row before accepting: other manual saves sort by timestamp and can change the
second row. The IME scenario opens the native save-name dialog after loading;
edit the basename, accept it and verify a distinct `.noc` file. While IME blocks
guest presentation, elapsed time continues and overdue captures wait until
dismissal. Keep the new save outside the source profile and use a fresh copy for
Release confirmation.

#### Debug symbols and LLDB

The checks for a Debug emulator build and for debugging guest movies/rendering
can be completed on any supported host with a suitable debugger and matching symbols.
The commands and limitations below describe the recorded macOS/LLDB workflow;
they do not require resolving that host's limitation to complete a check elsewhere.

The macOS workflow can resolve **Nocturne guest Debug symbols** and stop at early
guest startup. The installed emulator has function symbols but no usable host
source DWARF; a Debug emulator with matching source symbols and sustained guest
movie/rendering debugging remain open. See
[emulator debugging results](../docs/validation.md#emulator-debugging) for the
verified stops and limitations.
The observed guest executable base is `0x400000`; confirm it in each run's
emulator log before loading the symbols. This slide applies to ELF virtual
addresses, not file offsets:

```text
target create --arch x86_64 --no-dependents /absolute/path/to/shadps4
target modules add /absolute/path/to/build/ps4-x86_64/bin/Debug/nocturne.elf
target modules load --file nocturne.elf --slide 0x400000
image lookup -v -n main nocturne.elf
breakpoint set --shlib nocturne.elf --name movie_present --one-shot true
```

For a bounded live investigation, launch the owned emulator with
`--wait-for-debugger`, then attach LLDB to its PID. macOS may require debugger
authorization. The option waits for a native debugger before loading the guest;
it does not start a remote debugging server. shadPS4 handles protected-memory
faults and emulated instructions. The attempted forwarding setup uses both Mach
exceptions and BSD signals, with the Darwin policy set before attaching:

```text
target create --arch x86_64 --no-dependents /absolute/path/to/shadps4
settings set platform.plugin.darwin.ignored-exceptions EXC_BAD_ACCESS|EXC_BAD_INSTRUCTION
process attach --pid <owned-emulator-pid>
process handle -p true -s false -n false SIGSEGV SIGBUS SIGILL SIGVTALRM SIGUSR2
breakpoint set --shlib shadps4 --name 'Core::RunMainEntry(Core::EntryParams*)' --one-shot true --skip-prologue false
continue
```

At `RunMainEntry`, add and relocate the guest ELF as above, then set guest-scoped
breakpoints on `main`, `SDL_TicksInit` or `movie_present` before continuing. Keep
SIGTRAP and SIGABRT stopping normally. Inspect frames and variables first;
evaluating guest function calls through Rosetta is not validated. Allow for
debugger pauses in the run deadline, retain logs, and stop only the owned process
if attachment stalls. This macOS/Rosetta setup still intercepts `EXC_BAD_ACCESS`
at shadPS4's protected GPU output memory during framebuffer clearing, despite
these settings. Detach before framebuffer creation when testing early startup;
the commands above do not establish a working movie/rendering debugging session.

### Automated Tests

Run the isolated script tests without a console or original assets:

```sh
python3 -m unittest discover -s ps4/tests -p 'test_*.py' -v
```

Build-script tests use synthetic verified archives and fake compiler tools. They
cover Debug/Release selection, offline rebuilds, validation before clean, cache
immutability, target-specific preparation, and preservation of desktop output,
game data, and maintained source. Packaging tests cover asset layout and bytes,
POD order, spaces and XML characters, SFO identifiers, invalid inputs, and tool
failures that must preserve an existing package or app0 staging. Validation-runner
fixtures check profile isolation, build/staging identity, bounded process cleanup,
capture evidence and failure reporting. These tests establish script behavior,
not PS4 execution.

PS4 runtime and backend fixtures also run in the desktop Debug/Release CTest
suites. See [cross-build and host-test results](../docs/validation.md#cross-build-and-host-tests)
for package validation, regression coverage, offline rebuilds and cache-preservation
evidence. Native Linux/Windows build and runtime validation remains separate.

### Hardware Validation

On firmware 9.00/GoldHEN, Debug and Release cover installation, main-menu
navigation, Cross-confirm/Circle-cancel native keyboard behavior and manual-save
loading after application relaunch. A Debug save survives Debug package replacement
and installing Release; a separate Release-created save also survives relaunch.
One five-second Release controller disconnect/reconnect succeeds for the same
user, with controls released before disconnecting. Debug, shadPS4, held-input and
repeated reconnect checks remain open. The [physical PS4 results](../docs/validation.md#physical-ps4) record
the tested steps, evidence and remaining acceptance gaps.

A brief ps4debug attach/callback/detach cycle and read-only Debug code checks have
succeeded. Breakpoints, stepping, thread snapshots and sustained attachment remain
unverified. A failed callback attempt was followed by a console freeze; the cause
is unconfirmed. Review the [hardware debugging record](../docs/validation.md#hardware-debugging)
before attempting further debugger work.

[tests/sdk_probe.cpp](tests/sdk_probe.cpp) builds as
`build/ps4-x86_64/bin/<Config>/nocturne_ps4_sdk_probe.elf` for console diagnostics;
its filesystem, clock and thread checks have not run on hardware.

For remaining hardware acceptance, compare software-renderer captures with the
host software build for scenes, shadows, mirrors, and cloth; record performance
without changing simulation timing.
Report the tested campaign sections and inherited defects explicitly. Other
firmware, PS5, GPU rendering, and full campaign certification are outside the
initial validation target.
