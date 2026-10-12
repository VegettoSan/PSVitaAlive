# PSVitaAlive — Evitar bloqueos al crear directorios de imágenes

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
