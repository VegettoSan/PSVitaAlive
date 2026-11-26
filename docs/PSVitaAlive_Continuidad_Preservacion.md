# PSVitaAlive — continuidad de preservación (26/11/2026)

## Reglas permanentes

Trabajar directamente en main, sin ramas nuevas; comprobar el HEAD antes de publicar y no interferir con checkpoints activos. Los tamaños del catálogo (sz/size, tamaños de enlaces, Game Files, Data Files y cualquier otro archivo) son **estimaciones informativas**, con posibles diferencias de bytes o MB. Nunca exigir igualdad con los bytes reales, ni cambiar la estimación por esa sola razón. Usar metadatos de la publicación oficial y bytes descargados para límites reales, MD5/SHA-256 y verificación pública. No modificar versiones, fechas, tamaños ni otros datos durante el respaldo de una misma versión. Los bytes exactos viven en checkpoints. El actualizador de versiones es independiente: no cambiar sus reglas de selección, repositorio, versión o publicación.

Fuentes canónicas apps/, authors/, categories/ → validate.yml → catalog.json, authors.json, categories.json → cliente y web. Nunca editar catálogos generados manualmente. No servidores propios. Solo respaldar VPK, icono, capturas, SOURCES.txt; no Game Files, Data Files, DLC ni plugins. No eliminar históricos ni sobrescribir; respetar exclusiones, fuentes y derechos de distribución.

## Módulos

- scripts/sync_release_updates.py y workflow Sync Verified Homebrew Releases: monitor Orbit y actualizador de versiones; mantener intactos.
- scripts/archive_backup_batch_preview.py: inventario y localización por histórico/referencias; estimaciones solo para prioridad y preview.
- scripts/archive_backup_batch.py: VPK de ítems existentes, metadatos GitHub exactos para ejecución, límite real y hashes; data/archive_backup_jobs.json.
- scripts/archive_item_creation.py: piloto VitaForge, primitivas reutilizables de descubrimiento/PUT/verificación conjunta; data/archive_item_creation_jobs.json.
- scripts/archive_new_items_batch.py: pilotos por fases con SHA-256 y tamaño real fijados desde GitHub; data/archive_new_item_jobs/<id>.json.
- scripts/archive_media_backup.py: imágenes nuevas de ítems existentes, 3 por ejecución, 8 MiB, PNG/JPEG/WebP estático, máximo 4096x4096. data/archive_media_backup_jobs.json.
- scripts/archive_upload_log.csv: mapeo histórico app_id/identifier/status/files/error/url.
- config/archive_backup_approved_apps.json: exclusiones.
- scripts/normalize_vpk_labels.py: Download/VPK único, no renombrar variantes o auxiliares.
- .github/workflows/archive-backup-batch.yml: escaneo cada tres horas, sync y push de módulos, lock compartido vitahub-catalog-publisher. Secrets IA_ACCESS_KEY, IA_SECRET_KEY, IA_EXPECTED_UPLOADER; jamás revelar valores.

## Protocolo de escritura

Verificar Title ID único, repo, fuente/version aprobada, exclusión, historial y propiedad exacta por metadata.uploader. Buscar ítems propios también por cuenta/título/identificador. Si hay candidato propio ambiguo no crear otro. Cuatro opciones psvitaalive-<id>, -psvita, -homebrew, -archive; nunca cambiar después de checkpoint. Preparar VPK e imágenes originales, deduplicar por hash; checkpoint persistido antes de cada PUT. Enviar toda la tanda sin esperar disponibilidad pública individual (esperar propietario confirmado del ítem). Verificar todos los archivos visibles contra MD5, tamaño real, SHA-256 público; reanudar pendiente sin repetir PUT ni redescargar origen si todos intentados. Actualizar JSON individual + CSV + checkpoint solo con todo verificado. El workflow de validación regenera los catálogos. No confundir logs de tests con transferencias reales.

## Estado confirmado antes de esta revisión

VitaForge 0.1.15: PUBLISHED_VERIFIED 22:58:41 UTC, psvitaalive-vitaforge. TNTV 1.05 y VitaTwitch 1.32: PUBLISHED_VERIFIED 23:20:09 y 23:20:12 UTC. Run 45909766047 envió todas las 6 transferencias; run 45910260225 verificó y publicó los seis archivos sin reenvío. Los tres JSON apuntan a sus ítems. 71 pruebas previas pasadas. Actualizador run 45909867730 correcto (0 nuevas versiones elegibles, 44 revisiones). Validación 45910236653 correcta; verificar siempre catálogos actuales. Pages 45909766011 correcto tras fallo anterior.

Commits: 269958d subida por tandas; 5254d0c normalización (1254 fichas, 1228 estándar, 17 especiales, 9 sin VPK directo); 03d6c04 pilotos TNTV y VitaTwitch y reparación de fuentes. Correcciones de tamaños anteriores fueron innecesarias: se restauran las estimaciones originales de Insaniquarium 7444890, PeelGallery 4404019 y VagaChatVITA 2537554, conservando repositorios. Halo 2D, Pekka Kana 2 Vita, PKGi y Snake tienen VPK directo añadido.

## Nueva fase

mofa-vita 1.20 (KNIF00540), 4938904 bytes oficiales, 4939696 aproximados en catálogo; SHA256 06a0701c21f5ddccc3570359f10c4a77f86278789fed8c545e441dc9fc43d402; 4 capturas. SuperTuxKart Vita 1.02 (STKV00001), 12251364 bytes reales, SHA256 d2359fd4bf98d4dca75d31014f5f2a76bfb7d07f782023453d9b4a85e790a74d; 5 capturas desde releases oficiales, icono raw. Game Files intactos. Habilitados solo después de confirmar TNTV y VitaTwitch. Validar nueva ejecución, no afirmar resultados por commit ni repetir transferencias.

