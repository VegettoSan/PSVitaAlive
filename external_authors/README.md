# `external_authors/` — Provisional external identities

This layer represents authors discovered automatically who are not yet fully curated in `authors/`, or whose identity still needs resolution.

## Flow

```text
external source
      ↓
name / repository / links
      ↓
identity resolution
      ↓
external_authors/ (when provisional data is needed)
      ↓
authors/<stable-id>.json
```

The `author_id` must remain stable. When an identity is manually curated, create:

```text
authors/<same-id>.json
```

Do not change the ID used by applications merely because the profile moved from provisional to canonical storage.

## Identity resolution

The pipeline may compare normalized names, hyphen/underscore variants, repositories, GitHub links and other public identifiers to avoid duplicate profiles.

When an external source represents multiple developers in one field, the goal is to create individual profiles only when the separation is sufficiently unambiguous.

## Fallback

When no reliable avatar exists, the canonical profile may use:

```text
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors/icon/autoricon.png
```

## Important

`external_authors/` does not replace `authors/`. The website and PS Vita client consume canonical author data through generated `authors.json`.
