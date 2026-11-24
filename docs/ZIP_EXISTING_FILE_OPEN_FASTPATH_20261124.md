# PSVitaAlive — Optimización comprobada de aperturas de archivos existentes (extraída de PR #85)

**Fecha:** 24/11/2026.  
**Origen:** [PR #85](https://github.com/VegettoSan/PSVitaAlive/pull/85), Build experimental #645.  
**Implementación aislada:** `fix/pr85-existing-open-fastpath-20261124`.  
**Objetivo de integración:** `main` tras checks VitaSDK/host. **No cierra** [issue #111](https://github.com/VegettoSan/PSVitaAlive/issues/111).

## Qué solicitó el usuario

Integrar exclusivamente la mejora **ya observada en PS Vita real** que evita búsquedas/aperturas redundantes para archivos ZIP que existen en el destino. No se solicita el modo nuevo de «omitir archivos terminados» (PR #112) ni integrar el pipeline experimental ni modificar el ZIP de Minecraft.

## Qué demostró la prueba #645

En la PS Vita real se registraron `direct_existing=6193`, `direct_miss=1` al llegar a 6.665/10.637 entradas. Antes del primer archivo nuevo (alrededor del índice 6335), la ruta directa había abierto miles de archivos existentes sin intentar EXCL primero (`excl_ms=0` en esa fase). El sistema empezó a ralentizarse de nuevo cuando el extractor pasó a crear archivos nuevos dentro de `Minecraft/Common/Modern/sound/bee/`.

**Conclusión limitada:** se confirmó que el modo de apertura optimizado se ejecuta y evita syscalls redundantes. No se demostró resolución del congelamiento ni que todos los medios de almacenamiento rindan igual.

## Código portado sobre el `main` actualizado

- `ZipExistingOpenPolicy`: prioridad por carpeta. Al entrar en una nueva carpeta intenta directamente `SCE_O_WRONLY | SCE_O_TRUNC` **sin `SCE_O_CREAT`**. Así una apertura exitosa puede *solo* sobrescribir un archivo existente.
- Si la apertura directa devuelve **ENOENT (`0x80010002`)**, cambia a `SCE_O_CREAT | SCE_O_EXCL` para un grupo de archivos nuevos. Un EXCL exitoso identifica los archivos recién creados para la limpieza segura al fallar.
- Si EXCL devuelve **EEXIST (`0x80010011`)**, no hace `exists()+isDirectory()` extra: abre para reemplazar con `O_TRUNC` y prioriza existentes en adelante. Son precisamente las rutas verificadas en la rama experimental.
- Para otros errores conserva la ruta conservadora de comprobación original y no presume un error ENOENT/EEXIST inexistente.
- Se añaden métricas finales en `[ZipPerf]`: `direct_existing`, `direct_miss`, `fast_existing`, `checked_existing`, sin registrar cada archivo individualmente.
- Test host de la selección por directorio y transiciones de existente/nuevo; test agregado al workflow Build PS Vita Client.

**Importante:** Esta mejora **sigue reemplazando el contenido completo** de archivos existentes. `O_TRUNC` trunca archivos preexistentes; no los comprueba, no salta entradas y no implementa reanudación por CRC32. La propuesta de reutilización verificada es **PR #112, todavía borrador y separada**. No mezclar ambas funciones ni afirmar que se probó la omisión.

## Qué NO se integra

No se integran el pipeline de hilos ZIP STORE, las políticas `ZipWorkPacer` ni de directorios preflight nuevas, el perfilado detallado del PR #85 ni su lógica experimental en bloque, ni la reanudación inteligente del PR #112, ni ajustes de CMake del PR #85.

La apertura de archivos **nuevos** dentro de directorios muy grandes sigue siendo lenta y puede provocar congelamientos. Este problema se continúa estudiando en [issue #111](https://github.com/VegettoSan/PSVitaAlive/issues/111), con las [tres pruebas y plan por fases](MINECRAFT_ZIP_DIRECTORY_STALL_FOLLOWUP_20261124.md).

## Riesgos y validación

- Mantener la regla de «borrar en rollback únicamente archivos que se sabe que fueron creados por esta extracción».
- Confirmar en Vita real las rutas de archivos existentes, nuevos, mezcla, directorios, ZIP STORE/DEFLATE, ZIP segmentado/ZIP64, cancelación y recuperación. Las pruebas host y compilación prueban el código, pero **no demuestran que un directorio de Minecraft deje de bloquear la UI**.
- Proteger datos y partidas existentes. No hacer otra extracción completa de Minecraft hasta diseñar una prueba controlada para la ralentización de creación.

**Modelo de datos:** no se modificaron `apps/`, `authors/`, `categories/` ni catálogos generados. No se añadieron dependencias de servicios externos.
