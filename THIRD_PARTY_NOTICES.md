# Third-party notices

Emerald itself is licensed under the MIT License. The following independently
licensed components and data are used by the spell-checking feature.

## Hunspell 1.7.3

Emerald builds the vendored, unmodified official Hunspell 1.7.3 sources as a
private static library. The corresponding source and license texts are shipped
in `third_party/hunspell`, making normal builds independent of external hosts.

- Project: https://github.com/hunspell/hunspell
- Source archive: https://github.com/hunspell/hunspell/archive/refs/tags/v1.7.3.tar.gz
- Archive SHA-256: `933be3dac6fd55f6e752331a170efb7e33800e40fae1156d8434cc8c85379a1b`
- License: MPL 1.1 / GPL 2.0 / LGPL 2.1 tri-license, at the recipient's option

Exact machine-readable provenance is in `third_party/hunspell/UPSTREAM.json`.
Emerald does not modify the Hunspell source files.

## Bundled English (US) dictionary

The `en_US` Hunspell dictionary is derived from SCOWL and Ispell and comes from
the LibreOffice dictionaries repository. Its upstream `README_en_US.txt`,
including copyrights and license terms, is embedded with the dictionary and is
extracted beside it as `NOTICE.txt` when Emerald first uses spell checking.

- Source commit: `f2ff99058268502bdcf4cad25c1ca2935ad8aa7d`
- Source: https://github.com/LibreOffice/dictionaries/tree/f2ff99058268502bdcf4cad25c1ca2935ad8aa7d/en

## Optional language dictionaries

Italian, German, French, and Spanish dictionaries are not embedded in Emerald's
application package. Reviewed snapshots are mirrored as immutable, versioned
assets on Emerald's own GitHub releases. If requested by the user, Emerald
downloads the selected pair and notice, verifies every file against the
manifest and SHA-256 digest embedded in that application version, and saves the
notice beside the installed pair. The mirrored source files and release input
live in `packaging/spelling-packs`; each dictionary remains governed by the
license shown in Emerald's language manager and its downloaded `NOTICE.txt`.
