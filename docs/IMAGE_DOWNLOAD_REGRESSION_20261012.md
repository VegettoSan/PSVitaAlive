# PSVitaAlive — regresión de descarga de imágenes: análisis y reparación

Fecha: 2026-10-12. Rama `fix/image-download-throughput-20261012`.
Base: `fix/catalog-startup-nonblocking-20261012`. **No modificar main.**

## Evidencia proporcionada en la segunda ronda

Se ejecutó el VPK optimizado de arranque en consola real y el mantenedor reportó
solo dos iconos nuevos y una descarga de imágenes notablemente más lenta.

| Indicador | session cache.log | session no cache.log |
|---|---:|---:|
| `startup_images inventory=skipped_on_demand` | sí | sí |
| `image_job_scan` (bloqueo anterior) | 0 | 0 |
| `[ImageCache] ready` | 0 | 2 |
| `[ImageCache] cancelled` | 6 | 5 |
| `pruned off-screen` | 12 | 13 |
| Respuestas explícitas 500/502 | 4 | 5 |
| `HTTP BEGIN` | 7 | 9 |

En `session cache.log`, `Shadow Guardian/icon0.png` comienza cerca de 12,3 s,
recibe HTTP 502/500 varias veces y termina cancelado alrededor de 75,2 s.
En `session no cache.log`, Paper Mario y Sonic R consiguen iconos tras usar
failover de Archive.org; varias otras solicitudes se cancelan al cambiar la
vista. Los tiempos de sistema del logger y los tiempos HTTP tienen orígenes
distintos: comparar solo eventos dentro de la misma escala.

## Diagnóstico / límites de la evidencia

- **Confirmado:** el arranque rápido sí elimina el escaneo en la UI, tanto
  con caché como sin ella.
- **Confirmado:** numerosos HTTP 500/502 de Archive.org, failover y
  cancelaciones desperdician tiempo de su único trabajador de imágenes.
- **Confirmado por código:** `ImageCache::cancelQueuedExcept()` solicitaba
  cancelar inmediatamente un trabajo activo cuando el conjunto de imágenes
  visibles dejaba de incluir su ruta. Durante animación, navegación y
  cambios de pantalla esto favorece el patrón cancelar/reiniciar.
- **Confirmado por código:** el cliente daba ocho intentos HTTP por imagen;
  `fetchArchiveMetadataJson()` permitía hasta 25 segundos cuando el
  servidor fallaba. Un icono roto podía retrasar a todos los siguientes.
- **No confirmado:** que el guard de estado de red implementado en la
  optimización anterior fuese el origen de la lentitud; en estos logs hay
  peticiones HTTP conectadas y respuestas de servidor.
- **No confirmado:** que los HTTP 500/502 sean causados por un cambio del
  código. Dependen del servidor remoto y en la ejecución anterior también
  hubo fallos transitorios de Archive.org.

## Cambios experimentales para corregir el rendimiento

1. **3 intentos por imagen de Archive.org** frente a 8. Las imágenes
   de GitHub/GitLab/otras fuentes mantienen 8, y todas las descargas
   del instalador siguen intactas.
2. **Iconos de Archive.org** (rutas `/cache/images/v3/*/app_*`):
   `CONNECTTIMEOUT=7 s`, `TIMEOUT=18 s` y
   `LOW_SPEED_TIME=12 s` por intento. No restringe VPK, PKG, datos
   grandes ni descargas normales. Las capturas mantienen los límites anteriores.
3. **Obtención de metadata rápida solo para imágenes**:
   `CONNECTTIMEOUT=4 s`, `TIMEOUT=8 s`; para demás descargas,
   continúan los plazos de 15/25 s. Conserva failover y candidatos.
4. **Reintentos de 5xx de Archive.org aplazados**: un archivo de imagen que agota sus
   intentos con HTTP 5xx tiene enfriamiento de 45 s antes de
   ocupar de nuevo el trabajador. Los demás errores conservan 3 s.
5. **Debounce de cancelación activa de 1,5 s**: la cola que ya no es
   visible se limpia como antes, pero el trabajo en curso solo recibe
   cancelación automática si lleva 1,5 segundos fuera de pantalla.
   `cancelAll()` y las cancelaciones explícitas de instalación/tab
   conservan su funcionamiento inmediato.
6. **Nuevo marcador de diagnóstico**:
   `[CATTRACE] build_marker ... image-download-throughput-20261012`.
   Permite identificar la versión correcta de las pruebas.

El registro especializado `archive_nodes.log` permanece desactivado, pero
`session.log` conserva eventos HTTP y de imagen necesarios para comprobar
el origen de fallos.

## Criterios de aceptación / nuevas pruebas en Vita

1. Arranque con caché + Internet: sigue apareciendo
   `inventory=skipped_on_demand`; no regresa el bloqueo 7–10 s.
2. Sin caché + Internet: permanecer en la primera pantalla visible
   durante al menos 1 minuto, sin cambiar la selección. Contar
   `[ImageCache] ready` y `[ImageCache] failed`.
3. Repetir en la misma pantalla, **sin desplazarse**: la cantidad de
   `[ImageCache] cancelled` por scroll debe ser 0 o muy baja.
4. Desplazamiento rápido durante 20–30 s: comprobar que no se cancelan
   reiteradamente las mismas URLs, y que al detenerse vuelve el tráfico.
5. Detalle con capturas: cargas, cancelaciones conscientes, botones y
   navegación funcionan. Los screenshots no usan TIMEOUT=18 s.
6. Imágenes offline: se siguen mostrando las cacheadas; al volver
   Internet aparecen las otras sin reiniciar el cliente.
7. VPK/PKG/game files desde Archive.org: reintentos, failover,
   descargas grandes y resume permanecen como antes.
8. Confirmar que no se recrea `archive_nodes.log`.

Tomar **logs completos por prueba**, ya que `session.log` se
reinicia con cada arranque. Comparar con la primera y segunda ronda.
No fusionar hasta demostrar que la tasa de imágenes listas mejora
en el mismo entorno con errores HTTP comparables.

## Limitaciones y trabajo futuro

- Un Archive.org lento o con HTTP 500/502 no puede hacerse rápido desde
  el cliente: ahora se abandona antes el icono problemático para no
  impedir la descarga de otros.
- La única hebra de ImageCache sigue serializando las imágenes:
  **no se añade paralelismo** por riesgo de RAM/libcurl, hasta medirlo.
- La decodificación de una textura PNG todavía puede producir
  frames >100 ms; sería un trabajo separado para no mezclar causas.
- El contador de imágenes descargadas no se puede equiparar a imágenes
  visibles reutilizadas de caché. Se deben comparar ambas métricas.

## Rollback

- Rama anterior con arranque mejorado:
  `fix/catalog-startup-nonblocking-20261012`.
- Rama de instrumentación original:
  `diagnostics/catalog-startup-trace-20261012`.
- `main` inalterada.
- Title ID de VPK de pruebas: `PSVAS1178`.
