# PSVitaAlive — Registro de integración estable del PR #85

**Fecha:** 24 de noviembre de 2026  
**Estado:** parte estable integrada en `main` mediante [PR #110](https://github.com/VegettoSan/PSVitaAlive/pull/110); el extractor experimental permanece en [PR #85](https://github.com/VegettoSan/PSVitaAlive/pull/85) (borrador).  
**Commit de integración (squash):** [`1d99d034abb26b21f982e02a144c6bdd207fc085`](https://github.com/VegettoSan/PSVitaAlive/commit/1d99d034abb26b21f982e02a144c6bdd207fc085).  
**Rama de aislamiento utilizada:** `fix/pr85-stable-space-preflight-20261124`.  
**Release:** no se publicó una nueva versión al fusionar; que el código esté en `main` no implica que los usuarios de la release instalada lo tengan.

> **Seguimiento posterior — PR #113 integrado en main:** tras #110, se incorporó por separado la **apertura directa de archivos existentes** validada funcionalmente en Build #645. El [PR #113](https://github.com/VegettoSan/PSVitaAlive/pull/113) se fusionó con commit [`fb21bc0e3038d65137c7dd7b1ba6055176031485`](https://github.com/VegettoSan/PSVitaAlive/commit/fb21bc0e3038d65137c7dd7b1ba6055176031485). Ver [ZIP_EXISTING_FILE_OPEN_FASTPATH_20261124.md](ZIP_EXISTING_FILE_OPEN_FASTPATH_20261124.md). La lista histórica de este documento describe **solo PR #110**; el pipeline ZIP, la reanudación CRC32 (PR #112) y el problema de crear archivos nuevos siguen pendientes.

## Por qué se separó la integración

El PR #85 agrupaba mejoras de espacio de almacenamiento, progreso, paralelismo ZIP STORE, pausas cooperativas y experimentos de apertura de archivos. Durante tres pruebas en PS Vita física, el ZIP de Minecraft Modern Legacy siguió sufriendo una ralentización severa cerca del 17 % en bytes y congelamientos de interfaz, con apagados forzados.

Además, la rama experimental estaba muy atrasada respecto de `main`; no era prudente fusionarla por completo ni dar por validado su extractor.

Se creó una **rama limpia desde el `main` vigente** y se trasladaron únicamente los cambios de política de espacio, coordinación de `Install All`, progreso de archivos ZIP, pruebas y documentación. Se verificó un diff de **nueve archivos**, sin tocar `zip_extractor.cpp`, los catálogos ni sus entradas fuente.

## Funcionalidades integradas

| Área | Comportamiento en `main` |
| --- | --- |
| Descarga individual | `InstallController::requestInstall` consulta espacio libre de `ux0:` antes de encolar VPK, ZIP Game/Data, plugin y PKG/BGDL; si la consulta falla, bloquea la descarga. |
| ZIP STORE conocido | Reserva estimada de **2,25 × tamaño descargado + 64 MiB**: ZIP y extracción pueden coexistir. Los ZIP con gran compresión siguen requiriendo el preflight exacto del extractor. |
| VPK/PKG y plugins | Para paquetes directos/BGDL se utiliza 2,1 × tamaño declarado; plugin = tamaño + 4 MiB. |
| Tamaño desconocido | Reserva inicial mínima por enlace: ZIP **256 MiB**, paquete **64 MiB**, plugin **16 MiB**. No garantiza capacidad final si el servidor no informa su tamaño real. |
| `Install All` | Comprueba **antes del primer paso** el conjunto de enlaces realmente elegidos y plugins autorizados, con **128 MiB** de reserva adicional. No cuenta espejos no seleccionados. |
| Enlaces sin tamaño | No descartan la suma conocida: se agregan reservas mínimas por tipo. Las comprobaciones de tamaño HTTP 200/206 y de espacio durante la transferencia siguen vigentes. |
| Pasos sucesivos | Reutilizan la aprobación del presupuesto inicial, evitando volver a cobrar como si todos los bytes de fases anteriores fueran nuevos. |
| Interfaz | El progreso del dispatcher presenta `ZIP files: N/M` además del progreso de bytes; esto **no cambia la velocidad de extracción**. |

Las estimaciones no sustituyen las verificaciones de espacio en ejecución, CRC, rutas, integridad ZIP ni recuperación. Los flujos sin `Content-Length` verificable tienen limitaciones inevitables.

## Archivos de código y pruebas fusionados

- `Client PSVitaAlive/include/installer/install_space_policy.hpp`
- `Client PSVitaAlive/include/installer/install_controller.hpp`
- `Client PSVitaAlive/include/ui/full_catalog_screen.hpp`
- `Client PSVitaAlive/source/installer/install_controller.cpp`
- `Client PSVitaAlive/source/installer/install_dispatcher.cpp`
- `Client PSVitaAlive/source/main.cpp`
- `Client PSVitaAlive/source/ui/full_catalog_screen.cpp`
- `tests/test_install_space_policy.cpp`
- `docs/PREDOWNLOAD_SPACE_RESERVATION_20261122.md`

## Validación y estado real

- **Aprobado:** [Build PS Vita Client #646](https://github.com/VegettoSan/PSVitaAlive/actions/runs/45415331299) (pruebas host del workflow y compilación VitaSDK).
- **Aprobado:** [Validate and Generate Local Catalog #3328](https://github.com/VegettoSan/PSVitaAlive/actions/runs/45415331421).
- **Aprobado:** PR #110 sin conflictos, fusionado por GitHub, commit verificado en `main`.
- **Pendiente:** matriz de pruebas físicas específica de las nuevas reservas (poco espacio, ZIP STORE, tamaños desconocidos, `Install All` con varias fuentes, BGDL, contenido HTTP distinto al catálogo, cancelación/recuperación). La CI exitosa **no sustituye** la validación en hardware.

## Cambios que deliberadamente NO se fusionaron

Permanecen solo en la rama experimental del PR #85:

- Cambios de `Client PSVitaAlive/source/archive/zip_extractor.cpp`, creación exclusiva `SCE_O_EXCL` y apertura directa/adaptativa.
- Pipeline de lectura/escritura para archivos ZIP STORE grandes (hilo de escritura + 3 × 256 KiB).
- `ZipWorkPacer`, nuevas políticas de directorios, caches y métricas del extractor.
- Ajustes de CMake y pruebas/docs exclusivamente experimentales de ZIP.
- Cualquier presunta solución al congelamiento del ZIP de Minecraft.

**No hacer merge global del PR #85**, incluso si sus checks aparecen verdes. Como la integración estable se realizó mediante squash, el PR antiguo puede seguir mostrando cambios ya integrados. Antes de retomarlo, crear una rama actualizada desde `main` y trasladar solo los experimentos que se quieran volver a validar.

## Compatibilidad y arquitectura

No se modificaron manualmente `apps/`, `authors/`, `categories/` ni `catalog.json`, `authors.json`, `categories.json`. Se conserva el flujo oficial de generación automática de catálogos. No se añadieron servidores propios, servicios externos ni nuevos formatos de datos.

## Puntos de continuación

1. Probar los controles de espacio por separado con poco espacio real y contenido controlado; confirmar que **no se encola** un trabajo cuando se deniega.
2. Usar [la investigación pendiente de Minecraft](MINECRAFT_ZIP_DIRECTORY_STALL_FOLLOWUP_20261124.md) para la siguiente fase del extractor.
3. Mantener PR #85 como borrador hasta medir un cambio seguro y reproducible en consola física.
4. Publicar una release únicamente mediante el proceso normal de PSVitaAlive cuando la versión candidata esté validada; **no confundir merge con release**.

**Referencias:** [PR #110 integrado](https://github.com/VegettoSan/PSVitaAlive/pull/110) · [PR #85 pendiente](https://github.com/VegettoSan/PSVitaAlive/pull/85) · [política detallada de reserva](PREDOWNLOAD_SPACE_RESERVATION_20261122.md).
