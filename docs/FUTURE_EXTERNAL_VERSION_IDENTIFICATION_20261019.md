# Investigación futura: versiones de Homebrew instalados externamente

> **ESTADO: SOLO DOCUMENTADO — NO IMPLEMENTAR TODAVÍA.** Investigación y propuesta del 19 de octubre de 2026. No cambia la lógica actual de versiones ni la semántica de `UpdateAvailable`.

## Problema real

Un VPK contiene `sce_sys/param.sfo`. Distintos proyectos y forks pueden actualizar `APP_VER`, `VERSION`, ambos o ninguno. Ejemplos orientativos:

| APP_VER | VERSION | Versión conocida en catálogo | Problema |
| --- | --- | --- | --- |
| 00.00 | 00.03 | 0.3 | APP_VER es un valor fijo, VERSION podría reflejar el release |
| 01.05 | 01.00 | 1.5 | VERSION podría estar fijo |
| 01.00 | 02.03 | 2.3 | No es seguro elegir por valor máximo o cercanía |
| 00.00 | 00.00 | 1.4 | Ninguno permite identificar el release |

No asumir que dos apps del mismo autor siguen la misma convención, especialmente en forks/compilaciones automatizadas. `VERSION` no siempre representa la misma noción de release que `APP_VER`. No inferir que `01.71` significa `1.71`: algunos proyectos empaquetan `1.7.1` en el formato Vita `01.71`.

### Código actual en PSVitaAlive (19/10/2026)

- `Client PSVitaAlive/source/ui/full_catalog_screen.cpp`: `queryLocalInstall()` detecta presencia por `ux0:app/<TITLE_ID>`; lee primero `APP_VER`; solo si no puede leerlo consulta `VERSION`. Un `APP_VER=00.00` válido impide leer `VERSION` aunque esta fuera útil.
- `Client PSVitaAlive/source/update/update_decision.cpp`: el `sfo_policy=fallback` por defecto **no anuncia una actualización solo por diferencia del SFO**. También contempla la codificación `XX.YZ` para versiones semánticas `X.Y.Z`.
- `Client PSVitaAlive/source/update/installed_release_tracker.cpp`: PSVitaAlive conserva un recibo de versión del catálogo al instalar un VPK. Puede quedarse obsoleto si se reemplaza la app desde VitaShell/u otra tienda. **No constituye prueba criptográfica**.
- `queryLocalInstall()` aún pasa `FingerprintEvidence{}` sin calcular huellas del instalado y `UpdateDetectionMeta{}` sin firmas conocidas. `update_decision.hpp` ya tiene estructuras para signatures, historial de releases y política SFO. No afirmar que los hashes están operativos.

Documentación preexistente: `docs/UPDATE_DETECTION.md` y `tests/test_update_decision.cpp`.

## Investigación de VitaDB Downloader

Referencias primarias (revisadas 19/10/2026):
- https://github.com/Rinnegatamante/VitaDB-Downloader/blob/main/source/database.cpp
- https://github.com/Rinnegatamante/VitaDB-Downloader/blob/main/source/utils.cpp
- https://github.com/Rinnegatamante/VitaDB-Downloader/blob/main/source/network.h
- https://github.com/Rinnegatamante/VitaDB-Downloader/blob/main/daemon/main.c

Su detector enumera directorios instalados por Title ID. En ausencia de `hash.vdb` calcula **MD5** del `eboot.bin`, o de `EBOOT.PBP` en PSP, y almacena una caché `hash.vdb`. Opcionalmente usa `aux_hash.vdb` y archivos de motor (Unity `Media/sharedassets0.assets.resS`, GameMaker `games/game.win`, Godot `game_data/game.pck`, Lua y YoYo Loader). Compara la firma con `hash` / `hash2` publicados por VitaDB. La implementación lee el archivo completo a través de un búfer compartido; el proyecto reserva un búfer de 32 MiB para varios usos, **no implica que ese tamaño sea necesario para calcular MD5/SHA-256**. El daemon lee las firmas en caché.

Advertencia: el flujo de coincidencia de VitaDB puede clasificar un hash distinto del actual como `APP_OUTDATED`. Un hash desconocido puede representar un fork, nightly, modificación o una versión más nueva. **Nunca traducir “no coincide” directamente a “actualización disponible” en PSVitaAlive.** Tampoco reutilizar código GPL sin revisar obligaciones de licencia; estudiar ideas y escribir implementación original.

Como opción de estudio adicional, una herramienta futura de catálogo podría usar solicitudes HTTP Range para descargar solo las partes de un ZIP necesarias para obtener un archivo interno, si el servidor soporta Range y el ZIP permite el acceso. No es viable universalmente; no evita leer los bytes del archivo que se quiere hashear y requiere validación de ZIP64, offsets, redirecciones y proveedores.

## Diseño tentativo para evaluación futura

1. Detectar aplicación por Title ID y, sin bloquear la UI, buscar recibo local **válido**. El recibo sin digest no debe considerarse prueba de una instalación que pudo cambiar externamente.
2. Si existe firma de release publicada y verificada, calcular hash **del archivo ya instalado** con lectura incremental y búfer pequeño (p. ej. 64 KiB), en un worker. No descargar un VPK completo a la consola para detectar su versión.
3. Mantener un historial opcional de firmas por release; comparar contra **versiones conocidas**, no solo contra el hash último. Priorizar `SHA-256`, admitir hashes MD5 externos únicamente cuando el algoritmo esté identificado y con reglas de confianza y migración claras.
4. Elegir archivos representativos por app (`eboot.bin` y, cuando proceda, recurso del motor). Algunas releases modifican solo datos; una firma de `eboot.bin` igual en dos releases no identifica de forma única su versión. Evitar hashing masivo de contenido pesado en arranque.
5. Incorporar caché con invalidación real (tamaño/mtime + revalidación según confianza) en `ux0:data/psvitaalive/`; tamaño/mtime **no son prueba criptográfica** y pueden repetirse. Límites de CPU, I/O y RAM medidos en PS Vita real.
6. Leer `APP_VER` y `VERSION` **independientemente** como candidatos. Descartar placeholders como `00.00` para inferencias, no escoger automáticamente el mayor ni el más cercano al catálogo como prueba de versión. Una política explícita por aplicación podría seleccionar una clave SFO confiable, pero jamás extrapolar por autor.
7. Resultado conservador: firma de release actual -> `Installed`; firma de release anterior conocida -> `UpdateAvailable`; hash desconocido, SFO ambiguo o conflicto con recibo -> `InstalledUnknown`. Nunca ofrecer downgrade en base a evidencia ambigua.
8. Conservar la arquitectura oficial: JSON individuales `apps/`, `authors/`, `categories/` -> validación y generación GitHub Actions -> `catalog.json`, `authors.json`, `categories.json` -> PS Vita/web. Metadatos nuevos **opcionales** y compatibles con proyectos Legacy/Archive. **Nunca editar JSON generados manualmente.**
9. Probar primero con 5–10 aplicaciones reales de distintos motores, forks y releases. Medir coste de hashing en PS Vita y la precisión; no avanzar sin validar el experimento.

## Decisión actual

**Aplazado por decisión del responsable del proyecto.** No agregar cálculo SHA-256, escaneo de instalaciones externas, descarga Range, políticas SFO nuevas ni esquemas JSON nuevos en esta fase. La siguiente fase de trabajo se refiere a **estabilidad del instalador**, no a detección de versiones.
