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
