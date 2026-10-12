# PSVitaAlive — Evitar bloqueos al crear directorios de imágenes

> **Estado actualizado (12-10-2026):** esta corrección fue validada en PS Vita real e **integrada en `main` por PR #28**, junto con otras optimizaciones aprobadas. Las referencias históricas siguientes a “main no modificada” y “no integrar automáticamente” corresponden a la etapa de diagnóstico anterior al merge, no son el estado presente. Persisten pausas al decodificar texturas PNG; ver [estado vigente y prueba fallida del índice](CLIENT_PERFORMANCE_STATUS_20261012.md).

Fecha: 2026-10-12.

**Base estable preservada:** `stable/startup-image-cache-20261012`.
**Rama con corrección aislada:** `fix/async-image-buckets-20261012`.
**Rama de origen del diagnóstico:** `perf/frame-rootcause-20261012`.
**main no modificada**. No fusionar sin validación con PS Vita real.

## Diagnóstico a partir de session.log (prueba sin caché)

El registro informa:
- Marcador `frame-rootcause-profile-20261012`.
- `[ImageCache] startup cache size=0`.
- Catálogo Homebrew: 1118 aplicaciones / 2.101.356 bytes.
- `parser_dom_end duration_ms=5052`; conversión ~995 ms;
  `parser_complete duration_ms=6221`. Procesamiento asíncrono existente.
- Entre `startup_homebrew_request t_us=4302926` y
  `startup_screen_first_set t_us=19084093` transcurren **14,781 s**:
  incluye la descarga del catálogo y la conversión JSON.
- Imágenes completadas: **48** (42 iconos + 6 capturas).
- `[ImageCache] failed` = 0, HTTP 500/502 = 0;
  `[ImageCache] cancelled` = 1 (icono Vita Media Deck,
  fuera de pantalla tras descarga, a verificar si se repite).

El verdadero cuello de botella se capturó en
**42 eventos `[PERFIMGREQ]`**, correspondientes a la preparación de
solicitudes de imágenes nuevas en el hilo UI:

| Métrica | Suma | Mediana | Máximo |
|---|---:|---:|---:|
| `total_ms` | 8.508 ms | 139 ms | 1389 ms |
| `directory_ms` | **8.121 ms** | **127 ms** | **1382 ms** |
| `stat_ms` | 355 ms | 8 ms | 25 ms |
| `enqueue_ms` (resolución en ms) | 0 | 0 | 0 |

**Ojo:** el log redondea a milisegundos cada etapa; `enqueue_ms=0`
no equivale a un coste cero literal.

Los fotogramas con más bloqueo se alinean con operaciones de directorio:
- `[PERFSEG] total_ms=2707 images_ms=2696`, con
  `[PERFIMGVIEW] load_ms=2685` y ocho solicitudes en las que
  una sola dio `directory_ms=1382`.
- `total_ms=1853 images_ms=1821`: nueve solicitudes de carpeta
  en la misma vista.
- `total_ms=1767 images_ms=1750`: directorio individual 1320 ms.
- `total_ms=1539 images_ms=1522`: ocho solicitudes nuevas.

Código: `ImageCache::request()` llamaba en el hilo UI a
`ensureDirectory(parent)`, que delega a
`StorageManager::createDirectories()` y genera varias operaciones
`sceIoGetstat`/ `sceIoMkdir` sobre `ux0:`.

## Modificación mínima

Solo cambiar `Client PSVitaAlive/source/ui/image_cache.cpp`:
1. **Eliminar la comprobación/creación de carpeta del método
   `ImageCache::request()`**, que llama el hilo de UI.
2. **Hacer esa misma comprobación/creación dentro de
   `ImageCache::workerMain()`**, después de extraer un trabajo y antes
   de llamar a `HttpClient::downloadToFile()`. Esa llamada está fuera
   del mutex del caché; así no retiene el lock durante I/O.
3. Si crear carpeta falla, log `[ImageCache] cannot create image bucket`,
   devolver `HttpResult::IoError` al mismo flujo de reintentos/errores
   ya existente y no intentar escribir a un directorio inexistente.
4. Mantener exactamente el nombre, bucket, formato v3 y selección de
   recursos; no cambiar los plazos Archive.org, TLS, catálogos,
   estructura JSON ni descargas de VPK/PKG.

Con esta modificación, las primeras solicitudes todavía hacen
`sceIoGetstat` para reconocer un fichero en caché (mediana 8 ms en
esta prueba), pero el coste de crear directorios deja de ejecutarse en
el hilo de renderizado.

**Límite de lo comprobado:** el cambio no demuestra todavía una mejora
en hardware; el VPK VitaSDK debe compilar y probarse sin caché.

## Problema secundario detectado (no corregido en esta rama)

