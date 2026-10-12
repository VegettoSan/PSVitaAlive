# PSVitaAlive — Inicio cache-first sin bloqueos (experimento del 12-10-2026)

> **Estado actualizado (12-10-2026):** las mejoras aprobadas de inicio *cache-first* y carga de imágenes se integraron en `main` mediante [PR #28](https://github.com/VegettoSan/PSVitaAlive/pull/28). El texto inferior conserva la cronología y las instrucciones de la rama experimental original; sus frases “main no se ha modificado” ya **no describen el estado actual**. El índice binario posterior **no** se integró y falló la prueba de dos arranques. Ver [estado vigente y recuperación](CLIENT_PERFORMANCE_STATUS_20261012.md).

## Estado y alcance

- **Rama:** `fix/catalog-startup-nonblocking-20261012` (creada desde
  `diagnostics/catalog-startup-trace-20261012`, **no desde main**).
- **Estado de validación:** pendiente de prueba real A/B en PS Vita / Vita3K.
  Compilar no equivale a garantizar ausencia de bloqueos.
- **Sin cambios** en catálogo oficial, formato JSON, IDs, enlaces, instalaciones,
  configuración de idioma, URL de descarga ni lógica de selección/failover de Archive.org.
- Los marcadores `[CATTRACE]` se conservan en esta rama *solo para comparar resultados*;
  no deberían pasar a una release final sin revisar el coste de diagnóstico.

## Problema reproducido — mediciones originales (1.118 registros, JSON ≈2.1 MB)

Datos provistos por el mantenedor, compilación `catalog-startup-trace-20261012`:

| Caso | Parser DOM | Conversión | Inventario de imágenes | Observaciones |
|---|---:|---:|---:|---|
| Primera apertura / caché vacía | 5.085 s | 1.022 s | 6.795 s, 4.406 pendientes | primero descarga remota; después procesa |
| Sin Internet / caché existente | 5.048 s | 0.980 s | 7.103 s, 4.388 pendientes | cache-first SÍ funciona |
| Actualización remota / caché existente | 5.298 s local | 0.988 s local | 10.267 s, 4.388 pendientes | ETag cambió; se ejecutó descarga y segundo parse |

En tercera prueba el segundo parse fue 8.329 s DOM + 2.174 s conversión,
con competencia por CPU/almacenamiento. **Los registros no prueban que el
contenido descargado fuese idéntico:** solo prueban cambio de ETag.

**Causa principal demostrada:** `collectCatalogImages()` recorre y hace
`sceIoGetstat()` para miles de imágenes en el hilo de UI antes de navegar,
incluso si `promptImageWarmup=false`. La barra parece congelada.

**Causas secundarias:** segundo `setCatalogItems()`/sort/reset en el
mismo arranque; segundo parse completo tras ETag distinto sin comparar bytes;
`setActiveCatalog()` en un hot-swap resetea navegación; descargas de
imágenes DNS con 8 intentos desconectado. El parse JSON original aún tarda
≈6 s y NO se optimiza en esta etapa por riesgo de RAM/compatibilidad.

## Cambios implementados

1. **Bajo demanda**: con `promptImageWarmup=false` se omite por completo
   inventario de miles de imágenes. La carga de imágenes visibles sigue
   siendo on-demand mediante `ImageCache::request()`.
2. **Asistente de descarga de todas las imágenes**: con
   `promptImageWarmup=true`, inventario incremental de **máximo 8 aplicaciones
   o 3 ms por frame**. Mantiene detección de faltantes, confirmación y
   `runImageWarmup()`. Se seguirá esperando al consentimiento del usuario
   antes de descargar masivamente.
3. **Primera activación única**: entrega del vector por `std::move` al
   primer `screen.setCatalogItems()`, sin segunda activación ni copia
   permanente del catálogo (excepto buffer opcional para inventario).
4. **Actualización silenciosa con posición conservada**:
   `refreshCatalogItems()` mantiene búsqueda, filtro, ID seleccionado,
   scroll, posición de detalle y texturas GPU compatibles. Si un elemento
   desaparece durante un detalle, cierra el detalle de manera segura.
   Se limpia la caché de estados de instalación porque la versión pudo cambiar.
5. **Cache-first sincronizado**: el worker espera a que el hilo UI consuma
   la publicación local antes de comenzar refresh; cancela la espera si el
   usuario cambia de catálogo. Así no sobrescribe la caché pendiente con
   un resultado remoto por usar `readyPending_` como ranura única.
6. **Contenido remoto idéntico**: si el ETag cambia pero la descarga es
   byte a byte igual, la comparación de streaming (búfer de 4 KiB por archivo)
   omite el segundo DOM/repintado y actualiza `.meta`. Si cambió realmente,
   conserva parseo y validación existentes.
7. **Barra de progreso honesta**: al terminar descarga y entrar en
   parse/conversión, estado indeterminado con texto de procesamiento en vez
   de quedarse al 100 %. No se simula progreso de parse sin métricas fiables.
8. **Imágenes sin conexión**: el worker de `ImageCache` no inicia
   una transferencia cuando `sceNetCtlInetGetState` indica desconexión;
   conserva trabajos en cola y vuelve a probar. Si NetCtl no está disponible,
   no bloquea la funcionalidad en Vita3K. Esto NO cambia las descargas del
   instalador ni sus políticas de reintento.
9. **Archive.org node trace retirado**: macro de compilación descarta
   la generación de 45+ mensajes detallados de nodos, sin ejecutar sus
   argumentos; `diagnostic_logger` no crea ni escribe
   `archive_nodes.log` y elimina el archivo antiguo una vez al iniciar.
   Se conservan los logs generales HTTP de `session.log` y todos los
   mecanismos de selección, benchmark, descarga y failover.

## Lo que no funcionó y no debemos repetir

- Revisar todos los iconos/capturas *antes* de entrar al catálogo,
  sin condicionar al modo de descarga anticipada: bloqueo medido de 7–10 s.
- Volver a establecer el catálogo luego de responder el asistente de imágenes:
  copias, ordenamientos y posibles cambios de selección repetidos.
- Interpretar un ETag cambiado como garantía de contenido cambiado.
- El indicador `100 %` de descarga no es `100 %` de inicialización.
- El archivo de log especializado por nodos ya cumplió su objetivo y
  aumentaba escrituras de almacenamiento; **no** hay evidencia de que
  fuera la causa principal de consumo de RAM (era trabajo de E/S).
- Cambiar de parser JSON sin ensayo de memoria: todavía es una
  **hipótesis de optimización**, no una corrección validada.

## Pruebas de aceptación obligatorias (antes de cualquier merge)

En la **misma consola** y con el **mismo caché** que las pruebas anteriores:

| Escenario | Comprobación |
|---|---|
| A. Caché local + internet | Se entra sin inventario masivo, catálogo visible antes del refresh |
| B. Caché local + Wi-Fi apagado | Entra sin 8× reintentos DNS de imágenes; marcadores `startup_images inventory=skipped_on_demand` |
| C. ETag distinto pero archivo idéntico | `catalog_content_compare identical=1` + **sin** segundo `parser_begin` o `ui_set_items_begin` |
| D. Contenido realmente actualizado | Toast/refresh, nueva información, búsqueda y posición conservadas |
| E. `promptImageWarmup=true` | Inventario incremental, botón/confirmación intacta, permite completar y cancelar |
| F. Cuatro pestañas (Homebrew, Vita, PSP, PS1) | Cambio de catálogo y recarga en memoria; sin perder sus filtros normales |
| G. Sin caché inicial | Descarga, barra alcanza tamaño y pasa a “Processing downloaded catalog...” |
| H. Imágenes visibles y detalle | Iconos/capturas aparecen; fallos de red no bloquean navegación |
| I. Descargas Archive.org | Selección de nodos y failover operan sin archivo `archive_nodes.log` nuevo |
| J. Rendimiento/instalación | Sin incremento preocupante de memoria o fallos de VPK/ZIP/PKG/plugins |

Cada prueba requiere revisar `ux0:data/psvitaalive/logs/session.log`
antes de la próxima apertura. El log se reinicia en cada ejecución.

**A/B mínimo:** tiempo `startup_homebrew_request` →
`startup_screen_first_set`, tiempo hasta primera pantalla navegable,
y ausencia de `image_job_scan duration_ms=7000+` cuando la precarga está
desactivada. No prometer ahorro exacto hasta recibir nuevas mediciones.

## Riesgos y trabajo deliberadamente pendiente

- **Parser JSON**: sigue construyendo DOM de ≈2,1 MB en la consola. Un
  formato cache binario/local, o parser en streaming, necesita diseño de
  versión, invalidación, prueba de RAM y rollback. No implementar a ciegas.
- **GPU**: `vita2d_load_PNG_file` puede bloquear un frame >100 ms;
  conservar la regla de una textura nueva por frame. Predecodificación
  asíncrona requeriría ensayar RAM y permisos GPU. No cambiar sin datos.
- **Concurrencia**: durante descarga y parseo otros hilos compiten por CPU.
  Verificar `ui_slow_frame` bajo diferentes conexiones/dispositivos.
- **Refresh en detalle**: hay que probar filtros, enlaces activos y elemento
  eliminado, no solo la lista de inicio.
- **Cambio de red tras comenzar un HTTP de imagen**: si Wi-Fi cae
  *durante* la transferencia, la lógica existente de libcurl aún reintenta;
  el guard de NetCtl evita empezar trabajos offline, no sustituye la
  detección dinámica de caída dentro de la transferencia.

## Reversibilidad

La rama anterior `diagnostics/catalog-startup-trace-20261012` contiene el
VPK base con logs intensivos. `main` no se ha modificado. No fusionar
sin revisar el diff y repetir los tests de regresión. En una PS Vita, el
Title ID es `PSVAS1178`; conserva un VPK estable para regresar si hace falta.

## Arquitectura preservada

`apps/` + `authors/` + `categories/` → GitHub Actions →
`catalog.json` + `authors.json` + `categories.json` → cliente/web.
Nunca editar manualmente los JSON generados. Este experimento modifica
solo el comportamiento del cliente, el workflow de CI de la rama y esta
documentación.
