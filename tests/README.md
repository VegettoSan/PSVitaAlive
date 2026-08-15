# VitaHub fast tests

These tests are intentionally small and deterministic. They validate source adapters and merge behavior without downloading the full external catalogs or regenerating the production catalog.

Run locally with:

```bash
python -m unittest discover -s tests -p 'test_*.py'
```

The full catalog workflow remains the integration test and is only needed after the fast tests pass.