En pantalla de detalle:
`[PERFDETAIL] total_ms=1221 header_ms=0 list_ms=2 detail_ms=3
overlays_ms=1214 finish_ms=0`.

El código confirma una espera dentro del bloque de operaciones después
de `drawDetailPanel` y antes de `finishFrameWithCommonDialog`,
pero todavía no se identificó cuál llamada concreta es responsable.
**No atribuir por defecto a `vita2d_swap_buffers`**: el tiempo
registrado para `finish_ms` fue 0 ms en ese fotograma.
Se requieren mediciones específicas de footer, chips, overlays y toast
en fase aparte, una vez validada esta corrección.

Asimismo, el decoder PNG de texturas puede costar ~30-139 ms
en operaciones individuales y la normalización de capturas usa
el trabajador de imágenes con costes ~0,5-1 s. No confundirlos con la
creación de directorios.

## Prueba de aceptación

1. Compilar rama de corrección con VitaSDK en GitHub Actions y revisar
   paquete VPK (ZIP íntegro, eboot.bin, param.sfo).
2. Instalar, iniciar con **caché vacía** y recorrer tres zonas de
   catálogo. Debe continuar llenando iconos sin bloqueos de
   1-3 segundos al preparar nuevas carpetas.
3. Entrar en vistas de detalle y observar capturas. No deben aparecer
   errores `cannot create image bucket`, `failed` ni fallos de escritura.
4. Reiniciar **con caché**, comprobar que el mismo icono no se vuelve a
   descargar y que todas las texturas siguen apareciendo.
5. Comparar marcadores de arranque, `[Perf] slow frame`,
   `[ImageCache] ready`, `[ImageCache] cancelled` y
   tiempos HTTP. Mantener nodos Archive.org idénticos.
6. Si todo funciona, considerar actualización de la referencia
   estable de forma explícita y solo entonces eliminar instrumentación
   temporal. No tocar `main` automáticamente.

## Notas de compatibilidad

Sin cambios para `apps/`, `authors/`, `categories/`, archivos
generados `catalog.json`, `authors.json`, `categories.json`,
Title IDs, enlaces, plugins, VPK/PKG, ZIP ni motores de actualización.
Los datos de origen externo siguen siendo solo insumos para el catálogo
oficial de PSVitaAlive.

## Primera validación de la corrección en PS Vita real (12/10/2026)

**Fuente:** nueva sesión enviada por el mantenedor: `session.log`,
539 líneas, marcador exacto
`[CATTRACE] build_marker ... async-image-buckets-20261012`.

Esta sesión **NO es una prueba de caché vacía**: inicia con
`[ImageCache] startup cache size=1404255` bytes (1,40 MB).
Se debe comparar con cautela respecto al ensayo anterior,
que tenía `startup cache size=0`.

### Hallazgos de esta sesión

- Homebrew de 1118 registros servido desde la caché de catálogo:
  `startup_homebrew_request t_us=3347920`;
  `startup_screen_first_set t_us=10370266` = **7,022 s**.
- Procesamiento JSON todavía consume `parser_dom_end=5072 ms`,
  `parser_conversion_end=1005 ms`, `parser_complete=6160 ms`.
  El cacheado de imágenes no resuelve este coste de catálogo.
- **13 imágenes nuevas terminadas**: 9 `app` y 4 `shot`.
  Sin líneas `[ImageCache] failed` ni
  `cannot create image bucket`: evidencia favorable de que el
  trabajador crea correctamente nuevas carpetas.
- **2 cancelaciones de capturas** de Halo CE mientras el usuario
  cambiaba la zona visible; no se registran cancelaciones de iconos.
- **2 HTTP 500 transitorios**, ambos seguidos del failover
  Archive.org con otro nodo y HTTP 200 exitoso. El sistema de
  nodos se conserva funcional.
- **77 eventos** `[Perf] slow frame`; máximo registrado
  **642,620 ms**, mediana de esos eventos **83,863 ms**.
  Solo un fotograma registrado supera 500 ms.
- Decodificaciones registradas de 47 texturas con log `slow`:
  máximo **197,111 ms**, mediana **41,877 ms**.
- Todavía hay pausas de **490 ms, 398 ms y ~370 ms**,
  varias durante desplazamientos que liberan/recrean
  texturas (`deferred=9`). La asignación exacta de ese
  tiempo no se puede determinar en este build: al partir de
  la base estable, **no incluye** la instrumentación experimental
  `[PERFSEG]`/`[PERFIMGVIEW]`. No afirmar que esos
  fotogramas se deban al sistema de directorios.
- En esta sesión no se reproduce el bloqueo de 1,2 s
  del dibujado de detalle visto en la sesión diagnóstica,
  pero eso **no demuestra** que haya desaparecido para todos
  los usuarios.

### Interpretación responsable

