# PSVitaAlive

Tienda alternativa, gratuita y abierta de Homebrew para PlayStation Vita.

## Arquitectura

```text
apps/ + authors/ + categories/
            ↓
      GitHub Actions
            ↓
catalog.json + authors.json + categories.json
            ↓
      Web + Cliente PS Vita
```

Los tres catálogos superiores son generados automáticamente y no deben editarse manualmente.

## Documentación por área

- [`apps/`](apps/README.md) — formato, publicación, IDs, versiones y enlaces de aplicaciones.
- [`authors/`](authors/README.md) — perfiles individuales, autores múltiples y fallbacks.
- [`categories/`](categories/README.md) — categorías oficiales y subcategorías.
- [`catalog_overrides/`](catalog_overrides/README.md) — correcciones y enriquecimiento manual que prevalece sobre fuentes externas.
- [`sources/`](sources/README.md) — configuración de fuentes externas y reglas de importación.
- [`external_authors/`](external_authors/README.md) — identidad provisional de autores descubiertos externamente.
- [`scripts/`](scripts/README.md) — pipeline de generación, normalización y validación.
- [`scripts/external/`](scripts/external/README.md) — adaptadores, deduplicación, merge y autores externos.
- [`web/`](web/README.md) — sitio GitHub Pages y consumo de los catálogos generados.
- [`Client PSVitaAlive/`](Client%20PSVitaAlive/README.md) — cliente PS Vita, compilación e integración con VitaSDK.
- [`.github/workflows/`](.github/workflows/README.md) — automatización de Actions, validación, rebuild y publicación.
- [`docs/`](docs/README.md) — decisiones y memoria técnica del proyecto.

## Fuentes externas actuales

El pipeline puede importar y enriquecer aplicaciones desde VitaDB, NeoVitaDB y VitaDBtoo. La fuente externa es una entrada de datos; la autoridad final es el modelo VitaHub, sus datos locales y sus Overrides.

## Regla para colaborar

Antes de cambiar la lógica de generación, revisar la documentación del directorio afectado y mantener la arquitectura oficial. Los catálogos generados se regeneran mediante Actions.
