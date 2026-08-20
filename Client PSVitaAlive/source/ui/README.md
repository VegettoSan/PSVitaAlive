# `source/ui/` — Native UI

Rendered with **vita2d** (960×544). Accent color aligns with the store green (`#3BFF00`).

## Main surface

`FullCatalogScreen` covers:

- Full catalog grid
- Split detail view
- Catalog loading (including full-screen loading art when configured)
- Settings
- Download / install progress and result overlays
- Search and catalog switching

## Supporting pieces

| File | Role |
|------|------|
| `image_cache.cpp` | Async icon/screenshot cache; release textures when leaving views |
| `ui_types.cpp` | Shared UI types |

## Rules

- UI requests actions (install, cancel, acknowledge); it does not promote packages itself.
- Touch and controls should share the same actions where implemented.
- Heavy textures should not stay resident when the user leaves a catalog/detail context.

## Catalog list memory

For large catalogs (Vita Games), browsing with an empty search should use `catalogView()` backed by `allItems_` so the filtered `items_` vector is not a full second copy of the catalog in RAM.