**La corrección pasa las pruebas funcionales observadas**:
el catálogo se presenta, las imágenes nuevas se descargan y normalizan,
las texturas aparecen y Archive.org continúa con failover. El fotograma
peor registrado (642,6 ms) es mucho menor que el peor de la prueba
previa de caché vacía (2707,3 ms), pero **las condiciones de
caché son diferentes**, así que no se puede afirmar una
reducción porcentual causal ni completar aún la validación
de rendimiento en arranque frío.

**Prueba restante requerida antes de actualizar la base estable:**
arrancar este mismo VPK con `cache/images/v3` sin imágenes
(sin borrar catálogos, configuraciones ni descargas), recorrer
varias zonas del catálogo, conservar `session.log`,
comprobar ausencia de errores de carpetas y de pausas de 1-3 s,
y luego repetir con caché. La rama estable anterior no se toca.


## Validación final con caché de imágenes vacía — 12/10/2026

Archivo de prueba: `session.log` proporcionado por el mantenedor;
847 líneas, marcador `async-image-buckets-20261012`.

**Condiciones verificadas**:
- `[ImageCache] startup cache size=0` y worker
  `cache=v3 replacement=per-image`. Esta vez sí es caché vacía.
- Además hubo **`disk_miss` en el catálogo JSON**: se descargó
  `catalog.json` (2.101.356 bytes, 1118 aplicaciones). Por ello,
  el tiempo total de inicio incluye descarga y no puede compararse
  directamente con una sesión de catálogo almacenado.
- `startup_homebrew_request t_us=3989548` a
  `startup_screen_first_set t_us=17467872` = **13,478 s**.
  Parse `parser_complete duration_ms=6113` (DOM 4916,
  conversión 993 ms): problema de arranque independiente.

**Resultado del caché y sus descargas**:
- `[ImageCache] ready` **44**, exactamente **36 iconos** y
  **8 capturas**;
- `[ImageCache] failed` **0**,
  `[ImageCache] cancelled` **0**,
  `cannot create image bucket` **0**;
- 3 respuestas HTTP 500 transitorias (Monster Shooter,
  Crash Team Racing High Octane, Gravity Guy). En las tres el
  *failover de Archive.org* cambió a nodo alternativo y respondió
  HTTP 200. No se alteraron las estrategias de nodos.
- Se liberaron y cargaron texturas al desplazarse y hubo capturas
  nuevas de Dragon Ball Tap Battle y Halo CE.
- El funcionamiento del cambio **queda validado con caché vacía**:
  el worker crea carpetas y guarda correctamente los recursos.

**Rendimiento residual**:
- `[Perf] slow frame`: **107 eventos** registrados; máximo
  **1624,985 ms**, mediana de esos eventos **66,754 ms**.
  Importante: son registros de fotogramas *lentos*, no muestra de
  todos los fotogramas, y existe limitación temporal del logger.
- El fotograma de **1624,985 ms** contiene
  `[Perf] texture decode slow us=1376709` para
  `.../A5/app_H_A5D4B85DED59DF32_78EE24D1.png` (icono de
  Asphalt 5). De manera directa, el tiempo dentro de la llamada
  de carga de PNG supera **1,376 s**; esta medición es de reloj
  transcurrido, podría incluir planificación e I/O, no únicamente
  cálculo de decodificación.
- También fotogramas de **1129,254 ms** durante capturas de Halo CE,
  **706,629 ms** durante navegación e imágenes nuevas, y
  **406,901 ms** con `deferred=9`. No disponemos de
  `[PERFSEG]` en esta build, así que no asignar los tiempos
  de esos fotogramas a llamadas específicas sin medirlas.
- En la anterior prueba *también* sin caché, la mayor pausa fue
  **2707 ms**, con un bloqueo de creación de directorio medido
  de **1382 ms**. En esta prueba la mayor pausa es
  ~**40% menor**, pero se navegaron imágenes y condiciones distintas;
  esto **no prueba** una reducción causal exacta de la latencia.
  El cambio estructural sí elimina el trabajo pesado de directorio
  de `ImageCache::request()` en el hilo UI.

**Veredicto**: corrección de directorios **validada funcionalmente**
en hardware con imágenes vacías y no vacías, lista para
**congelarse como referencia estable independiente**, sin
confundirla con una eliminación completa de stutters.
No publicar una release ni integrar `main` automáticamente.

**Próxima fase propuesta**: medir el tiempo de
`vita2d_load_PNG_file()` (carga del icono, lectura de disco,
decoder/libpng, creación de textura GPU, CPU scheduling) y el
fotograma de Halo CE de 1,129 s antes de introducir optimizaciones.
Posponer el caché binario del JSON, que exige compatibilidad
cuidadosa, a otra fase. Mantener la versión anterior
`stable/startup-image-cache-20261012` como rollback.
