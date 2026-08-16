# `authors/` — Individual author profiles

This directory contains one JSON file per author. PS Vita Alive Store treats each developer or team as an independent identity so the website and PS Vita client can open a profile and list that author's applications.

## Structure

```text
authors/
├── author-a.json
├── author-b.json
└── icon/
    └── autoricon.png
```

## Profile data

A profile can contain fields such as:

```json
{
  "id": "author-a",
  "name": "Author A",
  "avatar": "https://example.org/avatar.png",
  "bio": "...",
  "links": [
    {
      "type": "GitHub",
      "name": "GitHub",
      "url": "https://github.com/author-a",
      "recommended": true
    }
  ],
  "icon": "https://example.org/avatar.png"
}
```

The exact optional fields may evolve. Consumers should ignore unknown fields and tolerate missing optional values.

## Multiple authors

Applications reference authors through `author_ids`:

```json
"author_ids": [
  "author-a",
  "author-b"
]
```

Do not create a synthetic profile such as `author-a & author-b`. When an external source contains multiple names in one field, the pipeline attempts conservative identity resolution and creates individual profiles when the separation is unambiguous.

## New authors

When an imported application references an author that does not yet exist, the pipeline may create a minimal profile and enrich it later with repositories, links and public avatars.

## Avatar fallback

The repository fallback image is:

```text
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors/icon/autoricon.png
```

If no reliable avatar exists, the canonical profile should use a valid absolute fallback URL rather than a broken relative path.

## Links

Author links should be useful and valid. The pipeline normalizes duplicates and allows at most one link to be marked `recommended`.

## Orphaned profiles

A physical author JSON may remain for preservation even when no current application references it. The generated `authors.json` represents the author records included by the final catalog generation process.

## Do not edit

`authors.json` is generated data. Permanent changes belong in `authors/*.json`, the external identity layer or the relevant aggregation/Override logic.
