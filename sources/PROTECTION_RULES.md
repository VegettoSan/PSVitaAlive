# VitaHub local application protection

Applications already present under `apps/` are protected catalog entries. External feeds are inputs for discovery, enrichment, and correction; they are never authoritative for deletion.

A rebuild must preserve every existing local application even when the application is absent from VitaDB or VitaHomebrewDB.

## Retired applications

Author-requested or otherwise permanently removed applications are registered in `registry/retired_ids.json` by both internal `id` and `title_id`.

A retired application:

- must not remain in `apps/` as an active entry;
- must not be recreated from VitaDB, VitaHomebrewDB, NeoVitaDB, or another external feed;
- must not be replaced by a new application using the retired identity;
- remains historically reserved so its identifiers cannot be silently reused.

The registry is the protection layer for removals. The external aggregation path must treat registered retired Title IDs as non-publishable records, and CI validation must reject any accidental reintroduction. An explicitly approved restoration can be allowlisted in `apps.restorable` using the exact `id`, `title_id`, and `author_ids` after manual verification; this must not happen automatically.

### Current author-requested removal

- `id`: `aleph-one-vita`
- `title_id`: `ALPH00001`
- Author: `drdecki`
- Reason: removal requested by the original author.

### Temporarily withdrawn pending an official developer release

- `id`: `pw-ace-attorney`
- `title_id`: `OBJECTION`
- Withdrawn 2026-11-21 after the original port developer reported that the published entry came from an unofficial submission of an unfinished test build.
- The submitted `suspect-wild` author profile has also been retired because it was created only for this entry. It must not be reused as the developer's identity.
- The original unauthorized test listing remains retired and must never be revived under `suspect-wild`.
- The port's developer `BekaLife08` (RetroBoy) submitted PR #121 under `phoenix-wright-ace-attorney-trilogy`; after review this is the sole allowed restoration for `OBJECTION`, using `author_ids: ["bekalife08"]`.
- `registry/retired_ids.json` preserves the old `pw-ace-attorney` identity while explicitly allowlisting the developer's new identity. This does not authorize other apps or authors to reuse the Title ID.
- Publication remains subject to the corrected canonical JSON, Archive.org mirror verification, and CI checks; never import any retired external record automatically.
