# PSVitaAlive 02.17 — estado técnico y validación (2026-10-28)

**Release:** [PsVita Alive Store 02.17](https://github.com/VegettoSan/PSVitaAlive/releases/tag/02.17) · **tag:** `02.17` · **commit de publicación:** `40e03c66d08aa09ef15bf4d4ac05df2167a9ae70` · [body](releases/02.17.md).

Este documento refleja la implementación de 02.17, conservando como historial los documentos anteriores. La arquitectura Homebrew permanece `apps/ + authors/ + categories/ → GitHub Actions → catalog.json + authors.json + categories.json → web/cliente`. No modificar manualmente los catálogos generados.

## Cambios incorporados

| Área | Comportamiento | Archivos principales |
|------|----------------|----------------------|
| Tarjetas | Ocultan `versionDate` del pie; muestran versión, autor, estado, tamaño y chips de Game/Data Files. El detalle conserva la fecha y el orden cronológico sigue disponible. | `source/ui/full_catalog_screen.cpp` |
| Filtros | Sin filtro activo se muestra la etiqueta localizada «Filtros», no «Mostrar todo». Con selección se muestra su nombre localizado con marquee, sin cambiar ancho/hitbox. | `source/ui/full_catalog_screen.cpp` |
| PSP/PS1 instalado | Índice de PBP e ISO/CSO con la unidad correspondiente; badges como «Instalado ux0»/«Instalado uma0» con marquee. Se exploran las particiones accesibles `ux0`, `uma0`, `imc0`, `xmc0`. | `include/ui/installed_game_index.hpp`, `source/ui/full_catalog_screen.cpp` |
| PSP/PS1 instalación | Ruta `pspemu` visible durante el trabajo y al completar; PKG/ISO/PBP tienen mensajes adecuados. Se evita copiar la ruta cada 64 KiB durante el progreso. | `source/installer/psp_installer.cpp`, `install_dispatcher.cpp`, `install_controller.cpp` |
| Estabilidad | Eliminada una doble llamada a `updateAndDraw()` y la invalidación de estado por fotograma; el índice PSP/PS1 se refresca después de cerrar el resultado exitoso. | `source/main.cpp`, `source/ui/full_catalog_screen.cpp` |
| Discord | QR y web con `https://discord.gg/byn8FDuDrw`; Problema/Sugerencia manual crean hilos en sus foros; diagnósticos automáticos conservan su destino separado. | `source/network/error_reporter.cpp`, `include/network/feedback_forum.hpp` |

**Nota sobre almacenamiento:** `psp_storage` elige `ux0:` o `uma0:` para instalar con Adrenaline; Folder utiliza `pspemu/PSP/GAME` y el ISO de PSP `pspemu/ISO`. La detección de `imc0`/`xmc0` es de archivos existentes, no una tercera opción de destino configurada. Los PSP/PS1 enviados a LiveArea conservan BGDL. Un archivo sin ID compatible no prueba instalación ni versión comercial.

## Publicación de 02.17

1. `Client PSVitaAlive/CMakeLists.txt` contiene `VITA_VERSION "02.17"` y Title ID `PSVAS1178`.
2. [Build PS Vita Client](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40782997499) completó pruebas C++/VitaSDK y publicó el VPK como artefacto CI.
3. [Publish PSVitaAlive Release](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40783438367) obtuvo ese mismo artefacto, comprobó CRC del VPK y `sce_sys/param.sfo` (`APP_VER=02.17` y `TITLE_ID=PSVAS1178`), creó SHA-256 y publicó `PSVitaAlive.vpk` más `PSVitaAlive.vpk.sha256`.
4. GitHub marcó la 02.17 como **Latest**. El autoactualizador utiliza `tag_name` y el asset `PSVitaAlive.vpk` mediante el helper existente `PSVAUPDT1`.
5. El workflow exige notas en `docs/releases/<version>.md` y no sustituye releases existentes. Solo los builds exitosos por push a `main` pueden publicarse.

**Alcance de pruebas:** el responsable del proyecto probó en **PS Vita real** las correcciones funcionales mediante el VPK de ensayo anterior, todavía marcado internamente 02.16. La compilación final 02.17 se volvió a generar y se verificó por **GitHub Actions**. No atribuir prueba en consola del VPK final sin una confirmación nueva; CI tampoco comprueba permisos reales de Discord ni todas las combinaciones de unidades.

## Regresiones recomendadas

- Comprobar cuadrícula/lista, idiomas, filtros «Filtros» → categoría → «Mostrar todo», efecto marquee, tarjeta sin fecha y fecha en detalle.
- Instalar PSP/PS1 en `ux0` y `uma0`; comparar ruta de progreso y final, indexación tras cierre, badges y casos de `imc0`/`xmc0` disponibles o desmontados. Repetir PS1 PBP, PSP ISO/CSO y LiveArea.
- Terminar una actualización iniciada con **Update** sin parpadeo; comprobar vista detalle y navegación.
- Verificar QR del Discord oficial, envío manual Problema/Sugerencia a cada foro y ausencia de cambio en los reportes automáticos.
- Descargar el VPK 02.17, verificar SHA-256, instalar en consola y ejercitar el autoactualizador cuando corresponda.

## Documentos relacionados

- [README del cliente](../Client%20PSVitaAlive/README.md), [interfaz](../Client%20PSVitaAlive/source/ui/README.md) e [instalador](../Client%20PSVitaAlive/source/installer/README.md)
- [Panel de filtros](../Client%20PSVitaAlive/docs/FILTER_PANEL_20261020.md) e [instalados](../Client%20PSVitaAlive/docs/INSTALLED_CATALOG_FILTERS_20261020.md)
- [Detección de actualizaciones](UPDATE_DETECTION.md) y [política de releases](self-update-versioning.md)
- [QR Discord](DISCORD_COMMUNITY_QR_20261027.md) y [foros Discord](DISCORD_FEEDBACK_FORUMS_20261028.md)
- [Body oficial 02.17](releases/02.17.md)
