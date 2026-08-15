# `sources/` — Fuentes externas y reglas de importación

`external_sources.json` define las fuentes consultadas por GitHub Actions. Las fuentes externas sirven para descubrir, actualizar, enriquecer y preservar aplicaciones; no sustituyen el formato canónico de PS Vita Alive Store.

## Fuentes actuales

- **VitaDB** — fuente oficial de VitaDB y fuente externa prioritaria entre las actuales.
- **NeoVitaDB** — catálogo estático alternativo.
- **VitaDBtoo** — catálogo comunitario/rescate que actúa como respaldo y fuente de enriquecimiento.

La configuración real se encuentra en `sources/external_sources.json`.

## VitaDB

El endpoint documentado por VitaDB utiliza `POST` sin parámetros:

```text
https://rinnegatamante.eu/vitadb/list_hbs_json.php
```

El adaptador actual usa HTTPS, fallbacks y un User-Agent de navegador porque VitaDB puede devolver una lista vacía a User-Agents no reconocidos.

No sustituir el lector específico por un `GET` genérico sin comprobar primero el comportamiento real del endpoint.

## Prioridad y versiones

La prioridad de fuente es un criterio de preferencia y desempate. No sustituye la comparación de frescura.

Para seleccionar una descarga recomendada, el pipeline actual compara primero versión y fecha; la prioridad de fuente se utiliza solamente cuando esos criterios empatan.

## Mapeo de categorías

`category_map.json` traduce valores de las fuentes externas al vocabulario oficial de PS Vita Alive Store. Una fuente externa no puede crear libremente nuevas categorías o subcategorías durante la importación.

### Tipos VitaDB / VitaDBtoo / NeoVitaDB

Los tres feeds usan el mismo esquema numérico (documentado en NeoVitaDB `categories.json`):

| type | Significado | `category_id` PSVitaAlive |
|-----:|-------------|---------------------------|
| 1 | Original Game | `games` |
| 2 | Game Port | `ports` |
| 4 | Utility | `utilities` |
| 5 | Emulator | `emulators` |

También se aceptan los slugs textuales (`game`, `port`, `utility`, `emulator`, `plugin`, …).

Los **plugins** suelen llegar en un listado aparte (sin `type`); se mapean a `plugins`.

### Tags → subcategorías

Cuando la fuente trae `tags` (p. ej. `puzzle`, `rpg`, `fps`, `filebrowser`), el agregador los traduce a `subcategory_ids` oficiales mediante `tag_to_subcategory` en el mismo JSON. Solo se conservan IDs que existan en la categoría destino; se eliminan duplicados. Si no hay tag usable, se usa `other`.

## Recursos multimedia

Los catálogos externos pueden proporcionar rutas relativas como `icon.png` o `screenshot1.png`. PS Vita Alive Store debe convertirlas a URLs públicas absolutas cuando la fuente realmente publique esos recursos.

No inventar URLs ni conservar rutas relativas que solo tengan significado dentro del sitio externo.

## Agregar una nueva fuente

1. Identificar el formato real del catálogo.
2. Documentar endpoint, método HTTP, autenticación y límites.
3. Crear/adaptar un lector aislado.
4. Convertir cada registro al modelo interno `Candidate`.
5. Resolver autores como perfiles individuales.
6. Resolver categorías mediante `category_map.json`.
7. Resolver iconos/screenshots.
8. Definir prioridad.
9. Añadir/practicar smoke tests.
10. Ejecutar la validación completa antes de habilitar la fuente.

## Regla fundamental

La fuente externa es una entrada. La autoridad final es PS Vita Alive Store: `title_id`, autores individuales, taxonomía oficial, datos locales, Overrides y reglas de merge.

El cliente PS Vita y la web siguen consumiendo únicamente los catálogos generados.
