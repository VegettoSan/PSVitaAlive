# `web/` — Sitio web de PS Vita Alive Store

El sitio web es una interfaz estática para GitHub Pages. No mantiene una base de datos propia ni edita los catálogos.

## Datos consumidos

La web debe consumir únicamente:

- `catalog.json`
- `authors.json`
- `categories.json`

Esos archivos son generados por GitHub Actions.

```text
GitHub Actions
      ↓
catálogos oficiales
      ↓
web/*.html + web/js/*.js
```

## Funciones principales

La interfaz puede mostrar:

- aplicaciones;
- autores;
- categorías/subcategorías;
- perfiles de autor;
- screenshots;
- múltiples enlaces de descarga;
- versión y fecha;
- búsqueda y ordenación.

## URLs externas

Las imágenes y descargas procedentes de fuentes externas deben llegar como URLs absolutas válidas. La web no debería asumir que una ruta relativa de VitaDBtoo, VitaDB o NeoVitaDB existe dentro de GitHub Pages.

## Fallbacks

Los componentes de la web deben tolerar iconos o avatares inválidos. El catálogo ya intenta persistir fallbacks, pero la UI debe mantener un fallback visual para evitar tarjetas rotas.

Fallback de autor:

`https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors/icon/autoricon.png`

Los iconos de aplicaciones pueden caer al icono de su categoría cuando el recurso principal no carga.

## Desarrollo

Cuando se cambie la web:

1. No modificar la estructura JSON sin actualizar el pipeline y el cliente.
2. Probar páginas de aplicación, autor y categoría.
3. Comprobar que los enlaces externos abren fuera de GitHub Pages.
4. Revisar que múltiples autores sean enlaces individuales.
5. Comprobar caché/actualización después de un rebuild.

## GitHub Pages

El despliegue se automatiza mediante `.github/workflows/pages.yml`. El repositorio no necesita un servidor backend para mostrar el catálogo.
