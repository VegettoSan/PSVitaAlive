# Catalog Overrides

Crea un archivo JSON por aplicación usando exactamente el `id` canónico:

```text
catalog_overrides/adrenaline.json
```

El archivo solo contiene los campos que quieras cambiar o enriquecer.

Permitidos como reemplazo directo:

- `name`
- `description`
- `long_description`
- `requirements`
- `icon`
- `changelog`

Para listas se usa:

- `replace`
- `add`
- `remove`

Ejemplo:

```json
{
  "id": "adrenaline",
  "links": {
    "add": [
      {
        "type": "Download",
        "name": "Game Data",
        "url": "https://example.com/data.zip"
      }
    ]
  }
}
```

No se permiten overrides normales para `id`, `title_id`, `author_ids`, `category_id`, `subcategory_ids`, `status`, `version`, `version_date` ni `size`.
