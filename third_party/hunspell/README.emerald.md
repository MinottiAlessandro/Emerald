# Vendored Hunspell

Emerald statically compiles this reviewed Hunspell snapshot so normal builds
are offline and do not vary with the host system. `UPSTREAM.json` records the
immutable upstream tag, archive URL, and SHA-256 used to create this directory.

Do not edit the upstream source files directly. Dependency updates are made by
`tools/update_spelling_assets.py` and proposed as reviewable pull requests by
the Spell dependencies workflow. The upstream license files are retained in
this directory and the application-level attribution is in
`THIRD_PARTY_NOTICES.md`.
