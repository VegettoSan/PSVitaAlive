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

The registry is the permanent protection layer for removals. The external aggregation path must treat registered retired Title IDs as non-publishable records, and CI validation must reject any accidental reintroduction.

### Current author-requested removal

- `id`: `aleph-one-vita`
- `title_id`: `ALPH00001`
- Author: `drdecki`
- Reason: removal requested by the original author.
