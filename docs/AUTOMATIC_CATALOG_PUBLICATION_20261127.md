# PSVitaAlive — automatización de descubrimiento, publicación y preservación (2026-11-27)

**Estado:** implementado e integrado en `main`; validación de CI y primeras publicaciones reales confirmadas. No es una certificación de todos los VPK ni de todos los derechos de redistribución.
**Corte de evidencia:** 27 de noviembre de 2026, 23:17 UTC (18:17 Colombia). Los estados de los trabajos pueden cambiar después de este corte.
**Alcance:** infraestructura del catálogo, Orbit, Archive.org, Discord y GitHub Pages. No se cambió el cliente nativo en estas correcciones. El plan para automatizar PR de autores sigue aplazado y se describe por separado en [VERIFIED_PR_AUTOMATION_PROPOSAL_20261127.md](VERIFIED_PR_AUTOMATION_PROPOSAL_20261127.md).

## 1. Objetivo y arquitectura

PSVitaAlive busca descubrir, registrar, actualizar y preservar homebrew sin servidores propios. La fuente de verdad permanece en los JSON individuales:

```text
Orbit: descubrimiento -> enriquecimiento y cola persistente
                       -> señales/evidencia de publicación
PSVitaAlive: verificación independiente -> apps/ + authors/ + categories/
                       -> validate.yml
                       -> catalog.json + authors.json + categories.json
                       -> cliente PS Vita + web
                       -> notificación Discord / señal al monitor Orbit
                       -> preservación Archive.org -> verificación -> apps/
                       -> nueva generación oficial
```

