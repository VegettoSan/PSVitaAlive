# `categories/` — Official category taxonomy

PS Vita Alive Store controls the category vocabulary used by the catalog. External sources are mapped into this taxonomy instead of creating arbitrary categories during import.

## Structure

Each category has its own JSON file and may declare its allowed subcategories:

```json
{
  "id": "games",
  "name": "Games",
  "description": "...",
  "subcategories": [
    {
      "id": "arcade",
      "name": "Arcade"
    }
  ]
}
```

## Rules

- `category_id` must refer to an existing official category.
- `subcategory_ids` may contain only subcategories declared by that category.
- An external source cannot create a new official category simply by importing a new value.
- `sources/category_map.json` translates external category/type values into the official vocabulary.
- Applications should remain compatible when optional category metadata is added later.

## Category icons

Category icons are maintained in the project's resource structure. They may also be used as application-image fallbacks when an imported application does not provide a usable icon.

## Changing the taxonomy

When adding or changing a category:

1. Modify the individual JSON file in `categories/`.
2. Review the external mapping in `sources/category_map.json`.
3. Check existing `category_id` and `subcategory_ids` references.
4. Run the catalog validation before publishing.

`categories.json` is generated automatically and must not be edited manually.
