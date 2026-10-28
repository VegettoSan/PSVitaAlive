# PSVitaAlive self-update versioning

## Current rule

The PS Vita client must use the GitHub Release `tag_name` as the only source of truth for the remote client version.

Release `name` and `body` are free-form human-readable metadata. Numbers in release notes (file sizes, RAM amounts, years, issue IDs, counts, etc.) must never affect update detection.

Valid examples include tags such as:

- `02.04`
- `v02.04`
- `1.2.3`
- `BETA-0.1`

If a tag does not contain exactly one dotted numeric version, the client must fail the update check safely instead of guessing a version from other release metadata.

## Reason

Before this rule, the updater collected version-like numbers from `tag_name`, release `name`, and release `body`, then selected the numerically highest candidate. A number such as `64` in release notes could therefore override a real version such as `02.04` and cause the client to repeatedly download and install the same release.

## Publication safeguards — implemented for 02.17 (28 October 2026)

[`publish-client-release.yml`](../.github/workflows/publish-client-release.yml) listens for **successful push builds on main** and requires a valid `NN.NN` version in `Client PSVitaAlive/CMakeLists.txt`, plus human-written notes in `docs/releases/NN.NN.md`.

It downloads the *exact* successful VitaSDK CI `PSVitaAlive.vpk`, validates its ZIP CRC, checks `sce_sys/param.sfo` for matching **APP_VER** and `TITLE_ID=PSVAS1178`, calculates SHA-256, and publishes the latest stable GitHub Release under that version tag. An existing release is left intact; a pre-existing tag without a release blocks publication.

The publisher's `NN.NN` format restriction is stricter than the **runtime updater's** existing backward-compatible `tag_name` parser (which accepts certain other dotted numeric formats). The runtime version comparison remains based exclusively on the tag, never the release body.

**Verified publication:** [02.17](https://github.com/VegettoSan/PSVitaAlive/releases/tag/02.17) · [workflow policy](../.github/workflows/README.md). The release artifact passed CI metadata/integrity checks; hardware validation of this exact versioned binary remains a separate step.