Orbit ([repositorio](https://github.com/VegettoSan/PsVita-Homebrews-New-Releases)) aporta evidencia, nunca reemplaza los JSON canónicos. No editar los catálogos generados a mano. El formato compacto V2 se mantiene con los conversores `scripts/catalog_schema_v2.py` (p. ej. `tid`, `aids`, `v`, `sz`). Los registros y checkpoints internos no son nuevos catálogos públicos.

Hay tres operaciones distintas:
- **Registro inicial:** Orbit descubre candidatos y PSVitaAlive decide si registra una nueva aplicación. No asigna Verified automáticamente: Legacy para publicaciones de al menos 365 días y Archive para las recientes incorporadas para preservación. Verified sigue reservado al flujo de autor.
- **Actualización de versión existente:** `scripts/sync_release_updates.py` usa metadatos del monitor; mantiene sus reglas de identidad, versión, origen y VPK único. No descarga ni inspecciona el binario en ese flujo; consultar [AUTOMATIC_RELEASE_CATALOG_SYNC.md](AUTOMATIC_RELEASE_CATALOG_SYNC.md).
- **Preservación:** motor de Archive.org verifica el destino y los archivos antes de reemplazar enlaces canónicos. Un enlace oficial en el catálogo no equivale a un respaldo ni autoriza por sí solo la redistribución.

## 2. Implementación del registro inicial automático

| Componente | Responsabilidad |
| --- | --- |
| [scripts/import_orbit_discoveries.py](../scripts/import_orbit_discoveries.py) | Consulta release actual, descarga acotada, validación de paquete, nombre nativo, medios y staging V2 |
| [scripts/orbit_auto_selection.py](../scripts/orbit_auto_selection.py) | Elegibilidad, evidencia, identidad del autor, taxonomía y reintentos |
| [config/orbit_publication_approved.json](../config/orbit_publication_approved.json) | Política: piloto explícito MinFWVer y selección automática, 5 altas / 20 comprobaciones como máximo por ejecución |
| `data/orbit_import_cursor.json` | Cursor y feedback persistente, no cola pública |
| `data/orbit_imports/<id>.json` | Recibo de identidad del origen, tag y SHA-256; no es una concesión de licencia |
| `apps/media/<id>/<sha256>.png` | Imágenes verificadas provenientes del VPK, deduplicadas |
| [validate.yml](../.github/workflows/validate.yml) | Importación previa a generación, validación y publicación bajo bloqueo compartido |
| [import-orbit-discoveries.yml](../.github/workflows/import-orbit-discoveries.yml) | Pruebas de PR y alternativa manual; no publicar desde PR |

### 2.1 Cola y evidencia

Orbit pasó de inspección de candidatos a una cola preparada con prefiltrado barato de todo el inventario, prioridad de candidatos prometedores (80%) y exploración equitativa (20%). Guarda la cola en su checkpoint persistente y actualiza conjuntamente inventario/checkpoint. La evidencia de readiness v2 comprueba repositorio/autor, release estable actual, asset VPK único publicado, URL, tamaño y asset ID. PSVitaAlive exige `READY_TO_PUBLISH`, fingerprint y SHA-256 coincidentes, prueba de fuente de hasta 24 horas, tag/URL/asset ID/tamaño coincidentes y digest coincidente si está disponible. Un productor v1 sigue siendo compatible durante el despliegue; nunca se omiten las verificaciones independientes.

El feedback evita gastar el presupuesto continuamente en los mismos casos: upstream 404, 7 días; release modificada, 12 horas; revisión de metadatos, 3 días; otros fallos, 1 hora. Un cambio significativo de evidencia invalida la espera. Descripciones ausentes, IDs conocidos/retirados, duplicados y demás candidatos inelegibles se filtran antes de gastar recursos en descargas.

### 2.2 Comprobaciones canónicas

1. Validar identidad numérica del repositorio GitHub, linaje, cuenta individual propietaria y coincidencia del perfil. Reutilizar autor existente por identidad comprobada; evitar colisiones y autores retirados. Los forks/organizaciones ambiguos quedan para revisión.
2. Exigir la release estable actual, exactamente un VPK subido, fecha no futura, origen/URL y tamaño oficiales; verificar el digest del asset cuando exista y el SHA-256 calculado contra la evidencia.
3. Leer el SFO sin ejecutar ni extraer arbitrariamente entradas. Title ID nativo de nueve caracteres alfanuméricos para esta selección, único contra catálogo, candidatos y registro de retirados. Comprobar ZIP, CRC, eboot no vacío, límites de expansión/entradas y rechazar rutas inseguras, symlinks y entradas cifradas. El filtro de selección limita el VPK a 64 MiB.
4. Preferir nombre de presentación nativo del VPK; no usar el nombre del repositorio como nombre de la app cuando existe título verificable.
5. Extraer icono real y otros recursos nativos válidos desde rutas conocidas (icon0, pic0 y fondos LiveArea), validar PNG, dimensiones y tamaño, deduplicar por hash. Los fondos de presentación no son necesariamente capturas de gameplay.
6. Validar taxonomía oficial, requisitos, descripción y relaciones de autor. Los datos externos que el usuario aporta por separado no se confunden con material comercial incluido en el paquete. Indicaciones explícitas de contenido de terceros incluido requieren revisión.
7. Crear solamente las fuentes canónicas nuevas, medios y recibo; impedir sobrescrituras. Ejecutar el validador global y generar catálogos. Despachar procesos existentes después de la publicación.

La ausencia de metadatos de licencia ya no bloquea por sí sola el **registro de un enlace oficial** (PR #127). Esto no significa que cualquier VPK o recurso gráfico pueda redistribuirse. Mantener la política/exclusiones de preservación y revisar autorizaciones de terceros; no copiar Game Files, Data Files, DLC, plugins u otros recursos comerciales. Ni el hash ni el nombre Verified constituyen autorización legal.

## 3. Cambios y razones (historial de PR)

| PR PSVitaAlive | Resultado integrado |
| --- | --- |
| [#124](https://github.com/VegettoSan/PSVitaAlive/pull/124) | Piloto histórico MinFWVer: fuente fija, licencia original, paquete y medios nativos, flujo canónico y preservación existente |
| [#125](https://github.com/VegettoSan/PSVitaAlive/pull/125) | Nombre de presentación nativo preferido |
| [#126](https://github.com/VegettoSan/PSVitaAlive/pull/126) y [#127](https://github.com/VegettoSan/PSVitaAlive/pull/127) | Selección automática, luego separación del registro y licencia; Legacy/Archive, nunca Verified |
| [#128](https://github.com/VegettoSan/PSVitaAlive/pull/128) | Importación dentro de validate.yml bajo bloqueo; evitar trabajos perdidos al competir por la plaza pendiente |
| [#129](https://github.com/VegettoSan/PSVitaAlive/pull/129) | Incremento de 3/12 a 5 altas/20 comprobaciones, sin relajar elegibilidad |
| [#130](https://github.com/VegettoSan/PSVitaAlive/pull/130), [#131](https://github.com/VegettoSan/PSVitaAlive/pull/131) y [#136](https://github.com/VegettoSan/PSVitaAlive/pull/136) | Correcciones de taxonomía, distinción entre datos requeridos y contenido incluido; recuperación inicial de carreras de Git |
| [#132](https://github.com/VegettoSan/PSVitaAlive/pull/132) | Cola nativa de Actions `queue: max`, no cancelar trabajos de publicación |
| [#133](https://github.com/VegettoSan/PSVitaAlive/pull/133) | Evitar artefactos Pages duplicados en reintentos usando run_id y run_attempt |
| [#134](https://github.com/VegettoSan/PSVitaAlive/pull/134) | Prioridad a candidatos preparados y feedback persistente por evidencia |
| [#135](https://github.com/VegettoSan/PSVitaAlive/pull/135) | Prueba fresca de release estable para readiness v2 |
| [#137](https://github.com/VegettoSan/PSVitaAlive/pull/137) | Corrección PeelGallery, bloqueo de Discord compartido, reintento Git y diagnósticos |

Cambios coordinados en Orbit: [#12](https://github.com/VegettoSan/PsVita-Homebrews-New-Releases/pull/12) piloto de enriquecimiento, [#13](https://github.com/VegettoSan/PsVita-Homebrews-New-Releases/pull/13) nombre nativo, [#14](https://github.com/VegettoSan/PsVita-Homebrews-New-Releases/pull/14) prioridad y aviso, [#15](https://github.com/VegettoSan/PsVita-Homebrews-New-Releases/pull/15) y [#16](https://github.com/VegettoSan/PsVita-Homebrews-New-Releases/pull/16) clasificación por propósito, [#17](https://github.com/VegettoSan/PsVita-Homebrews-New-Releases/pull/17) cola, [#18](https://github.com/VegettoSan/PsVita-Homebrews-New-Releases/pull/18) publicación preparada, [#19](https://github.com/VegettoSan/PsVita-Homebrews-New-Releases/pull/19) preflight estable y [#20](https://github.com/VegettoSan/PsVita-Homebrews-New-Releases/pull/20) taxonomía.

La categoría debe describir el propósito principal, no una dependencia incidental. Ejemplos revisados: VitaIRC -> networking/messaging/client; LSD Revamped -> ports/other; The Two Towers -> ports/action/adventure; Syncthing -> networking/file-sharing/client; PS Vita Recovery -> system/recovery/diagnostics/storage; UAE4All2 HD -> emulators/commodore; ComboPeel -> plugins/input. AdvancedSlop no necesitó cambio en #136.

## 4. Preservación: checkpoints y recuperación

El motor existente procesa respaldos de VPK en ítems propios, creación inicial y medios. La creación inicial permite 8 reanudaciones por ejecución (no límite acumulado), 6 altas, 20 preflights nuevos, 400 MiB por carril, 10 minutos para iniciar reanudaciones y 24 para nuevas tareas; son máximos, no rendimiento garantizado. El límite por archivo de la fase documentada es 200 MiB. Ver [continuidad del 26/11](PSVitaAlive_Continuidad_Preservacion.md) y código para valores vigentes.

Protocolo: confirmar origen/version/Title ID y exclusiones, consultar propiedad exacta del ítem, reservar identificador y guardar checkpoint en Git antes de cada PUT. Mantener nombre versionado y archivos históricos, no sobrescribir. Separar transferencia aceptada de disponibilidad pública. Verificar hashes/tamaño y disponibilidad de todos los archivos; solo entonces actualizar el enlace VPK/icono/imágenes y registrar histórico. Un resultado incierto no habilita repetir la subida ni crear otro ítem. `VERIFYING`, `SOURCE_REVIEW`, `PENDING_MANUAL` y espera de Archive.org afectan a una aplicación; un fallo global de Git/checkpoints detiene el lote.

Rutas de mantenimiento: `data/archive_new_item_jobs/<id>.json`, `data/archive_backup_jobs.json`, `data/archive_media_backup_jobs.json`, `scripts/archive_upload_log.csv`. Ver el artefacto `archive-backup-batch-report` y logs. Tamaños de catálogo son orientativos; los bytes oficiales/reales gobiernan los límites y hashes. No corregir estimaciones de fichas rutinariamente durante el respaldo de la misma versión.

### 4.1 Incidente CATALOG_PUSH_BLOCKED

- [#184](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46076590810): PS Vita Recovery Menu Project; archivos verificados, publicación detenida por rechazo Git/cambio relevante. Recuperado en #185.
- [#187](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46079055006): Snail Mail; archivos parcialmente aceptados, publicación de checkpoint detenida. #188 retomó y #190 publicó la app.
- El mensaje anterior no distinguía todas las causas. No se pudo probar qué rechazo exacto produjo cada incidente. Discord tenía un grupo de concurrencia independiente y podía modificar main; también existían rechazos transitorios sin cambios visibles en main. #131 permitía recuperar exclusivamente cambios independientes de .github/discord/state.json.
- #137 unificó Discord con el grupo `vitahub-catalog-publisher` de catálogo, importación, sincronización y preservación. Mantiene `cancel-in-progress: false` y `queue: max`. Las notificaciones pueden esperar detrás de lotes largos. No garantiza ausencia de cambios manuales o externos. Los workflows antiguos en curso conservan su configuración original.
- `git_publish()` en [archive_backup_batch.py](../scripts/archive_backup_batch.py) reintenta de forma acotada si el push falla y main no cambió; rebase solo de la ruta Discord autorizada si árbol limpio; revalida escrituras canónicas. Registra rutas concurrentes relevantes o causa de rebase rechazado; detiene el lote ante cambios relevantes, árbol sucio o fallo persistente. No fuerza pushes ni fusiona a ciegas.
- [tests/test_archive_backup_batch.py](../tests/test_archive_backup_batch.py) prueba Git bare local: cambio Discord permitido, app/política rechazadas, push transitorio recuperado. Las 26 pruebas pasaron en #3769 y #3770 pasó la validación final. Los logs de pruebas incluyen errores simulados: no confundirlos con los fallos de transferencias reales.

### 4.2 Caso PeelGallery: dato histórico incorrecto

El estado `PENDING_MANUAL: VPK Title ID/structure does not match canonical record` no identificaba por sí solo el campo erróneo. El JSON histórico decía `tid: peelgallery` (11 caracteres). La release original [v1.0](https://github.com/AlecksPV1/PeelGallery/releases/tag/v1.0) se descargó mediante una prueba CI aislada, con SHA-256 oficial fijado y sin ejecutar el VPK.

Evidencia: [auditoría de solo lectura](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46080485449), job 140778045492: SHA-256 correcto, un eboot.bin, un sce_sys/param.sfo, Title ID `PEELGALLR`, tamaño 4.295.510 bytes, sin coincidencia canónica previa.

```text
SHA-256 d25569029531ca5cd799969ef4d5b12cd2d3550e818d63c56fa94e867ec03244
id interno: peelgallery (sin cambio)
tid: peelgallery -> PEELGALLR
sz y tamaño Download/VPK: 4404019 -> 4295510
versión: 1.0 (sin cambio)
```

La corrección editorial del tamaño se hizo junto con la identidad auditada, no es una regla para reescribir tamaños estimados en cada respaldo. Se retiró el identificador erróneo en registry/retired_ids.json para respetar el registro histórico. PR #137 se fusionó en 26e949050c303cd31f01e3182477a3a4e1a469a1. Se eliminó el workflow temporal de auditoría antes de fusionar; su ejecución sigue como evidencia. #190 aceptó la transferencia y guardó VERIFYING; #191 confirmó `PUBLISHED_VERIFIED` a las 23:12:33 UTC. Commit [9cece44](https://github.com/VegettoSan/PSVitaAlive/commit/9cece44d698c0a44c4de39ecdff6ddadc457ac8a), [ítem Archive.org](https://archive.org/details/psvitaalive-peelgallery), JSON individual con VPK, icono y captura archivados. La solución no relaja el validador ni cambia el ID interno.

## 5. Evidencia real al corte

| Ejecución | Resultado observado |
| --- | --- |
| [Import PR #21](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46076208503) | Éxito de staging y comprobaciones sin publicar desde PR |
| [Catálogo #3766](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46077936299) | 5 aplicaciones nuevas, 6 preflight elegibles de 275; comprobaciones canónicas correctas |
| [Catálogo #3770](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46080549351) | Validación final del PR #137, incluido nuevo test de concurrencia |
| [Preservación #190](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46080631724) | Éxito; Snail Mail y Skater Nation publicados; PeelGallery en VERIFYING |
| [Preservación #191](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46081015869) | Éxito; PeelGallery publicado; Raging Thunder 2 en VERIFYING |
| [Preservación #192](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46081399534) | Éxito; Raging Thunder 2 publicado; VitaIRC aún espera propiedad |
| [Catálogo #3771](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46080631728), [#3774](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46080957011), [#3775](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46081544318) | Éxito; #3771 incorporó Raging Thunder 2 y #3775 posterior a la preservación |
| [Tracker Orbit #661](https://github.com/VegettoSan/PsVita-Homebrews-New-Releases/actions/runs/46081016106) | Éxito de monitor |
| [Sync #258](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46081050567), [Discord #1347](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46081589195) | Éxito |
| [Pages #2268](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46080631830), [Build cliente #972](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46080631672) | Éxito; compilación no sustituye prueba física |

Ejemplos comprobados de altas y preservación: Snail Mail (PSVSM0001), Inotia 2 (INOTIA2PV), Skater Nation (PSVSN0001), Bike Race (BIKR00001), Riptide GP (PSVRGP001) en #3766; los cinco tienen commits de preservación verificada. Raging Thunder 2 fue incorporado en #3771 y publicado en #192 ([8cbec2f](https://github.com/VegettoSan/PSVitaAlive/commit/8cbec2f271)). También quedaron publicados LOTR The Two Towers Vita, UAE4All2 HD Vita, AdvancedSlop, PS Vita Recovery Menu Project, Syncthing Vita, LinuxOnVita y MKXP-Z Vita durante los lotes anteriores. Esto prueba los casos observados, no la cobertura de todo el inventario. No se verificó instalación física ni descarga por cliente de cada título.

## 6. Pendientes y límites conocidos

- VitaIRC: `VERIFYING`, propiedad aún no visible en Archive.org en #192; conservar identificador/checkpoint, no crear otro ítem ni volver a subir sin reconciliar.
- Snake, Halo 2D, Pekka Kana 2 Vita, PKGi: `SOURCE_REVIEW: Artwork requires a provider-specific adapter` en #192. Estudiar URLs originales y permisos; adaptar solo proveedores concretos y mantener límites/validación de red e imagen. No saltar la guardia ni sustituir medios por imágenes inventadas. No son fallos globales.
- Mantener observación de carreras Git, cola y latencia de Discord. No se observaron nuevos fallos de concurrencia en #190–192; esto no demuestra que sean imposibles.
- Distinguir validación de identidad/CRC de garantía de ejecución, seguridad de código o derechos de redistribución. Conservar revisión humana para casos ambiguos.
- PR #138 de Five Nights at Freddy's 1 sigue abierto y no autorizado para publicación automática. Ver [propuesta aplazada](VERIFIED_PR_AUTOMATION_PROPOSAL_20261127.md).
- No realizar cambios en cliente, actualización de versiones, catálogos generados o checkpoints por el mero hecho de documentar.

## 7. Cómo retomar / reproducir de forma segura

1. Leer este documento, la [continuidad](PSVitaAlive_Continuidad_Preservacion.md) y el código actual; comprobar HEAD y Actions en ambos repositorios. Inspeccionar el resultado de los trabajos, no solo su color/estado agregado.
2. Consultar recibos, checkpoints y el registro histórico antes de modificar una aplicación. Leer el JSON mediante el conversor de esquema V2. Si un lote está activo, no reiniciar transferencias ni cambiar políticas.
3. Ejecutar las pruebas offline (desde la raíz, con las dependencias fijadas por CI):

```sh
python -m unittest discover -s tests -p 'test_import_orbit_discoveries.py' -v
python -m unittest discover -s tests -p 'test_archive*.py' -v
python -m unittest discover -s tests -p 'test_sync_release_updates.py' -v
python -m unittest discover -s tests -p 'test_discord_notifications.py' -v
```

4. Para inspección de inventario sin transferencias, usar `python -m scripts.archive_backup_batch_preview` y `python -m scripts.archive_new_items_batch --automatic` **sin --execute**. Consultar ayuda/código de la versión vigente antes de ejecutar. El importador `--stage` sí escribe archivos locales: probar en una copia aislada y no hacer push; no usarlo como un dry-run puro. La sincronización por workflow_dispatch manual es preview.
5. No publicar secretos ni utilizar credenciales de Archive.org en PR externos. No hacer force push; revalidar cualquier propuesta contra el main actual. Consultar logs y artefactos antes de declarar un caso resuelto.

Referencias históricas: [plan original de preservación](AUTOMATIC_ARCHIVE_ORG_PRESERVATION_PLAN.md), [pilotos de creación inicial](ARCHIVE_NEW_ITEMS_BATCH.md), [respaldo por lotes](ARCHIVE_BACKUP_BATCH.md), [medios](ARCHIVE_MEDIA_BACKUP.md) y [sincronización](AUTOMATIC_RELEASE_CATALOG_SYNC.md). Los planes antiguos describen sus fases históricas, no deben sustituir este estado fechado ni el código vigente.
