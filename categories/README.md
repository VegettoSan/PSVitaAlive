# `categories/` — Official taxonomy

Defines the category and subcategory IDs used by homebrew applications.

## Role

- Source of truth for `category_id` / `subcategory_ids` on apps
- Emitted into generated `categories.json`
- Icons live under the website/client asset trees as configured by the project

## Structure

Each category typically includes:

- `id`, `name`, `description`
- `order` (display ordering)
- `subcategories[]` with `id` and `name`
- optional `icon` path

## Rules

- App `category_id` must exist here.
- App `subcategory_ids` must belong to that category.
- Prefer the project’s normalized VitaDB / VitaHomebrewDB mapping rather than inventing parallel duplicate categories.
