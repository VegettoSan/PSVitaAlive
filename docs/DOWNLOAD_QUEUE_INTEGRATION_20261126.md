# PSVitaAlive — Gestor de descargas: integración y mantenimiento (2026-11-26)

## Estado y alcance

Candidato de código validado por el mantenedor en una PS Vita física: `37a0fdbccd3dfdaef1d238f162d33109193cf38f`, [build VitaSDK #845](https://github.com/VegettoSan/PSVitaAlive/actions/runs/45766980368). APP_VER 02.19, Title ID PSVAS1178. El mantenedor aprobó integrar esta versión en main el 26 de noviembre de 2026. La integración es de código fuente, no una nueva publicación en Releases ni un cambio de versión. Los archivos de catálogo, autores y categorías, generados o canónicos, no se modifican por esta integración.

Este documento consolida el estado vigente; las notas de fases anteriores son registros históricos. La matriz de pruebas de [hardware](DOWNLOAD_QUEUE_DEVICE_TEST_20261125.md) sigue siendo útil: la aprobación del mantenedor no implica que cada combinación de rutas, recuperación, plugins, cancelación o dispositivo se haya probado.

## Uso

- Agregar un enlace de descarga o un grupo Install All desde la ficha. La aceptación mantiene la navegación y muestra una notificación; el icono residente puede volar hacia el botón del gestor.
- El botón Descargas del pie abre trabajos activos/pendientes o, sin sesión, historial/recuperación. El detalle muestra componentes, etapa, bytes, porcentaje y ETA cuando son conocidos; no se inventan totales. Controles de pausa/reanudación/cancelación dependen de la operación y su token.
- La ficha activa aparece primero; pendientes conservan FIFO; estados terminales se desplazan al final. Transición de 320 ms en el listado visible, con protección de selección y toques. No se modifica el orden interno.
- El protector OLED tras 60 s de inactividad presenta aplicación, fase/componente, progreso disponible, pendientes/completados, batería y mascota; se puede salir del protector sin cancelar la instalación.
- Un bloqueo por capacidad permite abrir mantenimiento restringido y reintentar explícitamente. No equivale a permiso de borrar datos instalados.

## Navegación de controles (ajuste posterior a la integración)

START recorre tarjetas → footer → header → tarjetas según la zona real de foco; las pestañas cuentan como header. La selección de la aplicación se conserva. El gestor de descargas, historial, recuperación y detalle de pasos aceptan D-pad y joystick izquierdo vertical, con repetición de 330/130 ms. Se combinan las direcciones para no duplicar pasos, se neutralizan direcciones opuestas y se mantienen las barreras de confirmación y animación. Las acciones X/◯/△/□ y L/R no se repiten automáticamente.

## Arquitectura e invariantes

```text
FullCatalogScreen (vista, controles, solicitudes)
     -> main.cpp -> Coordinator -> Session (FIFO, admisión, tokens)
                            -> InstallController -> DownloadManager/HTTP
                                                 -> dispatcher / ZIP / VPK / BGDL
                            -> CatalogManager (gate de exclusión)
                            -> HistoryStore / RecoveryStore (identidades)
ImageCache worker -> RGBA en RAM -> UI/vita2d (textura)
```

| Archivo (relativo a Client PSVitaAlive/) | Responsabilidad |
|---|---|
| include/installer/download_queue.hpp | Estado de sesión, máximo 100 grupos y 64 componentes por grupo, planificación y contabilidad por destino |
| include/installer/queue_coordinator.hpp | Coordinación en hilo principal, exclusión de catálogo, barreras, propiedad de token y eventos terminales |
| source/installer/install_controller.cpp | Adaptador nativo, snapshots de espacio, ejecución y liquidación de un componente |
| source/main.cpp | Orquestación, persistencia, recibos, eventos de UI y telemetría |
| include/installer/queue_history.hpp | Historial visual acotado (40 entradas, máximo 32 KiB) |
| include/installer/queue_recovery.hpp | Identidad de trabajos pendientes/interrumpidos y selección explícita para recuperar |
| source/ui/full_catalog_screen.cpp | Gestor, orden visual, protector OLED y controles |
| include/ui/queue_progress_policy.hpp | Presentación de orden y progreso, independiente de la ejecución |
| source/ui/image_cache.cpp | Caché v3, validación y decodificación de iconos en trabajador existente |
| source/archive/zip_extractor.cpp | Validación, extracción y cesión cooperativa de CPU |

- Un solo componente nativo activo a la vez. El token identifica grupo y paso; ningún movimiento visual, toque o resultado obsoleto puede cambiar el dueño del paso.
- Mantener protección de shell/energía y exclusión de catálogo durante la sesión; el coordinador libera ambas solo cuando no hay trabajo nativo pendiente. Las operaciones incompatibles (cambios de catálogo, actualización, mantenimiento destructivo) conservan sus barreras.
- Reservar por unidad y considerar todos los componentes pendientes. Crédito únicamente por staging físico estable perteneciente al paso, sin contar bytes recibidos pero no persistidos. Replanificar si el tamaño HTTP real supera el estimado; no usar espacio de otra unidad. Tamaños desconocidos, rutas opacas y handoff de sistema mantienen barreras conservadoras.
- No confundir BGDL entregado al sistema con instalación verificada. Los pasos dependientes no se ejecutan automáticamente después de un handoff no verificado. Recuperación explícita con identidad actual del catálogo, marcador local y confirmación cuando corresponde.
- No saltar comprobaciones de rutas, ZIP, capacidad, plugin o recuperación para ganar FPS. Las pausas cooperativas del extractor se aplican también a preparación/directorios/archivos pequeños, sin eliminar validaciones.

## Persistencia y compatibilidad

`ux0:data/psvitaalive/queue_history.a/.b` (PSQH2, lee PSQH1) conserva resultados terminales, identidad, versión, fecha, pasos y clave opaca validada de icono. `queue_checkpoint.a/.b` (PSQR2, lee PSQR1) conserva identidades de recuperación. Ambos usan snapshots alternados con checksum y verificación de escritura; el anterior válido permanece si falla una escritura. El historial es informativo y nunca ejecuta trabajos. No serializar enlaces de descarga, zRIF, directivas de plugins, tokens ejecutables ni rutas arbitrarias. El checkpoint se guarda antes de aceptar/arrancar trabajo; ante fallo de la barrera de prearranque no se lanza el worker.

Los iconos de historial usan la caché existente v3, sin duplicar archivos; registros PSQH1 intentan resolver Title ID único en el catálogo cargado; si no hay imagen válida se usa placeholder. La clave de caché no autoriza una petición HTTP. El borrado/rotación de caché puede dejar entradas sin imagen. No existe reanudación automática byte a byte tras muerte del proceso; entradas ambiguas/interrumpidas requieren revisión.

## Rendimiento: decisiones y evidencia

- La UI conserva un LRU limitado (en sesión, hasta 9 iconos y 2 capturas); durante instalación se aplazan capturas nuevas. El worker de imágenes reutilizado lee/decodifica PNG de iconos (máximo 128×128) y publica un resultado RGBA; la UI crea/sube la textura y conserva la propiedad GPU. Dos buffers de píxeles acotados a 64 KiB cada uno, sin contar memoria interna libpng/GPU. No cargar PNG de iconos en el hilo de render durante sesión protegida.
- Los probes pasivos de instalación usan cache durante la sesión; estados desconocidos no equivalen a no instalado. Se aplazan scans progresivos y consultas visuales de ZIP/Game Files. Validaciones explícitas de acciones conservan su autoridad.
- `[QueuePerf]`: resúmenes por fase de frames >=50/100 ms, máximo total/UI/resto del bucle. Solo ventanas con frames lentos se imprimen. `[QueueUiPerf]`: operaciones >=100 ms, limitadas por sitio cada 5 s (icon_decode, icon_upload, install_probe, mascot_start, gpu_free, gpu_present). Ausencia de registros no significa ausencia absoluta de trabajo o latencia.

Prueba anterior #843: SuperTuxKart mostró una carga de icono de 2797 ms frente a un frame de 2807 ms. El siguiente candidato #845 eliminó los registros lentos de icon_decode/icon_upload en el log entregado y registró 4 frames >=100 ms en Extracting; no es un benchmark controlado (paquetes, tiempos y navegación diferentes).

| Prueba física #845, 26/11 | Bytes escritos | Apertura ZIP hasta fin | Resultado |
|---|---:|---:|---|
| Kingdom Hearts 358/2 Days Game Files | 268435456 | 68.428 s | Ok |
| Nitronic Rush Game Files | 407813671 | 161.718 s | Ok |
| Crash Nitro Kart 2 Game Files | 77130001 | 35.159 s | Ok |

En los tres casos se verificaron longitudes de rangos antes de extracción y hubo limpieza de ZIP exitosa. Se aceptaron trabajos adicionales; el tamaño real mayor que el estimado se replanificó con remainingKnown=1 y remainingValid=1. No se adjunta el log de usuario (contiene rutas/URLs temporales y datos de entorno).

## Deuda técnica priorizada (no implementada en esta integración)

1. Mascota: carga síncrona de Crash Bandicoot durante Nitronic Rush: 13882 ms; frame 13885 ms. Separar scan/lectura/decodificación del hilo UI, publicar pixels por presupuesto y subir texturas en pasos pequeños, sin modificar protección OLED; medir memoria y cancelación al salir.
2. Admisión: frame de 3517 ms coincide con agregar Nitronic Rush; separar captura de espacio, planificación y persistencia por spans; no relajar el checkpoint ni la coherencia de snapshots. No atribuir la totalidad de la pausa sin medición.
3. Transición: 2572 ms fuera de UI al iniciar siguiente trabajo después de Nitronic Rush. Medir snapshot, reap del worker, guardado y arranque; evitar despachar sin propiedad válida.
4. Consultas explícitas restantes: install_probe de 957 ms en Crash Nitro Kart 2. Diseñar resultado asíncrono con generación/identidad y revalidación; no reutilizar estados obsoletos para autorizar acciones.
5. Revisar discrepancia de diagnóstico archive/storage settle (primer segmento vs disk_size total) sin modificar reserva conservadora hasta medir su efecto.
6. Repetir la matriz de hardware, incluyendo muchos archivos pequeños (SuperTuxKart), descargas largas, Wi-Fi, pausas, cancelaciones, historial entre reinicios, recovery, multiunidad, BGDL y todos los idiomas. Medir los mismos paquetes y condiciones para comparar tasas de frames lentos por minuto.

## CI y mantenimiento

`download-queue-host.yml` ejecuta pruebas C++17 del core, coordinador, historial y recuperación con warnings como errores y sanitizers ASan/UBSan. A partir de la integración se habilita en main y PRs a main para los paths correspondientes. `client-build.yml` ejecuta pruebas de regresión de UI/catálogo y compilación VitaSDK; el último candidato aprobado es #845. Una compilación exitosa no sustituye pruebas de hardware. No publicar una nueva Release ni cambiar APP_VER sin una decisión aparte.

Referencias: [fases 1](DOWNLOAD_QUEUE_PHASE1_20261125.md), [coordinador y rutas](DOWNLOAD_QUEUE_PHASE5_NATIVE_20261125.md), [capacidad](DOWNLOAD_QUEUE_PHASE6_STORAGE_20261125.md), [historial](DOWNLOAD_QUEUE_PHASE7_HISTORY_20261125.md), [recuperación](DOWNLOAD_QUEUE_PHASE8_RECOVERY_20261125.md), [staging](DOWNLOAD_QUEUE_PHASE9_LIVE_SPACE_20261125.md), [rutas mixtas](DOWNLOAD_QUEUE_PHASE10_MIXED_ROUTES_20261125.md), [progreso](DOWNLOAD_QUEUE_PHASE11_PROGRESS_UI_20261125.md), [prioridad catálogo](DOWNLOAD_QUEUE_CATALOG_PRIORITY_20261125.md), [matriz hardware](DOWNLOAD_QUEUE_DEVICE_TEST_20261125.md).
