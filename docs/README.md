# Documentación de VitaHub

Este directorio sirve como índice y memoria técnica del proyecto. La intención es que otra persona pueda entender por qué existe cada pieza antes de tocarla.

## Arquitectura oficial

```text
apps/ + authors/ + categories/
            ↓
     importación externa
            ↓
       merge + dedupe
            ↓
        Overrides
            ↓
        validación
            ↓
catalog.json + authors.json + categories.json
            ↓
        Web + PS Vita
```

## Decisiones importantes

### Catálogos externos

VitaDB, NeoVitaDB y VitaDBtoo se utilizan como fuentes de entrada. Sus formatos no son el formato canónico de VitaHub.

Cada fuente debe adaptarse mediante un lector/normalizador específico cuando sea necesario.

### Title ID

El `title_id` permite deduplicar aplicaciones entre fuentes y detectar actualizaciones. Los IDs internos de las fuentes externas no se deben utilizar automáticamente como `id` de VitaHub.

### Versiones

Cuando varias fuentes ofrecen la misma aplicación, la frescura de versión/fecha debe tener prioridad sobre la prioridad fija de la fuente. La prioridad sirve como desempate y para decidir qué información es preferida cuando los registros son equivalentes.

### Overrides

Los datos manuales de VitaHub pueden complementar o sustituir campos incompletos de fuentes externas. No se deben perder por una importación posterior.

### Autores

Los autores son entidades individuales. Las fuentes que entregan varios nombres en un solo campo se normalizan a perfiles separados cuando la separación es inequívoca.

### Recursos

La capa canónica debe preferir URLs absolutas para imágenes y descargas. Las rutas relativas solo son válidas si apuntan realmente a un recurso dentro del propio repositorio.

## Historial técnico resumido

Los principales problemas que motivaron el diseño actual fueron:

- endpoints externos que cambiaban de comportamiento;
- VitaDB devolviendo `200 + []` con User-Agent no compatible;
- screenshots externos guardados como rutas locales;
- iconos/avatares rotos sin fallback;
- varios desarrolladores representados como un solo autor;
- autores nuevos referenciados pero sin JSON;
- dos Actions publicando simultáneamente y provocando conflictos Git;
- enlaces recomendados que podían quedar ligados a la prioridad de la fuente en vez de a la versión más reciente.

Estas situaciones se documentan para evitar repetir los mismos errores.

## Regla para futuros cambios

Antes de modificar una pieza del sistema, identificar si el problema pertenece a:

1. adquisición externa;
2. normalización;
3. identidad/deduplicación;
4. merge/enrichment;
5. Overrides;
6. persistencia canónica;
7. generación de catálogos;
8. validación;
9. publicación/Pages;
10. web o cliente.

Corregir la capa responsable en lugar de introducir parches en los catálogos generados.
