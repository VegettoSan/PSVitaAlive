# PSVitaAlive — diagnóstico de arranque de catálogos (2026-10-12)

## Objetivo
Reproducir la demora entre la pantalla de carga y el primer catálogo visible.
Esta rama **solo añade instrumentación**: no cambia la estrategia de caché ni edita
`catalog.json`, `authors.json` ni `categories.json`. No fusionar en `main`
hasta que los registros permitan aislar la causa.

## Build de diagnóstico
- Rama: `diagnostics/catalog-startup-trace-20261012`
- VPK: GitHub Actions → **Build PS Vita Client** → artifact `PSVitaAlive-diagnostics-<sha>`.
- Title ID: `PSVAS1178` (el mismo que la aplicación normal).
- El VPK de prueba **reemplaza el cliente instalado**; respaldar datos importantes.
- Deshabilitar temporalmente **Startup Update Check** en Settings mientras se reproduce
  el fallo; la autocomprobación de actualizaciones del cliente puede interrumpir la prueba.
- La caché local de datos no se borra al iniciar este VPK.

## Registro
`ux0:data/psvitaalive/logs/session.log` — se reinicia al abrir la aplicación.
Enviar **el archivo completo de la sesión**, antes de volver a iniciar PSVitaAlive.

Los eventos del experimento usan `[CATTRACE]` y el log agrega timestamps en ms:
- `build_marker`: confirma que corre el VPK de diagnóstico.
- `catalog_manager_init` / `image_cache_startup_init`: inicialización.
- `startup_homebrew_request` / `worker_request`: inicio de carga.
- `disk_miss` / `disk_parse_start` / `disk_parse_ok`: caché local.
- `parser_dom_end`, `parser_conversion_end`: costes del JSON.
- `publish_ready`, `ready_consumed`: transferencia entre hilo trabajador e interfaz.
- `WARNING_unconsumed_ready_overwrite`: una publicación nueva pisa la anterior antes de consumirla.
- `validator_result`, `soft_download_start`: comprobación remota.
- `startup_buffer_copy`, `startup_screen_first_set`: preparación inicial.
- `image_job_scan`: lectura de imágenes faltantes para el aviso de precarga.
- `ui_set_items_* `, `ui_sort_items`, `ui_apply_search`: coste en UI.
- `ui_loading_toggle`, `ui_slow_frame`, `startup_poll`: bloqueo observable.

## Pruebas (en consola real, por separado)
1. **Caché existente + Internet:** abrir desde cero y esperar hasta poder navegar.
2. **Caché existente + sin Internet:** apagar Wi-Fi, cerrar y volver a abrir.
3. **Con Internet tras cambios remotos:** esperar 30 s abierto; comprobar si hubo
   refresh, si la selección cambió y si se congeló.
4. **Opcional:** repetir con la opción de descarga de imágenes desactivada.
No borrar la caché antes de la primera prueba. No adjuntar datos privados.

## Qué devolver
`session.log` completo de cada prueba, indicando prueba 1/2/3 y tiempo aproximado
de la pausa. El logger abre/cierra archivo por evento; la instrumentación introduce
cierta carga de escritura, por lo que compararemos principalmente los intervalos
de fases, no interpretaremos un único frame lento como prueba definitiva.

## Seguridad del alcance
No se registran URLs completas, contenidos del catálogo, tokens ni credenciales.
No se añade un servidor ni se altera el flujo `apps/ + authors/ + categories/`
→ GitHub Actions → JSON generados → cliente/web.
