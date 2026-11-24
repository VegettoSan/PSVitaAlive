# PSVitaAlive — Reanudación ZIP verificada para «Retry extraction»

**Fecha:** 24/11/2026  
**Estado:** implementación experimental en `feature/verified-zip-resume-20261124`. **Sin validar aún en PS Vita real. No fusionar en `main` sin pruebas físicas.**  
**Motivación:** [Minecraft Modern Legacy: bloqueo cerca del 17 %](MINECRAFT_ZIP_DIRECTORY_STALL_FOLLOWUP_20261124.md) · [Issue #111](https://github.com/VegettoSan/PSVitaAlive/issues/111).

## Objetivo y activación

Si un ZIP de Game Files / Data Files fue descargado correctamente y su extracción se interrumpió, el cliente ya permite guardar el ZIP (único o partes `.001`–`.004`) y utilizar **Retry extraction** sin redescargarlo. Esta función evita **volver a escribir** archivos completos que ya existen en el destino, pero **solo después de verificarlos**.

La opción `resumeVerifiedExisting` se habilita únicamente en `InstallController::workerSavedZipMain()`, al repetir la extracción desde la lista de ZIP guardados. El flujo de descarga e instalación normal, VPK, PKG, plugin, ZIP recién descargado y los ZIP sin destino explícito mantienen el comportamiento previo.

## Algoritmo sin diario persistente

1. `ZipExtractor` mantiene la validación original del ZIP, destino, rutas, source-collision, espacio y preparación de directorios; ningún paso se omite.
2. Para cada **archivo ordinario**, no perteneciente a ramas de staging profundo, comprueba que libzip reporta simultáneamente `ZIP_STAT_SIZE` y `ZIP_STAT_CRC`. Si faltan, extrae como antes.
3. En modo retry, intenta abrir en **solo lectura** el archivo existente. Comprueba su longitud real con `sceIoLseek` (64 bits). Si difiere de la esperada, conserva la extracción normal.
4. Si la longitud coincide, lee el contenido **en bloques de 256 KiB** y calcula CRC32 incremental con zlib (ya enlazada al cliente); comprueba cancelación y cede cooperativamente el hilo entre bloques.
5. Solo declara el archivo reutilizable después de **leer hasta EOF, cerrar limpiamente el descriptor, comprobar longitud y CRC32** frente a la entrada ZIP.
6. Cuando todo coincide: **no llama a `zip_fopen_index` ni abre el destino con `SCE_O_TRUNC`**. Contabiliza la entrada como completada; la interfaz muestra `ZIP files: N/M (reused K)` y los bytes reutilizados cuentan para la barra de progreso sin fingir que se han escrito otra vez.
7. Cuando falta o no coincide el contenido: se extrae con el **mismo código existente**, sin omitir parcialmente un archivo y sin borrar a ciegas el contenido previo.

No existe un manifiesto/resume journal en disco: esto evita confiar en un marcador que pudiera haber quedado desincronizado tras un apagado forzado. Tampoco se altera el ZIP original ni el catálogo.

### Optimización importante para carpetas con muchos archivos nuevos

Al encontrar un **archivo ausente** (`ENOENT`) en una carpeta, deja de ejecutar la comprobación adicional de reutilización en las siguientes entradas de ese mismo padre hasta cambiar de carpeta. En una extracción interrumpida normalmente hay un **prefijo existente** y después archivos nuevos. Esta heurística solo afecta al **rendimiento**, jamás a la corrección: los archivos que no se verifican se extraen normalmente, aunque alguno existiera más adelante.

Las ramas de staging profundo no reutilizan archivos previos para no invalidar el requisito de montar un árbol completo nuevo antes del movimiento de carpeta.

### Limitaciones de integridad

El ZIP estándar usa **CRC32, no un hash criptográfico**. Tamaño + CRC32 es mucho más seguro que omitir por nombre o tamaño, y coincide con el control de integridad habitual de ZIP; no constituye una garantía frente a colisiones CRC32 deliberadas. Un cambio de contenidos del ZIP con tamaños/CRC iguales tampoco se distingue criptográficamente. Si se necesita resistencia frente a manipulación intencional, será necesario un manifiesto confiable con hashes fuertes.

El reintento tampoco puede cancelar instantáneamente una llamada a `sceIoOpen` o `sceIoRead` si el kernel queda bloqueado; la cancelación se comprueba **entre llamadas**. El cuello de botella de directorios enormes (Minecraft `sound/bee`) **no se considera resuelto**. La lectura CRC podría tener un coste considerable en ese directorio.

## Progreso, trazas y memoria

- `ZipProgress.bytesWritten`: bytes realmente nuevos escritos durante esta ejecución (se mantiene la semántica anterior).
- `ZipProgress.bytesReused`: bytes ya existentes, comprobados y omitidos.
- `ZipProgress.entriesReused`: archivos del ZIP reutilizados y confirmados.
- El dispatcher presenta progreso total como `bytesWritten + bytesReused` y muestra `(reused K)` solo cuando procede.
- `[ZipResume]`: activación y checkpoints cada 256 archivos reutilizados.
- `[ZipPerf]`: totales `resume_checked`, `resume_verified`, `resume_mismatch`, `resume_bytes` en el resultado final.
- Se reutiliza el **mismo búfer de extracción de 256 KiB**; no se crea un índice en RAM proporcional al número de entradas.

## Pruebas automáticas

`tests/test_zip_resume_policy.cpp` comprueba coincidencia por fragmentos, CRC distinto con tamaño igual, truncamiento, longitud excesiva, lectura interrumpida, error de cierre, CRC declarado incorrecto y archivo vacío. Se ejecuta con zlib en `.github/workflows/client-build.yml`. Además, el cliente completo compila con VitaSDK si el workflow termina correctamente.

## Matriz física necesaria antes de merge

- [ ] Crear ZIP pequeño STORE controlado (3–10 archivos), extraerlo parcialmente e interrumpir; guardar ZIP y seleccionar **Retry extraction**. Comprobar `reused>0` y archivos finales correctos.
- [ ] Archivo existente con tamaño idéntico pero un byte cambiado: **no debe omitirse**; la extracción debe repararlo.
- [ ] Archivo existente truncado o vacío cuando la entrada ZIP no lo está: debe reescribirse. Archivo ZIP vacío y destino vacío también deben poder reutilizarse.
- [ ] Retrasar/cancelar la verificación de un archivo grande; al cancelar, ningún archivo preexistente debe borrarse ni truncarse por la fase de verificación.
- [ ] Ejecutar una instalación **normal** y comprobar que jamás reutiliza archivos (solo saved ZIP).
- [ ] ZIP segmentado de 2/4 rangos, ZIP64, DEFLATE, rutas profundas con staging, múltiples entradas con mismo nombre, espacio insuficiente y fallo de CRC.
- [ ] Comparar tiempos y respuesta de UI en la **misma Vita y medio**, con una carpeta controlada pequeña primero; **no repetir directamente Minecraft completo** hasta confirmar cancelación/seguridad.
- [ ] En una prueba controlada, observar que al primer `ENOENT` se deja de comprobar reuse para el resto del grupo, y se extraen archivos nuevos correctamente.
- [ ] Conservar `session.log`, versión/commit de prueba y detalles de la tarjeta/montaje.

## Archivos relevantes

- `include/archive/zip_resume_policy.hpp`: cálculo incremental CRC32 con tamaño.
- `source/archive/zip_extractor.cpp`: verificación en solo lectura y omisión de entradas confirmadas.
- `include/archive/zip_extractor.hpp`: contadores de progreso y flag explícito.
- `source/installer/install_dispatcher.cpp` / `include/installer/install_dispatcher.hpp`: propagación de flag y UI.
- `source/installer/install_controller.cpp`: habilitación solo en `workerSavedZipMain()`.
- `tests/test_zip_resume_policy.cpp` y `.github/workflows/client-build.yml`: regresiones.

**Criterio de integración:** checks de CI, build VPK y pruebas físicas anteriores aprobadas, sin pérdida de archivos y con cancelación recuperable. Mientras tanto, mantener en PR de borrador separado de #85.
