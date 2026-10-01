# External dependencies

[manifest.json](manifest.json) records archive URLs, SHA256 digests, versions,
vendored components and licenses. Its `license_files` entries identify notices
inside each dependency's source tree; they are descriptive metadata.

The four external dependencies are SDL2, SDL_ttf, FFmpeg and Liberation fonts.
SDL_ttf's release archive also supplies its vendored FreeType and HarfBuzz sources.
The game source is tracked directly in `src/`; it is not downloaded by dependency
preparation.

[prepare.py](prepare.py) verifies archives and extracted sources in ignored
`deps/cache/`, then copies them into
`build/<platform>-<arch>/dependency-sources/`. Cached sources are immutable and
never used for compiler output. Edit project code in `src/`.

Cache provenance records archive hashes, source contents, executable bits and
symlink targets. Changed pins replace the extracted source; modified cached
sources fail verification. The cache contains both archives and extracted trees.

From the project root, rebuild without downloading dependencies or verify the
cache alone (the Apple Silicon build directory is shown):

```sh
NOCTURNE_OFFLINE=1 ./build.sh
python3 deps/prepare.py --build-dir build/macos-arm64 --offline --verify-cache-only
```

Cache-only verification leaves generated build sources untouched; it may recreate
missing extracted sources or provenance records from verified archives.
`build.sh --clean` preserves the dependency cache, game data and local notes.

SDL2 and SDL_ttf build as shared libraries. SDL_ttf uses its vendored FreeType;
HarfBuzz shaping is disabled by default. FFmpeg builds as static libraries with
optional external libraries, GPL components and nonfree components disabled.
The macOS bundle includes Liberation Mono Regular at
`Contents/Resources/fonts/Fallback.ttf` and copies license notices directly from
prepared sources into `Contents/Resources/licenses/`. The copy list is maintained
in [bundle_macos.cmake](../src/cmake/bundle_macos.cmake).

The controller database and its notice are tracked in `src/resources/`. The
NocturneDecomp notice is preserved at
[docs/NocturneDecomp-LICENSE](../docs/NocturneDecomp-LICENSE).
See [the license and distribution notes](../docs/LICENSE.md) for source provenance
and the distinction between original project code, recovered code and game assets.
