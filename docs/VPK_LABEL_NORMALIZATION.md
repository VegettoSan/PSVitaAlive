# Canonical Homebrew VPK button labels

A principal download uses type Download and name VPK. Existing release synchronization and Archive preservation require this exact unique label. The normalizer scans every apps/*.json before catalog generation and edits only recognized unambiguous labels: current version, exact app name/ID, direct asset filename or generic VPK/download label. Existing uniquely named VPK is kept (case/space-only normalization when necessary). Multiple candidates, variant descriptions and non-direct downloads are not guessed or renamed. Non-Download links, URLs, sizes, recommendation flags and every non-label field remain unchanged. No changes to generated catalogs by this script; validate.yml generates them as usual.

Run read-only: python -m scripts.normalize_vpk_labels --report /tmp/vpk-label-audit.json
Run apply: python -m scripts.normalize_vpk_labels --apply

GitHub Actions runs offline tests first, publishes an audit artifact and regenerates catalogs. A normalized label is not permission to create a new Archive.org item: source provider, version, asset identity and ownership safety gates remain unchanged.
