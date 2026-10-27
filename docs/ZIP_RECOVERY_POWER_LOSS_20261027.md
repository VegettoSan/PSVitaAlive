# PSVitaAlive: ZIP recuperable antes de la extracción (2026-10-27)

## Objetivo
No perder un ZIP de varios GB ya descargado por apagado, cierre forzado,
cancelación o error durante su extracción. No duplicar bytes ni modificar
el extractor o el reemplazo actual de archivos de destino.

## Comportamiento
1. Tras validar la descarga completa de un ZIP de datos se llama a la misma
   función de recuperación `InstallController::persistPendingZipRecovery()`.
2. ZIP y metadatos se guardan en
   `ux0:data/psvitaalive/manual/<title_id-or-app_id>/`. Se usan renombrados
   dentro de `ux0:`, nunca una copia de múltiples GB a la RAM.
3. Los ZIP completos y los 2/4 rangos están disponibles en
   `Saved ZIP Files → Retry extraction / Delete / Cancel`.
4. Éxito del extractor: eliminación automática del ZIP/rangos y su sidecar.
5. Error: el menú existente ofrece conservar o eliminar.
6. Cancelación/cierre/apagado: el archivo continúa guardado hasta reintento
   exitoso o eliminación explícita desde la UI o Administración de almacenamiento.
7. Los reintentos usan el mismo `ZipExtractor` y la sobrescritura de archivos
   existente. No se añade comparación de contenidos previos.

## Protección al guardar partes segmentadas
Antes del primer `rename` de rangos, el cliente escribe en el trabajo un
`.psva_keep_zip` con el journal `PSVA_ZIP_TXN_V1`: ruta origen, destino,
número de partes y metadatos del archivo `.info.txt`.

Al arrancar, `cleanupStartupDownloads()` termina los `rename` pendientes
antes de que `purgeIncompleteJobs()` borre los temporales. Si faltan partes
o hay una colisión ambigua, el trabajo marcado **no se elimina** y puede
revisarse desde el administrador de almacenamiento. Una recuperación completa
retira el marcador para que la limpieza habitual borre el trabajo vacío,
sin tocar el ZIP conservado.

## Alcance
Aplica a ZIP de datos con destino de extracción. VPK directo, PKG/BGDL,
plugins y descargas HTTP incompletas mantienen su flujo anterior. No cambia
`apps/`, `authors/`, `categories/` ni los catálogos generados.

## Pruebas en PS Vita real
- ZIP único y 2/4 partes: extracción correcta, sin residuos originales.
- Fallo o cancelación: ZIP guardado y reextracción sin Internet.
- Corte de energía durante extracción: ZIP visible tras reiniciar.
- Corte durante renombrado de varias partes: reconstrucción tras iniciar.
- Reemplazo de archivos previos: exactamente el comportamiento existente.
- Repetición de Install All, VPK/PKG/plugins y limpieza UX0: sin regresiones.

Las pruebas unitarias y la compilación CI no sustituyen apagar bruscamente una
Vita física durante la extracción.
