# `apps/` — Aplicaciones canónicas de VitaHub

Esta carpeta contiene **un JSON por aplicación**. Es la fuente canónica de aplicaciones de VitaHub y es la entrada que consume el generador antes de producir `catalog.json`.

## Regla principal

No crear ni editar `catalog.json` manualmente. Las aplicaciones deben mantenerse aquí.

Flujo:

```text
apps/*.json
    ↓
normalización + merge + Overrides
    ↓
validación
    ↓
catalog.json
```

## Identidad

Cada aplicación debe tener:

- `id`: identificador interno único de VitaHub. No necesita coincidir con un ID externo.
- `title_id`: Title ID de PS Vita. Es obligatorio y debe ser único dentro del catálogo.
- `name`: nombre visible.
- `description`: descripción corta.
- `author_ids`: lista de autores individuales.
- `category_id`: categoría oficial de VitaHub.
- `subcategory_ids`: subcategorías permitidas por esa categoría.
- `version` y `version_date`: versión publicada y fecha de versión.
- `icon` y `screenshots`: recursos multimedia válidos.
- `links`: fuentes de descarga, repositorio, sitio oficial, documentación, etc.
- `status`: `Verified`, `Legacy` o `Archive`.

## Autores

Las aplicaciones utilizan `author_ids` y no guardan un único string con varios desarrolladores.

Correcto:

```json
"author_ids": [
  "autor-a",
  "autor-b"
]
```

No recomendado:

```json
"author_id": "autor-a & autor-b"
```

## Enlaces

Puede haber varios enlaces de descarga. El pipeline externo determina el enlace recomendado según frescura de versión/fecha; la prioridad de fuente se utiliza como desempate. Los Overrides pueden fijar manualmente el resultado.

Ejemplo mínimo:

```json
"links": [
  {
    "type": "Download",
    "name": "VPK",
    "url": "https://example.org/app.vpk",
    "recommended": true
  },
  {
    "type": "Repository",
    "name": "GitHub",
    "url": "https://github.com/example/project",
    "recommended": false
  }
]
```

## Recursos externos

No conservar rutas relativas externas como `screenshots/foo.png` si el archivo no existe dentro de VitaHub. Deben convertirse a URLs absolutas públicas y validarse.

## Actualizaciones externas

Cuando una aplicación existe localmente y una fuente externa contiene una versión más reciente, la agregación puede actualizar/enriquecer el JSON, respetando información local y Overrides.

## Publicación manual

Para una aplicación mantenida por un desarrollador:

1. Crear o modificar su JSON en `apps/`.
2. Verificar `title_id`.
3. Verificar autores y categorías.
4. Ejecutar la validación local si corresponde.
5. Commit y push.
6. GitHub Actions regenerará los catálogos.

## Nunca hacer

- Editar manualmente `catalog.json`.
- Repetir una aplicación con otro `id` si tiene el mismo `title_id`.
- Inventar URLs de screenshots.
- Convertir varios autores en un único perfil compuesto.
