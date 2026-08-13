# Spelling dependency maintenance

Emerald's normal configure, build, test, and packaging paths are fully offline
with respect to spelling. They compile one reviewed Hunspell snapshot from
`third_party/hunspell` and embed the reviewed US-English files from
`resources/dictionaries/en_US`.

The files remain independently licensed third-party work even though Emerald
hosts them. Their upstream origin, immutable revision, archive hash, and license
texts are preserved in `third_party/hunspell/UPSTREAM.json`,
`resources/dictionaries/manifest.json`, and `THIRD_PARTY_NOTICES.md`.

## Verify the committed snapshots

This command is offline. It validates the manifest schema and release version,
the complete supported-language set, safe asset names, size ceilings, every
dictionary SHA-256, and the required Hunspell source and license files:

```bash
python3 tools/update_spelling_assets.py verify
```

The security workflow runs the same verification whenever spelling assets,
Hunspell sources, the maintenance script, or CMake integration changes.

## Review upstream updates

`.github/workflows/spell-dependencies.yml` checks the official Hunspell release
API and the LibreOffice dictionaries `master` revision every Monday. It runs:

```bash
python3 tools/update_spelling_assets.py update
```

If upstream changed, the workflow updates the source/data, attribution files,
manifest hashes, and provenance metadata on
`automation/spell-dependencies`, then opens or refreshes a pull request. It
never merges, tags, publishes, or changes user installations by itself.

Review that pull request like source code: inspect upstream release notes and
license changes, read the vendored diff, and require the full build, security,
editor, and performance checks. Dictionary content changes automatically bump
the manifest's patch-level pack version; a Hunspell-only update does not.

## Publish optional packs

Optional dictionaries are committed under `packaging/spelling-packs/v1` for
auditability but are not embedded in Emerald. After merging an approved update,
create the exact tag recorded as `releaseTag` in the manifest, for example:

```bash
git tag -a spell-dictionaries-v1.0.0 -m "Emerald spelling dictionaries 1.0.0"
git push origin spell-dictionaries-v1.0.0
```

`.github/workflows/publish-spelling-packs.yml` checks that the tag and manifest
match, re-verifies the committed data, flattens the four packs, generates
`SHA256SUMS`, and creates a new Emerald GitHub release. It refuses to overwrite
an existing release. Publish the matching pack before shipping an Emerald build
whose embedded manifest refers to it.

To inspect the exact release payload locally:

```bash
python3 tools/update_spelling_assets.py package /tmp/emerald-spelling-pack
cd /tmp/emerald-spelling-pack
sha256sum --check SHA256SUMS
```

At runtime Emerald initiates downloads only from the versioned Emerald release
URL encoded in the embedded manifest (GitHub may redirect an asset to its HTTPS
delivery host). It caps each response before buffering, checks its SHA-256
again, validates the dictionary with Hunspell in a temporary directory, and
only then atomically activates it outside the user's vault.
Existing optional packs are checked against the current embedded hashes. An
unchanged pack remains usable across catalog releases; changed or corrupted
content appears as **Update available** and is replaced through a staged rename
with rollback rather than being overwritten in place.