Pendientes: ampliar por fases a Silent Hill 3, Burnout y otras candidatas; reconciliar ítems propios descubiertos sin registro histórico (no duplicar); 73 referencias multimedia ITEM_REVIEW en último escaneo; adaptar ZIP (CrossCraft Classic/SurvivalTest, fheroes2, TheXTech), itch.io DS8, servidor externo Vita Steam Link y nombres de versión textual como Alpha 0.1.0, sin inventar ni cambiar versiones. No habilitar todos sin validar. Verificar SHA, logs, JSON, CSV y catálogo tras cada piloto. El actualizador no debe perder funcionalidad.

## Retomar en otro chat

Leer este archivo, consultar HEAD de main y runs recientes de GitHub Actions, revisar checkpoints antes de escribir. No reiniciar transferencias. Artefacto archive-backup-batch-report (run.json, new-items.json, new-items-batch.json, media.json). vpk-label-audit contiene auditoría de etiquetas. Comandos de solo lectura: python -m scripts.archive_backup_batch_preview; python -m scripts.archive_item_creation; python -m scripts.archive_new_items_batch; python -m scripts.archive_media_backup. Pruebas: python -m unittest discover -s tests -p 'test_archive*.py' -v.

Enlaces: https://github.com/VegettoSan/PSVitaAlive/actions/runs/45909766047 ; https://github.com/VegettoSan/PSVitaAlive/actions/runs/45910260225 ; https://github.com/VegettoSan/PSVitaAlive/actions/runs/45909867730.

## Actualización: selección automática de ítems

Fase 3: run 45910502148 y 45911131086 aceptaron todos los 15 archivos; al momento de activar esta mejora los dos checkpoints aún están CREATING, VPK de mofa-vita verificado. El nuevo workflow primero concilia ambos sin repetir PUT. La dependencia histórica entre pilotos quedó eliminada. Cola automática de GitHub Releases con carriles independientes: hasta 8 reanudaciones por ejecución (no máximo de checkpoints acumulados), 6 nuevas creaciones, 400 MiB reales por carril (hasta 800 MiB en total), hasta 20 preflights nuevos, 10 minutos de presupuesto para iniciar reanudaciones y 24 para nuevos ítems. Rotación horaria en ambos carriles. VERIFYING, WAITING_ARCHIVE, UPLOAD_RESULT_UNCERTAIN, SOURCE_REVIEW y PENDING_MANUAL no bloquean otras aplicaciones; checkpoint e identificador reservados incluso si IA aún no muestra el ítem. Solo fallos globales de Git/checkpoint o estado local inseguro detienen la cola. Se conservan exclusiones, referencias históricas, propiedad, Title ID único y comprobación del SFO interno para nuevas selecciones. Metadatos oficiales de GitHub, hash oficial si existe, de lo contrario SHA-256 calculado y fijado antes de PUT; el tamaño aproximado no es autoridad. Checkpoint dinámico conserva source_spec. Imágenes del repo original o raw del propio PSVitaAlive, incluyendo blob raw=true; otros proveedores quedan en revisión. No modificar actualizador ni Game Files. Vita Steam Link, MediaFire, ZIP, otros hosts y versiones no compatibles permanecen fuera de creación automática; no se descarga contenido no verificado. Dry run: python -m scripts.archive_new_items_batch --automatic. Workflow push dispara conciliación y cola; no crear otro workflow concurrente. Revisar informes y logs, no interpretar commit como respaldo completado.

## Corrección de independencia de cola

Los cinco pilotos quedaron PUBLISHED_VERIFIED en run 45912025837 (mofa-vita 23:38:17 UTC y SuperTuxKart 23:38:28 UTC). Eliminar PRIOR_PHASE, gate de VitaForge, gate de pilotos y MAX_PENDING acumulado. La incertidumbre de un PUT afecta solo a esa aplicación; no repetir el archivo. No modificar el actualizador ni el respaldo de VPK de ítems existentes. Añadir pruebas de más de cuatro pendientes, pilotos incompletos, errores individuales y parada por fallo global Git. Mantener lock compartido de publicación.

## Capacidad ampliada (26/11/2026)

Runs 45912025837 y 45912566275: cinco pilotos publicados; Burnout Legends e Insaniquarium seleccionados automáticamente y PUBLISHED_VERIFIED; Silent Hill 3 tiene 7/7 transferencias aceptadas, pendiente de verificación. La cola anterior permitía 2 nuevos ítems y 8 preflights compartidos. Se integra la corrección de independencia y se amplía a 6 nuevos, 8 reanudaciones separadas, 20 preflights de nuevas candidatas y 400 MiB de VPK reales por carril, manteniendo 200 MiB por archivo. Son máximos, no garantías de número; se envían secuencialmente sin esperar verificación pública de otras aplicaciones. Sin cambios del actualizador ni del motor anterior. Prueba de regresión exige 6 nuevas creaciones VERIFYING en una sola ejecución. El último informe deja Snake, Halo 2D, Pekka Kana 2 Vita y PKGi en revisión de imágenes; PeelGallery en revisión de Title ID/estructura VPK. No omitir estas protecciones ni cambiar IDs sin evidencia. Los proveedores externos siguen pendientes. Verificar CI antes de afirmar que esta ampliación funciona en producción.
