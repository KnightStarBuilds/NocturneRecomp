# Nocturne Recomp (KnightStar) license

The original adaptation code, build and dependency tooling, tests and
documentation written for this project are provided under the BSD Zero Clause
License (0BSD):

Permission to use, copy, modify, and/or distribute this software for any purpose
with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT,
OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

This grant does not extend to recovered Nocturne program code, exported types,
binary metadata, original game assets or executables. Keeping recovered code in
`src/` does not change its licensing scope.

## Source provenance

The source derives from
[NocturneDecomp revision `872d1d7ce186375df791d12da938a447f51b3216`](https://github.com/NearlyTRex/NocturneDecomp/tree/872d1d7ce186375df791d12da938a447f51b3216).
The corresponding
[source archive](https://codeload.github.com/NearlyTRex/NocturneDecomp/tar.gz/872d1d7ce186375df791d12da938a447f51b3216)
has SHA256
`49f2222f62fcb20f5e250ffb98c192abf0f050a488c2bc7c005504a8b1d72159`.
These identify the source's origin; builds compile the checked-in implementations.

The exact upstream notice is preserved in
[NocturneDecomp-LICENSE](NocturneDecomp-LICENSE). Its distinction between original
project work and recovered material applies to the corresponding paths here:

| Current path | Origin and scope |
| --- | --- |
| `src/game/` | Selected implementations from `annotations/nocedit.exe/pseudocode/src/`; recovered program code, with no license grant from NocturneDecomp |
| `src/include/` | Exported declarations and types from `annotations/nocedit.exe/pseudocode/include/`; recovered material, with no blanket license grant |
| `src/checks/` | Layout checks derived from the recovered types; retain the same distinction from original project code |
| `src/shims/`, `src/main/` | Original compatibility and entry-point code from the matching `annotations/nocedit.exe/pseudocode/` directories; covered by the upstream 0BSD notice |
| `src/cmake/`, original code in `src/tests/` | Original upstream build helpers and tests plus project adaptations; covered by their original-work 0BSD grants |
| `src/platform/`, `src/tools/`, project scripts and documentation | Original adaptation code under the 0BSD grant above |
| `src/resources/` | Controller database and its accompanying notice; retain that notice |

## Dependencies and distribution

Other dependencies retain their own licenses. The
[dependency manifest](../deps/manifest.json) records their licenses and notice
paths within the source archives. The macOS bundle copies upstream and dependency
notices into `Contents/Resources/licenses/`. Game data stays outside tracked source
and the macOS application bundle. The PS4 installer includes the supplied
original assets and carries notices under `licenses/`; packaging does not change
their licensing scope. Additional PS4 runtime notice provenance is recorded in
[ps4-notices.json](../deps/ps4-notices.json).

Static FFmpeg distribution additionally requires LGPL-compliant relinking
materials; retain corresponding source, any modifications and relinkable game
object files when preparing a distributable binary.
