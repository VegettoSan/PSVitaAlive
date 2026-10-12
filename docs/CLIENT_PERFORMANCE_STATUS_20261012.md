# PSVitaAlive — Estado de integración y rendimiento del cliente (12-10-2026)

> **Documento de estado vigente** de las optimizaciones aprobadas y del experimento de índice rápido. Complementa los informes históricos de esta carpeta. Las frases “no tocar main”, “no fusionado” y las tareas pendientes de integración en informes escritos *antes* del PR #28 describen aquel momento; no revocan los hechos posteriores aquí verificados.

## 1. Decisión actual

- Las mejoras **aprobadas** de inicio *cache-first*, caché de imágenes v3 y creación de buckets desde el worker, junto con consulta de espacio `ux0:` **por eventos**, ya fueron integradas en `main` por [PR #28](https://github.com/VegettoSan/PSVitaAlive/pull/28) el **12-10-2026 22:26:13 UTC**.
- Commit de integración verificado: [`9060e790a3dadec7a43ef0913e5b2e53ac727aa1`](https://github.com/VegettoSan/PSVitaAlive/commit/9060e790a3dadec7a43ef0913e5b2e53ac727aa1). CI de compilación del cliente: [run 38316653996](https://github.com/VegettoSan/PSVitaAlive/actions/runs/38316653996), **SUCCESS**.
- Checkpoint del código aprobado con UX0 por eventos: `stable/ux0-event-driven-20261012` (`509f54ecece7aee273a30605a907a05728f71e4f`). Base previa validada en PS Vita para la corrección de buckets: `stable/async-image-buckets-20261012` (`321c806da986c2f37e1757d9376d19520478d79a`).
- **No fusionar ni publicar** el experimento `perf/catalog-fast-local-index-20261012` (`.fastidx`). La prueba de dos arranques fue **negativa**: no hubo acierto del índice ni mejora de arranque; además se observaron bloqueos importantes de la interfaz. El experimento **no está en `main`**.
- Conservar los ajustes exclusivos de `main` en `apps/halo-ce.json`. `catalog.json`, `authors.json` y `categories.json` son salidas generadas: **no editarlos manualmente**.
- Esta actualización de documentación **no implica cambios de código, publicación de release ni validación adicional de hardware**.

## 2. Qué está integrado y qué no está probado completamente

| Componente | Estado de integración | Evidencia / cautela |
| --- | --- | --- |
| Catálogo local *cache-first*, refresco sin reemplazo innecesario, inicio sin inventario masivo de imágenes | En `main` (PR #28) | Ensayos anteriores de inicio/imágenes en PS Vita; parser JSON tradicional continúa costando varios segundos |
| Caché v3 y creación de directorios/buckets fuera del hilo UI | En `main` | Corrección de buckets validada en hardware con caché de imágenes vacía y existente; **no elimina** la latencia de decodificación PNG |
| Preparación de imágenes bajo demanda; cancelación durante navegación; LRU de iconos acotada | En `main` | PNG/JPEG todavía se convierten a texturas mediante llamadas en el hilo UI; capturas grandes pueden bloquear |
| Lectura de espacio `ux0:` desde worker al inicio y al terminar trabajos relevantes | En `main` | CI OK; sigue **pendiente** comprobar en PS Vita real los disparos por instalaciones/descargas y el cierre limpio |
| Índice local `catalog.json.fastidx` | **No integrado** | Dos arranques del mismo VPK: `catalog_fast_miss` en ambos. Sin confirmación de `catalog_fast_build` ni `catalog_fast_hit` |
| Descargas VPK/ZIP, Archive.org, BGDL/PKG, zRIF, plugins, idiomas, favoritos y catálogos de juegos | Sin cambios por la documentación | Mantener pruebas de regresión si se modifica el cliente en el futuro |

La arquitectura Homebrew sigue siendo:

```text
apps/ + authors/ + categories/
  -> GitHub Actions (validación/generación)
  -> catalog.json + authors.json + categories.json
  -> cliente PS Vita y sitio web
```

Los catálogos de juegos PS Vita/PSP/PS1 siguen siendo independientes; no mezclarlos con Homebrew.

## 3. Experimento rápido del catálogo: resultados reales

**Rama aislada:** [`perf/catalog-fast-local-index-20261012`](https://github.com/VegettoSan/PSVitaAlive/tree/perf/catalog-fast-local-index-20261012). VPK probado con marcador `catalog-fast-local-index-20261012 main=untouched experiment=1`; build del experimento previamente compilado en [run 38315559178](https://github.com/VegettoSan/PSVitaAlive/actions/runs/38315559178). Los dos `session.log` fueron obtenidos por el mantenedor en PS Vita real el 12-10-2026, sin borrar previamente la caché.

| Medición | Primera apertura | Segunda apertura |
| --- | ---: | ---: |
| Aplicaciones Homebrew | 1.118 | 1.118 |
| `catalog_fast_miss` | Sí | Sí |
| `catalog_fast_hit` | No aparece | No aparece |
| `catalog_fast_build` | No aparece | No aparece |
| `parser_dom_end` | 4.829 ms | 4.749 ms |
| `parser_conversion_end` | 944 ms | 958 ms |
| `parser_complete` | **5.859 ms** | **5.773 ms** |
| `startup_homebrew_request` → `startup_screen_first_set` | **6,706 s** | **6,519 s** |
| Mayor `[Perf] slow frame` observado | **3,373 s** | **4,482 s** |

La lectura y conversión procede del JSON local de **2.101.356 bytes**. La segunda apertura no ahorra los ~5–6 segundos esperados: repite el parser DOM completo. **El objetivo de acelerar mediante `.fastidx` no se cumplió**.

En `CatalogManager::tryLoadDiskCache()` el *fast path* está condicionado a cargar un índice válido; si no, continúa con `CatalogParser::parseFile()`. `CatalogFastCache::store()` también requiere el sidecar `.zrifidx`. El camino de generación está situado en el worker después de publicar la caché. El código de serialización emite muchas escrituras pequeñas y puede competir por I/O, **pero los logs no permiten asegurar que esa sea la causa exacta** de la ausencia de `catalog_fast_build`: falta instrumentar la entrada, los retornos, el archivo temporal y la salida de `store()` en una rama aislada. No se debe describir el origen como confirmado.

**Atribución importante:** el VPK experimental partía de `stable/async-image-buckets-20261012`, **sin la corrección `ux0:` por eventos que sí tiene `main`**. No confundir diferencias entre ejecutables con una regresión de UX0 del código integrado. Tampoco hay prueba A/B suficiente para afirmar que el índice causó todos los bloqueos PNG.

## 4. Hallazgo paralelo: pausas al preparar iconos y capturas

En los dos registros se observaron decodificaciones `[Perf] texture decode slow` para `ns=app` y `ns=shot`. Ejemplos:

- Iconos: numerosos decodificados de **100–473 ms** en el hilo de interfaz.
- Una captura `ns=shot` tardó aproximadamente **1.410 ms**.
- Se repitieron decodificaciones de rutas `app_H_*.png` ya vistas después de desplazarse; el LRU mantiene solo **18** texturas de iconos y libera las menos recientes cuando necesita nuevas.
- Otros fotogramas duraron **2–4,48 segundos**, y no pueden explicarse sumando exclusivamente los valores informados por una sola decodificación.

En el cliente actual, `FullCatalogScreen::prepareImageTexture()` llama a `vita2d_load_PNG_file()` o `vita2d_load_JPEG_file()` desde la preparación del frame; `prepareVisibleTextures()` limita la entrada a **una textura nueva por frame**. El control evita múltiples decodificaciones en un frame normal, pero **una sola operación puede seguir bloqueando**. Además, `diagnostics::log()` escribe sincrónicamente y las trazas intensivas `[CATTRACE]`/`[Perf]` pueden amplificar las pausas.

Este problema es **independiente de que el JSON se procese en el worker**. Antes de incrementar límites de texturas, mover APIs GPU a otros hilos o implementar otro caché binario, medir por separado lectura PNG, decodificación, creación GPU, E/S en `ux0:`, liberaciones y coste del logger.

## 5. Recuperación y regla de regresión

1. Para volver a la base integrada, utilizar un VPK **compilado desde `main` @ `9060e790a3dadec7a43ef0913e5b2e53ac727aa1`** (Actions [run 38316653996](https://github.com/VegettoSan/PSVitaAlive/actions/runs/38316653996)). Ese ejecutable **no contiene** `CatalogFastCache`.
2. El cliente usa el Title ID `PSVAS1178`. Instalar encima del VPK experimental **no requiere borrar** `ux0:data/psvitaalive/cache/catalog/` ni `ux0:data/psvitaalive/cache/images/v3/`. Respaldar datos si el entorno es sensible.
3. Mantener `main` como fuente de la próxima compilación de referencia y la rama experimental aislada para investigación. **No hacer merge** de esa rama divergente; no recuperar código anterior que revierta UX0 por eventos ni los cambios de Halo CE.
4. Cualquier `.fastidx` preexistente es un derivado experimental descartable; el binario normal de `main` no lo utiliza. No borrar el JSON oficial ni su `.zrifidx` para “limpiar” el experimento.
5. **No considerar el problema de fotogramas resuelto** por regresar a `main`; solo se desactiva el camino experimental del índice.

## 6. Próximas pruebas (sin nuevas integraciones automáticas)

- Repetir navegación por Homebrew, carga de iconos y capturas en PS Vita real **con el VPK de `main` y la misma caché**. Comparar máximos y percentiles de frames con instrumentación mínima y medir memoria.
- Probar inicio con y sin red, cambio entre las cuatro pestañas, búsqueda, actualización real del JSON, soft refresh, texturas en detalle y navegación de retorno.
- Validar `ux0:` por eventos **en un build de `main`**: consulta inicial; ninguna llamada síncrona al dibujar; cambios después de descargas/instalaciones; terminación correcta del worker.
- Realizar smoke tests de VPK/ZIP, Archive.org, licencia zRIF, BGDL, PSP/PS1 y plugins tras cualquier cambio futuro.
- Si se retoma `.fastidx` en el futuro: rama **nueva desde el HEAD vigente de `main`**, diagnósticos de motivo `miss` y errores `store`, escritura agrupada si procede, comprobación de RAM/integridad, primer y segundo arranque, offline/hot-swap y fallback robusto. Requiere aprobación y ensayo de Vita real **antes de merge**.

El archivo `ux0:data/psvitaalive/logs/session.log` se reinicia al iniciar; **guardar una copia por cada sesión antes de abrir de nuevo**.

## 7. Referencias de la carpeta `docs/`

- [Inicio cache-first](STARTUP_CACHE_OPTIMIZATION_20261012.md): decisiones históricas de arranque.
- [Buckets de imágenes en worker](IMAGE_BUCKET_MAIN_THREAD_FIX_20261012.md): causa y validaciones en hardware.
- [Regresión de descargas de imágenes](IMAGE_DOWNLOAD_REGRESSION_20261012.md): ensayos y recuperación del throughput.
- [UX0 asíncrono](ASYNC_UX0_SPACE_FIX_20261012.md) y [UX0 por eventos](EVENT_DRIVEN_UX0_REFRESH_20261012.md): evolución y pruebas pendientes.
- [Image cache v3](IMAGE_CACHE.md): funcionamiento vigente; no confundir caché de archivos PNG con caché de texturas GPU.
- [Diagnóstico de catálogo](DIAGNOSTIC_CATALOG_STARTUP.md): trazas previas y su interpretación.
- [Documento del índice rápido (solo rama experimental)](https://github.com/VegettoSan/PSVitaAlive/blob/perf/catalog-fast-local-index-20261012/docs/HOMEBREW_FAST_CATALOG_INDEX_20261012.md): diseño propuesto, **no adoptado**.

**Decisión de mantenimiento (12-10-2026):** integrar solamente lo aprobado (ya aplicado en PR #28), documentar el resultado negativo del índice, preservar `main` y no prometer fluidez de imágenes aún no verificada.
