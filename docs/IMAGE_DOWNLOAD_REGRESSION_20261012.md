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
3. **Metadata y descubrimiento de nodos Archive.org restaurados**
   al comportamiento original: `CONNECTTIMEOUT=15 s` y
   `TIMEOUT=25 s`, incluso para imágenes. Se había probado un límite
   de 4/8 s para imágenes, pero se revierte preventivamente porque
   podía impedir descubrir un nodo alternativo bajo conexiones lentas.
   El algoritmo de failover, la lista y el ranking no se modifican.
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

## Auditoría posterior solicitada: ¿se eliminó la lógica de nodos al quitar archive_nodes.log?

**Resultado del análisis de commits:** No.

Los commits exclusivos de retirada de registros:
- `880d6c2f2360c0f8cad2301d5ff60c861a857473`: se eliminó únicamente la
  configuración, creación, escritura y mensajes de `archive_nodes.log`
  en `source/diagnostic_logger.cpp`.
- `b12670ad0158469682f613f6ba240b4ed287f62d`: se retiró la
  declaración pública `archiveNodeLog`.
- `6eb3b67bede4e57592c8814bd573641d88a520b7`: únicamente se convirtió
  `archiveNodeDiagnostic(...)` en un macro sin operación que descarta los
  argumentos; no tocó `buildArchiveAlternateUrls`, `rankArchiveAlternateUrls`,
  `probeArchiveStorageUrl`, descarga, TLS ni condiciones de failover.
- `311495b7a7df80bcab3349e9c6c98eb86c7b0bb8`: borra la copia anterior
  del **archivo de log** al iniciar, no datos ni caché de nodos.

Verificación de contenido entre la rama original de trazas
`diagnostics/catalog-startup-trace-20261012` y la rama actual:
- `parseArchiveDownloadParts`: igual.
- `hostLooksLikeBadArchiveEdge`: igual.
- `probeArchiveStorageUrl`: igual.
- `rankArchiveAlternateUrls`: igual.
- `buildArchiveAlternateUrls` y `fetchArchiveMetadataJson`: en la
  corrección de rendimiento de imágenes se había añadido un argumento
  `lightweight` y límites menores; **se revirtió ese experimento** en el commit
  `47e6fce858fde4ef38d039ecf28dc8bb5ac664fa` para conservar
  las funciones y plazos originales. El archivo de log sigue desactivado.

**Distinción necesaria:** la optimización *separada* del rendimiento de
imágenes conserva cambios en los plazos de descarga de **iconos**
(`CURLOPT_TIMEOUT=18`, `CONNECTTIMEOUT=7` para esa transferencia),
intentos HTTP de iconos Archive.org (3), cooldown tras HTTP 5xx y
cancelación por salida de pantalla (1,5 s). Esto NO elimina el mecanismo
de búsqueda de nodos ni modifica sus metadatos, pero **sí cambia**
cuánto se intenta descargar cada icono. Las reglas para VPK/PKG y archivos
grandes continúan sin alterarse. No se debe describir esto como
“solo cambios de logging” cuando se habla de toda la rama de optimización.

La rama `main` no recibió estos cambios. La nueva versión debe
**recompilarse** después de restaurar la API de metadata: cualquier VPK
anterior al commit de restauración es anterior a esta auditoría.

## Tercera ronda: validación real favorable, después de restaurar los nodos

**Fuente:** nuevas sesiones de PS Vita aportadas por el mantenedor:
`session cache.log` (227 líneas aprox.) y `session no cache.log`
(419 líneas aprox.), posteriores al VPK
`PSVitaAlive-Archive-Nodes-Preserved-20261012.vpk`.
Ambos registros contienen el marcador
`image-download-throughput-20261012`. Este marcador identifica la
familia de compilación y no contiene el SHA exacto del commit.

**Comentarios del mantenedor:** «ya mejora bastante».

| Métrica observada | Anterior: caché | Actual: caché | Anterior: sin caché | Actual: sin caché |
|---|---:|---:|---:|---:|
| `[ImageCache] ready` (iconos nuevos terminados) | 0 | **3** | 2 | **18** |
| `[ImageCache] cancelled` | 6 | **0** | 5 | **0** |
| `pruned off-screen active_cancel=1` | presente | **0** | presente | **0** |
| Respuestas `HTTP response status=500/502` | 4 | **0** | 5 | **0** |
| `image_job_scan` completo y bloqueante | ninguno en versión anterior optimizada | **ninguno** | ninguno | **ninguno** |

En el último ensayo, todas las descargas de imágenes **terminadas** en
ambos registros tienen éxito HTTP 200; el ensayo sin caché inicia una
descarga adicional al final del log que no tiene resultado registrado, por
lo que no debe clasificarse como error ni éxito.

**Tiempos de entrada medidos con `[CATTRACE]`:**
- Con caché: `startup_homebrew_request=3.098679 s` y
  `startup_screen_first_set=9.623669 s`, diferencia **6.525 s**.
  `parser_complete` informa **5.875 s**; el flujo omite inventario inicial
  de imágenes. Los iconos inicialmente visibles pueden estar ya cacheados;
  por eso **3 nuevos** no representa el total de iconos vistos.
- Sin caché: solicitud **4.096333 s**, primera entrega **18.222657 s**,
  diferencia **14.126 s**, con descarga de catálogo y parse
  **6.170 s**. El flujo también omite el inventario inicial masivo.

**Conclusión validada en estas sesiones:** la versión reparada conserva el
beneficio de arranque y muestra una recuperación marcada de las descargas
de iconos. En el log sin caché pasó de 2 a 18 imágenes terminadas;
desaparecen las cancelaciones reportadas. No atribuir necesariamente
todos los éxitos a cambios locales: en estas sesiones tampoco se registran
HTTP 500/502 del servidor, por lo que el entorno de red era distinto.

**Riesgo pendiente observado:** el log sin caché todavía presenta frames
aislados lentos (máximo `2397259 us`, **2,397 s**, cerca del comienzo de
las peticiones de imágenes); con caché el máximo observado fue
`129523 us` (≈130 ms). Su causa exacta NO está determinada con estas
trazas. Optimizar frames/pipeline GPU y el parse JSON (~6 s) sería
trabajo separado, condicionado a medidas de RAM y pruebas de regresión.

**Decisión de fase:** no hacer más cambios solo por intuición y no
fusionar a `main` automáticamente. Conservar rama, VPK, evidencias,
regresiones y la posibilidad de volver a la versión anterior. Confirmar
antes de cualquier release: pestañas, capturas, detalles, precarga opcional,
actualización en caliente, VPK/PKG, failover de descargas grandes y entorno
sin Internet.
