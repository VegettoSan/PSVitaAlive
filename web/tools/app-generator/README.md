# App generator tool

Small static helper to draft homebrew `apps/*.json` records for PS Vita Alive Store.

## Usage

Open `index.html` locally in a browser (or serve the folder). Fill fields, copy/download JSON, and submit via PR into `apps/`.

## Notes

- Output must still pass repository validation.
- Prefer absolute media URLs.
- Align `category_id` / `subcategory_ids` with `categories/`.

## Optional link fields

- `size` — human-readable (`120 MB`) or integer bytes
- `extract_path` — ZIP extract destination (e.g. `ux0:data/MyApp/`). If omitted, the Vita client asks the user.
