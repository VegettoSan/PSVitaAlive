# `.github/workflows/` — Automatización de GitHub Actions

Los workflows automatizan validación, regeneración de catálogos y publicación de GitHub Pages.

## Workflow del catálogo

`validate.yml` ejecuta conceptualmente:

```text
checkout
  ↓
determinar modo incremental/rebuild
  ↓
smoke tests externos
  ↓
generate_catalog.py
  ↓
normalize_persisted_sources.py
  ↓
normalización adicional de autores/registros
  ↓
validate_catalog_ci.py
  ↓
verificación de catalog.json/authors.json/categories.json
  ↓
commit + publicación
```

## Ejecución automática

La generación se ejecuta periódicamente y también puede ejecutarse por `push`/manual según la configuración actual del workflow.

## Concurrencia

El workflow utiliza un grupo de concurrencia para evitar que dos publishers modifiquen `main` simultáneamente. Esto evita conflictos de tipo `add/add` que pueden producirse cuando dos ejecuciones crean el mismo JSON de autor o aplicación.

La intención es esperar a que termine la publicación anterior en lugar de cancelar silenciosamente la ejecución que contiene datos nuevos.

## Modo incremental y rebuild

Si existe un marcador explícito de rebuild o la validación de la capa fuente falla, el workflow puede reconstruir desde las fuentes externas.

El rebuild no significa editar los catálogos generados a mano. Reconstruye los archivos canónicos y luego vuelve a generar los catálogos.

## Publicación

`catalog.json`, `authors.json` y `categories.json` se verifican antes de publicar.

Si hay un error de validación, el workflow debe detenerse antes de publicar un catálogo incompleto.

## Diagnóstico

Cuando un workflow falla, mirar primero el primer paso rojo y no solamente el resumen final. Diferenciar entre:

- adquisición externa;
- normalización;
- validación;
- concurrencia/publicación;
- despliegue de Pages.

Esto evita corregir código de catálogo cuando el problema real es un `git push` o una ejecución simultánea.
