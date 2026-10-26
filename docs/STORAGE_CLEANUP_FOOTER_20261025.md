# Almacenamiento PSVitaAlive: limpieza y panel UX0 (rama de prueba)

## Contrato aprobado

- **Install All conserva su recuperación automática a los 10 segundos** cuando una descarga ZIP completa falla durante la extracción. El resultado se incluye en su informe.
- Las descargas fallidas o canceladas, VPK/PKG temporales y staging abandonado se eliminan tan pronto como sea seguro, o en la limpieza de arranque después de una interrupción.
- Los ZIP conservados en `ux0:data/psvitaalive/manual/` **no se eliminan automáticamente**.
- Los trabajos anteriores con indicador `.psva_keep_zip` o ZIP antiguos ambiguos se consideran recuperables y no se purgan sin decisión del usuario.
- Los directorios `ux0:data/PSVA_ZIP_STAGE_*` y `uma0:data/PSVA_ZIP_STAGE_*` son temporales **solo** cuando cumplen el patrón numérico generado por PSVitaAlive.
- Los archivos extraídos **nuevos** se intentan eliminar si falla la extracción ZIP. Los archivos preexistentes se preservan para no destruir datos legítimos; si fueron truncados durante una sobrescritura fallida, no hay respaldo transaccional.

## UX0 footer → Storage Management

- Tocar el panel UX0 abre el gestor; `SELECT + SQUARE` lo abre con mando/PS TV desde el catálogo.
- Exploración y eliminación se ejecutan en un worker, sin bloquear `drawFooterBar()`.
- Muestra **temporales** y **ZIP conservados**, con eliminación individual o de todos los elementos permitidos, previa doble confirmación.
- Los tamaños de catálogo e imágenes son **solo informativos**: el panel no permite borrarlos.
- `config.json` nunca aparece y no se incluye entre las rutas eliminables.
- No se inicia limpieza desde el menú mientras el instalador esté ocupado.

## Rutas del inventario

| Ruta | Tratamiento |
|---|---|
| `ux0:data/psvitaalive/downloads/jobs/job_*/` | Temporal, salvo indicador de recuperación / ZIP antiguo ambiguo |
| `ux0:data/psvitaalive/manual/<app>/*.zip[.001..004]` con sidecar | ZIP guardado, solo eliminación manual |
| `ux0:data/psvitaalive/tmp/pkg_*.pkg`, `nested_source.vpk`, `nested_vpk_stage/`, `work.bin` | Temporal de propiedad conocida |
| `ux0:data/psva_vpk/` | Staging VPK |
| `ux0:data/PSVA_ZIP_STAGE_<n>_<n>/` y su homólogo uma0 | Staging ZIP generado |
| `ux0:data/psvitaalive/cache/catalog/` e `images/` | Solo lectura |
| `ux0:data/psvitaalive/config.json` | Nunca se explora para borrado |

## Validación

- VitaSDK CI: compilar VPK.
- Test de propiedad de rutas (incluye rechazo de `config.json`, cachés, rutas maliciosas).
- En PS Vita real y Vita3K: cancelar descarga; fallo MediaFire; ZIP completo y fallo de extracción; dejar pasar 10s en Install All y comprobar informe + ZIP guardado; ZIP segmentado; reintento offline; eliminar un ZIP y borrar todos desde footer; fallo de plugin; reiniciar tras cierre abrupto; comprobar cachés intactas.
- Comparar espacio libre antes/después. No fusionar con `main` hasta estas pruebas en dispositivo.

## Límites conocidos

La eliminación de archivos nuevos no puede revertir el contenido anterior de un archivo sobrescrito parcialmente. Tampoco es seguro borrar automáticamente `ux0:temp/app` sin verificar la recuperación del promotor. Las descargas BGDL del sistema pertenecen al gestor de descargas de PS Vita y no al directorio privado de PSVitaAlive.
