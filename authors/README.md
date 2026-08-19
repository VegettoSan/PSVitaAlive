# `authors/` — Canonical author profiles

One JSON file per author referenced by homebrew apps.

## Role

- Editable source for author metadata
- Merged into generated `authors.json`
- Linked from applications via `author_ids`

## Typical fields

| Field | Purpose |
|-------|---------|
| `id` | Stable ID referenced by apps |
| `name` | Display name |
| `avatar` | Image URL or repo-relative path |
| `bio` / description | Optional text |
| `links` | Website, GitHub, Twitter, etc. |

Missing avatars fall back to the default author icon during catalog generation when configured.

## Rules

- Keep `id` stable once published.
- Prefer absolute URLs for remote avatars.
- Do not put multi-author strings in app records—use multiple `author_ids` instead.
