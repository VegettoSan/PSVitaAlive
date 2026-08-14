# `categories/` — Categorías oficiales de VitaHub

Las categorías de VitaHub son controladas por el proyecto. Las aplicaciones importadas desde fuentes externas deben adaptarse a estas categorías mediante `sources/category_map.json` y el agregador.

## Estructura

Cada categoría tiene su propio JSON y puede definir subcategorías:

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

## Reglas

- `category_id` debe corresponder a una categoría existente.
- `subcategory_ids` solo puede usar subcategorías declaradas por esa categoría.
- Una fuente externa no puede crear arbitrariamente una categoría nueva durante la importación.
- `category_map.json` traduce valores externos al vocabulario oficial de VitaHub.

## Iconos

Los iconos de categorías se conservan en la estructura de recursos correspondiente. Cuando el icono de una aplicación externa no es válido, la normalización puede utilizar el icono de su categoría como fallback.

## Cambiar una categoría

Modificar primero el JSON individual de `categories/` y revisar la tabla de mapeo externa. Después ejecutar las validaciones antes de publicar.

`categories.json` es generado automáticamente y no debe editarse manualmente.
