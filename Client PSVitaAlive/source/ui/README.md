# `source/ui/` — Interfaz del catálogo

UI nativa con **vita2d** (sin Dear ImGui).

## Pantalla principal

`FullCatalogScreen` implementa:

- Grid de catálogo (`FULL_CATALOG`) y detalle partido (`SPLIT_DETAIL`).
- Overlays de **carga de catálogo**, **progreso de descarga/instalación**, y **resultado** (éxito / error).

## Overlay de instalación

`setInstallProgress(..., outcome, liveAreaOk, installPath, titleId)`:

| outcome | Significado | Aspecto |
|--------:|-------------|------------------------------|
| 0 | Progreso | Barra % + Cancel |
| 1 | Éxito | Título verde, destino, LiveArea sí/no |
| 2 | Error | Título rojo, motivo, pista al `session.log` |

Callbacks:

- `setInstallCancelCallback` — cancela descarga en curso.
- `setInstallAcknowledgeCallback` — cierra el panel de resultado.

## Contratos

La UI **no** escribe en disco ni llama al Promoter. Solo lee estado del `InstallController` y pide acciones (install / cancel / acknowledge).

Colores de marca: fondo oscuro, acento `#3BFF00`, error `#E03232`.
