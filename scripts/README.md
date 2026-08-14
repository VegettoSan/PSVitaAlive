# `scripts/` — Automatización y validación del catálogo

Los scripts de esta carpeta convierten las fuentes de datos en la capa canónica de PS Vita Alive Store y validan que el resultado pueda ser consumido por la web y el cliente.

## Flujo principal

```text
external sources + apps/ + authors/ + categories/
                     ↓
          scripts/external/aggregate.py
                     ↓
             merge + deduplicación
                     ↓
              persistencia canónica
                     ↓
scripts/normalize_persisted_sources.py
                     ↓
            normalización de recursos
                     ↓
scripts/validate_catalog_ci.py
                     ↓
              catálogos generados
```

## Scripts importantes

### `generate_catalog.py`

Entrada principal del pipeline de generación. Coordina la agregación y genera los archivos canónicos/publicados según la implementación actual.

### `normalize_persisted_sources.py`

Sincroniza en los JSON realmente persistidos las decisiones de normalización tomadas por el pipeline. Su función es evitar que la información corregida exista solamente en memoria.

Entre otras tareas, puede resolver URLs multimedia, aplicar fallbacks y mantener autores consistentes.

### `external_smoke_test.py`

Pruebas rápidas para comprobar que las fuentes externas responden de una forma que el agregador puede procesar.

### `validate_catalog.py`

Validación completa del contenido del catálogo.

### `validate_catalog_ci.py`

Validación utilizada dentro de GitHub Actions antes de publicar.

### `validate_registry.py`

Comprobaciones adicionales de registros y catálogos generados.

## Desarrollo seguro

Antes de tocar la lógica de agregación:

1. Revisar `scripts/external/README.md`.
2. Probar el lector de la fuente afectada.
3. Ejecutar smoke tests.
4. Ejecutar la validación completa.
5. Confirmar que `catalog.json`, `authors.json` y `categories.json` siguen siendo generados.

## No editar manualmente

Los catálogos generados no son la fuente de verdad. Las correcciones deben hacerse en los JSON canónicos, en los Overrides o en el código que normaliza la fuente correspondiente.
