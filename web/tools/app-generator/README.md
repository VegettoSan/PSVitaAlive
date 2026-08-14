# Generador de JSON de aplicaciones

Esta herramienta permite crear y revisar un archivo individual `apps/<id>.json` desde la web de PS Vita Alive Store sin editar JSON manualmente.

## Qué hace

- Carga `catalog.json`, `authors.json` y `categories.json` desde `main`.
- Usa las categorías y subcategorías oficiales actuales.
- Permite seleccionar múltiples autores individuales.
- Comprueba IDs y Title IDs contra el catálogo publicado para evitar duplicados.
- Valida los campos obligatorios del validador actual.
- Permite entre 0 y 5 screenshots; si no se proporciona icono o screenshots, el pipeline puede aplicar sus fallbacks.
- Permite múltiples enlaces y como máximo uno marcado como recomendado.
- Incluye metadatos opcionales usados por las fuentes externas y el sistema de preservación.
- Permite importar un JSON existente y editarlo mediante el formulario.
- Muestra una vista previa JSON en tiempo real.
- Descarga solamente el JSON individual de la aplicación.

## Lo que NO hace

La herramienta no modifica `apps/`, `catalog.json`, `authors.json` ni `categories.json` en GitHub. Tampoco publica Pull Requests automáticamente.

El flujo oficial sigue siendo:

```text
Formulario web
    ↓
<id>.json
    ↓
apps/<id>.json
    ↓
GitHub Actions
    ↓
validación + normalización + Overrides
    ↓
catalog.json / authors.json / categories.json
```

## Autores nuevos

El generador solo considera válido un autor que ya exista en `authors.json`. Para un desarrollador nuevo, primero debe crearse su perfil individual en `authors/` mediante el flujo de contribución correspondiente. Esto evita generar aplicaciones que fallen posteriormente por referencias de autores inexistentes.

## Desarrollo

La herramienta es completamente estática y compatible con GitHub Pages. No necesita servidor, base de datos ni API propia. Los archivos están en `web/tools/app-generator/`:

- `index.html` — interfaz.
- `app-generator.css` — estilos propios de la herramienta.
- `app-generator.js` — carga de catálogos, formulario, validación, importación, vista previa y descarga.
