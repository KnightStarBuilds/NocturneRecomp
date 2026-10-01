# Original assets and audit

After [adding your game data](../README.md#adding-your-game-data), run this audit
from the repository root:

```sh
python3 src/tools/audit_assets.py --data-dir data/original \
  --output .local-memory/asset-audit.json
```

The tool reads assets in place and writes its JSON report under ignored
`.local-memory/`. It runs with Python 3.9+ without building the game. The report
records archive SHA256 hashes, mount order, duplicate payloads, mission references
and cloth definitions. Hashes describe the supplied files; comparing them with a
previous report can detect changes.

The installation tested for this port produced the following results. Other
editions or modified installations may contain different files and counts.

| Asset measure | Count |
| --- | ---: |
| POD archives | 41 |
| Archive entries | 12,383 |
| Unique case-insensitive paths | 12,101 |
| Paths repeated across archives | 179 |
| Additional copies at those paths | 282 |
| Missions and matching scripts | 28 each |
| Cloth definitions | 25 |

In that installation, forty archives use POD2 and `tground.pod` uses POD1; no
`pod.ini` is present. All 179 repeated paths have identical payloads. The runtime
uses filename-byte sorting, stable mount priorities and first-match
lookup. The auditor models these rules, including `pod.ini` selection and POD3
dependencies where present. Keeping archives separate preserves that lookup
behavior; flattening them discards their ordering.

The tested mission headers are version 7 with actor version 4. Every mission has
its script and referenced sets, and every cloth definition has its mesh. Cloth
versions are distributed as one version 1, thirteen version 2 and eleven version
3. The recovered loader replaces the physics parameters read from versions 1
and 2 with its legacy defaults. The port retains those overrides and the CPU
solver.

The audit checks archive bounds, mission headers and nonempty scripts, set/mesh
presence, cloth headers and nonnegative locked-vertex lists. It examines archived
assets in the selected directory; loose-file and user-profile overrides are not
included. It does not execute scripts, deserialize every actor, validate cloth indices
against mesh vertex counts, resolve skeleton bone names or simulate collisions.
Gameplay, media and persistence results are recorded separately in
[validation](validation.md).
