# Auditoría de funcionalidades implementadas — PSVitaAlive (25-10-2026)

> **Alcance:** revisión documental del estado de `main` en el commit
> [`cb43ae478a3b15151bada9529c24fe34f541af1c`](https://github.com/VegettoSan/PSVitaAlive/commit/cb43ae478a3b15151bada9529c24fe34f541af1c).
> Este inventario identifica componentes existentes, enlaza documentos y señala
> lagunas. **No** equivale a una auditoría exhaustiva línea a línea de todos
> los archivos del repositorio ni a una certificación de funcionamiento en
> todas las PS Vita, servidores o emuladores.

## 1. Arquitectura y límites

El catálogo **Homebrew** tiene fuentes editables en `apps/`, `authors/` y
`categories/`; los scripts y GitHub Actions validan/generan
`catalog.json`, `authors.json` y `categories.json`. **Nunca editar
manualmente esos tres JSON generados.** Las fuentes externas pasan por
normalización y controles de identidad; no sustituyen la fuente canónica.

El repositorio actual **también** ofrece catálogos comerciales independientes
(`catalog_psvita_games.json`, `catalog_psp_games.json`,
`catalog_ps1_games.json`) y el índice separado `.zrifidx` para licencias
PKG. No confundir estos artefactos con los tres catálogos generados de
Homebrew. Véanse [README principal](../README.md),
[generación](../scripts/README.md) y [fuentes](../sources/README.md).

Infraestructura de publicación y CI: GitHub Actions/Pages.
El cliente nativo se compila con VitaSDK; la web y los generadores son HTML/CSS/JS.
No se introdujo un backend propio para los modos de instalación descritos aquí.

## 2. Inventario: qué existe y dónde está documentado

`Implementado` significa **presente en código**; las validaciones de hardware
son independientes.

| Área | Funcionalidad constatada | Código/flujo | Documento de referencia |
| --- | --- | --- | --- |
| Cliente | Búsqueda, filtro por catálogo/categoría, detalle y selección por controles/táctil | `source/ui/full_catalog_screen.cpp` | [UI](../Client%20PSVitaAlive/source/ui/README.md), [filtros](../Client%20PSVitaAlive/docs/FILTER_PANEL_20261020.md) |
| Cliente | Cuatro pestañas: Homebrew, Vita Games, PSP, PS1; cacheo y zRIF separado | `source/catalog/`, `source/main.cpp` | [catálogo](../Client%20PSVitaAlive/source/catalog/README.md) |
| Cliente | Idiomas instalados, idioma del sistema/manual y reserva en inglés | `source/localization/`, `assets/lang/` | [idiomas](MULTILANGUAGE.md) |
| Cliente | Teclado QWERTY integrado para búsqueda y destinos ZIP | `source/ui/search_keyboard.cpp` | [decisión de IME](KEYBOARD_NATIVE_IME_REPLACEMENT_20261020.md) |
| Cliente | Temas, fuentes, pantalla OLED/batería y cache de imágenes | `source/ui/` | [caché](IMAGE_CACHE.md), [OLED](../Client%20PSVitaAlive/docs/OLED_PROTECTION_MODE.md) |
| Descarga | HTTP/resume, controles 200/206/416, reintentos, MediaFire y Archive.org | `source/network/http_client.cpp`, `download_manager.cpp` | [resiliencia](DOWNLOAD_RESILIENCE.md), [TLS](NETWORK_TLS.md) |
| Descarga | Transferencia adaptativa 2/4 rangos cuando el servidor es apto | `source/network/parallel_archive_download.cpp` | [descarga adaptativa](ADAPTIVE_DOWNLOAD_PIPELINE_20261018.md) |
| Instalación | VPK, ZIP de datos, plugins, PKG/BGDL, PSP/PS1 mediante Adrenaline | `source/installer/` | [instalador](../Client%20PSVitaAlive/source/installer/README.md) |
| Instalación | **Install All:** selección automática o manual, cola secuencial y resumen | `FullCatalogScreen::installAllAdvancePick`, `installAllStartQueue` | [espacio y cola](INSTALL_ALL_SPACE_PREFLIGHT_20261025.md), [cierre worker](INSTALL_ALL_WORKER_CRASH_FIX_20261018.md) |
| Espacio | **Modo conjunto** basado solo en enlaces *realmente encolados* cuando todos tienen tamaño | `installAllQueueRequiredSpaceBytes`, `StorageManager::queryUx0Space` | [flujo detallado](INSTALL_ALL_SPACE_PREFLIGHT_20261025.md) |
| Espacio | **Modo individual** cuando falta algún `size`, con presupuesto por descarga | `installAllStartQueue`, `InstallController::requestInstall` | [flujo detallado](INSTALL_ALL_SPACE_PREFLIGHT_20261025.md) |
| Espacio | Comprobación HTTP 200/206 **antes del cuerpo**, y antes de rangos paralelos | `HttpClient::headerCallback`, `DownloadManager::runJob` | [flujo detallado](INSTALL_ALL_SPACE_PREFLIGHT_20261025.md), [red](../Client%20PSVitaAlive/source/network/README.md) |
| Espacio | Vigilancia durante descarga y preflight de tamaño **descomprimido** ZIP | `download_manager.cpp`, `zip_extractor.cpp` | [resiliencia](DOWNLOAD_RESILIENCE.md), [ZIP/espacio](ZIP_SOBREESCRITURA_SEGURA_20261025.md) |
| ZIP | Archivo completo o rangos crudos `.zip.001`...`.004` conservados tras fallo de extracción | `InstallController::persistPendingZipRecovery` | [ZIP guardados](SAVED_ZIP_SEGMENTS_20261025.md) |
| ZIP | Seleccionar ZIP guardado: reextraer sin red / eliminar tras doble confirmación / cancelar | `FullCatalogScreen::drawSavedZipActionOverlay`, `workerSavedZipMain` | [ZIP guardados](SAVED_ZIP_SEGMENTS_20261025.md) |
| ZIP | **Install All** intenta conservar automáticamente un ZIP recuperable una sola vez tras 10 s; si falla, deja la decisión manual | `FullCatalogScreen::updateAndDraw`, `installAllResolveRecovery` | [ZIP guardados](SAVED_ZIP_SEGMENTS_20261025.md) |
| ZIP | Reutilización de espacio al sobreescribir archivos; precaución con archivos parciales y rutas profundas | `source/archive/zip_extractor.cpp` | [ZIP/espacio](ZIP_SOBREESCRITURA_SEGURA_20261025.md) |
| ZIP | Reubicación de ramas profundas a staging en el mismo volumen, sin fusionar ramas existentes no vacías | `source/archive/zip_extractor.cpp` | [renombrado profundo](ZIP_RECOVERY_AND_RANGE_ASSEMBLY_20261018.md) |
| Plugins | Detección archivo **y** línea activa de taiHEN, o solo archivo con `section=none` | `plugin_detector.cpp`, `tai_config_editor.cpp`, `full_catalog_screen.cpp` | [detección](PLUGIN_DETECTION_CONFIG_20261023.md) |
| Plugins | **Install All pregunta por cada Plugin**, incluso si ya está instalado: instalar/reinstalar u omitir | `installAllAdvancePick`, `installAllAdvancePluginPick` | [guía de plugins](PLUGIN_UPDATES.md), [UI](../Client%20PSVitaAlive/source/ui/README.md) |
| Plugins | Detección particular del kubridge antiguo mediante tamaño y omisión de fd_fix si RePatch está activo | `full_catalog_screen.cpp` | [plan de versiones y estado real](PLUGIN_UPDATES.md) |
| Actualizaciones | Autoupdate del cliente con `PSVAUPDT1` y detección de Homebrew instalado | `source/update/` | [cliente](../Client%20PSVitaAlive/source/update/README.md), [detección](UPDATE_DETECTION.md) |
| UI/diagnóstico | Teclado; QR/enlace a la página web; news; registro/reportes | `source/ui/web_qr_guide.cpp`, `source/network/error_reporter.cpp` | [QR](WEB_QR_HANDOFF_PLAN.md), [logger](ASYNC_REPORT_LOGGER_20261012.md) |
| Web | Listados/búsqueda, fichas de app/juego, autores, categorías y enlaces agrupados | `web/js/`, `web/*.html` | [web](../web/README.md) |
| Web | Generador JSON en navegador: crear/editar, tamaños por enlace, autores importados sin duplicarlos | `web/tools/app-generator/` | [generador](../web/tools/app-generator/README.md) |
| Web | Generador visual de mascotas y exportación ZIP | `web/tools/mascot-generator/` | [mascotas](../web/tools/mascot-generator/README.md) |
| CI | Validación/generación de catálogo, despliegue Pages, build Vita y tests de políticas | `.github/workflows/` | [workflows](../.github/workflows/README.md) |

La referencia a funciones de código sirve para ubicar la implementación, no para
crear nuevas APIs públicas. No se ha cambiado ningún esquema JSON en esta revisión.

## 3. Documentación desfasada que esta auditoría corrige

1. **Install All no omite automáticamente todos los plugins instalados.** El
   diálogo permite explícitamente reinstalarlos, porque la mera presencia no
   prueba la versión. El texto anterior de varias guías era obsoleto.
2. **Espacio no es siempre `2.1 × tamaño`.** El cálculo depende de formato,
   cola y modo conjunto/individual; el servidor puede aportar tamaño real en
   cabeceras, y el extractor ZIP hace otro cálculo sobre bytes descomprimidos.
3. **Los ZIP recuperados no son siempre un `.zip` único.** El flujo actual
   conserva en disco también 2/4 rangos crudos `.001`...`.004`.
4. **Los ZIP guardados no se ejecutan al pulsar directamente la fila.** Antes
   aparece el diálogo de reintentar/eliminar/cancelar con confirmación separada
   de borrado.
5. **Hay reutilización de espacio al sobrescribir ZIPs** que no estaba
   reflejada en la guía general; sus límites y riesgos se documentan aparte.
6. **Licencias del catálogo:** el anterior resumen global «todo CC0» dejó de
   ser una descripción válida después del cambio publicado el 18-10-2026.
   El histórico CC0 sigue vigente; las contribuciones originales nuevas pueden
   estar cubiertas por CC BY 4.0. Véase
   [`CATALOG_LICENSE.md`](../CATALOG_LICENSE.md). No asumir que derechos
   sobre trabajos de terceros están licenciados por PSVitaAlive.
7. **No confundir funciones terminadas con planes:** actualización remota de
   plugins mediante manifiesto y reconocimiento de versiones de instalaciones
   externas siguen siendo propuestas, no funcionalidades terminadas.

## 4. Límites y comprobaciones que siguen pendientes

- **Hardware:** el build VitaSDK y las pruebas unitarias no sustituyen pruebas
  con PS Vita real. Verificar descargas en serie, Archive.org, MediaFire,
  segmentos 2/4, tamaños HTTP ausentes, cancelación y espacio insuficiente.
- **Cabeceras HTTP:** `Content-Length`/`Content-Range` solo son fiables
  cuando describen la respuesta del archivo real. Algunos CDNs no exponen
  tamaño: no es posible prometer un rechazo anticipado en esos casos. Se
  mantiene el guardia en curso (cada ~32 MiB / reserva de 16 MiB).
- **Estimaciones ZIP:** tamaño comprimido ≠ tamaño instalado. La validación de
  crecimiento descomprimido se hace después de recibir el ZIP.
- **Reinicio durante persistencia ZIP:** existe un marcador
  `.psva_keep_zip` para proteger trabajos pendientes de la limpieza, pero
  no se ha verificado que *todos* los rangos dejados en un trabajo transitorio
  reaparezcan automáticamente en la lista de ZIP guardados tras un corte.
  Probar inyección de fallos de `rename` y reinicio antes de afirmarlo.
- **Sobrescritura de archivos antiguos:** si un fichero de destino existente
  falla después de truncarse, no se elimina automáticamente; **puede quedar
  incompleto**. No prometer rollback transaccional.
- **Rutas profundas:** una rama previamente existente y no vacía no se
  fusiona; puede requerir intervención manual. La prueba con Simpsons
  documenta un caso, no todos.
- **Versiones de plugins:** un tamaño concreto de kubridge se reconoce, pero
  comprobar bytes por tamaño no demuestra criptográficamente su versión.
  Actualización global por manifiesto/hash aún no está integrada.
- **Validación web:** el generador prepara JSON descargables, no aplica PRs ni
  garantiza que éstos superen el CI.

## 5. Procedimiento de mantenimiento documental

1. Identificar el archivo/función que **realmente** implementa la función.
2. Cambiar primero el documento técnico especializado del subsistema.
3. Actualizar los README de la capa y los índices
   (`docs/README.md`, `README.md` y, cuando corresponda, web/workflows).
4. Señalar si la evidencia procede de fuente de código, test de host, build
   VitaSDK, prueba de Vita real o simple diseño. **No confundir estados.**
5. Ejecutar tests/CI adecuados cuando cambie el código. Para cambios solo en
   `.md`, revisar enlaces y asegurar que la publicación no exige editar los
   catálogos JSON generados.
6. Mantener histórico de planes y experimentos, indicando cuál es la conducta
   vigente para evitar que documentos antiguos se lean como instrucciones.

### Estado de validación conocido del código analizado

El commit funcional `cb43ae4` completó
[Build PS Vita Client](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40250988724)
(incluido VPK de prueba), validación de catálogo y despliegue web en GitHub
Actions. Ese éxito valida compilación y pruebas automáticas, **no** pruebas
manuales en hardware real de los cambios de espacio del 25-10-2026.

**Alcance de este commit documental:** solo Markdown. No modificar el cliente,
la web operativa, los JSON canónicos ni los catálogos generados.
