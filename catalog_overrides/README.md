# `catalog_overrides/` — Overrides manuales

Esta carpeta contiene correcciones y enriquecimiento manual sobre aplicaciones que también pueden recibir datos desde catálogos externos.

## Cuándo usar un Override

Es apropiado cuando una fuente externa no contiene información que VitaHub quiere conservar, por ejemplo:

- descripción mejorada;
- descripción larga;
- screenshots adicionales;
- icono corregido;
- requisitos;
- changelog;
- enlaces adicionales, como datos de un juego/port;
- fuentes de descarga alternativas.

## Estructura

Se crea un JSON por aplicación usando el `id` canónico de VitaHub:

```text
catalog_overrides/adrenaline.json
```

El archivo debe contener solamente los campos que se quieren cambiar o enriquecer.

## Operaciones de listas

Las listas pueden utilizar:

- `replace`: sustituir la lista completa;
- `add`: añadir elementos conservando los existentes;
- `remove`: eliminar elementos específicos.

Ejemplo:

```json
{
  "id": "adrenaline",
  "links": {
    "add": [
      {
        "type": "Download",
        "name": "Game Data",
        "url": "https://example.com/data.zip",
        "recommended": false
      }
    ]
  }
}
```

## Prioridad

Conceptualmente:

```text
fuente externa
    ↓
merge/enrichment
    ↓
Override
    ↓
app/*.json final
```

Un Override parcial no debe borrar campos que no está modificando.

## Campos restringidos

La implementación actual no permite overrides normales para:

- `id`
- `title_id`
- `author_ids`
- `category_id`
- `subcategory_ids`
- `status`
- `version`
- `version_date`
- `size`

Estas restricciones protegen la identidad y la lógica de actualización del catálogo.

## Buenas prácticas

- Utilizar el `id` canónico exacto.
- Añadir solamente los datos necesarios.
- Usar URLs absolutas.
- No editar `catalog.json`.
- No usar el Override para ocultar un problema que debería corregirse en el adaptador de una fuente.
- Documentar en un comentario/commit la razón de una modificación especial.

## Ejemplo útil

Para un port que necesita datos externos del juego:

```json
{
  "id": "mi-port",
  "links": {
    "add": [
      {
        "type": "Download",
        "name": "Game Data",
        "url": "https://example.org/game-data.zip",
        "recommended": false
      }
    ]
  }
}
```

La aplicación sigue recibiendo automáticamente sus actualizaciones de versión desde las fuentes externas, mientras el enlace adicional permanece porque está definido en VitaHub.
