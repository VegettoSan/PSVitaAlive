# Fast tests

These tests validate the external version-date normalization and merge logic without downloading or regenerating the full catalog.

Run locally from the repository root:

```bash
python -m unittest discover -s tests -p 'test_*.py' -v
```

Coverage includes:

- `VitaDB.date -> version_date`
- `VitaDBtoo.date -> version_date`
- NeoVitaDB generated `date -> version_date`
- repair of a stale local date when the external source has the same version with a valid date
- no invented date when NeoVitaDB raw app metadata lacks a date
