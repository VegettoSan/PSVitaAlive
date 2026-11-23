# GitHub Actions: prueba inicial de selección por lotes de Archive.org

**Estado: prototipo en rama `feature/archive-backup-actions-batch-preview`. NO SUBE ARCHIVOS.**

El script `scripts/archive_backup_batch_preview.py` reutiliza el `plan_changes()` del sincronizador actual para versiones futuras, y añade la identificación conservadora de casos donde la versión catalogada ya está en GitHub/GitLab pero falta cambiar la URL al respaldo. Toma solo el archivo principal `Download/VPK`, no Game Files.

El destino debe estar documentado por una subida previa correcta en `scripts/archive_upload_log.csv` y no contradecir los enlaces actuales. Los casos ambiguos van a `REVIEW`. **No comprueba licencias ni disponibilidad real en Archive.org**: esos controles son requisitos de la fase de subida antes de habilitarla.

La ejecución crea un informe en `archive-batch-report` con los trabajos seleccionados, diferidos, demasiado grandes y los que requieren revisión. Los límites iniciales propuestos son **12 VPK, 200 MiB por VPK, 800 MiB por lote y 45 minutos para el futuro procesador**. El workflow de esta etapa tiene un máximo de 15 minutos porque **solo calcula la cola**; todavía no necesita 45 minutos ni secretos.

Para convertir el prototipo en respaldo real, en otra fase y con pruebas independientes, se deberá agregar `ia upload` usando claves IA-S3 en Actions Secrets, límite de duración por archivo, checkpoints, control de conexión caída, comprobación del nuevo nombre en IA antes de reintentar una subida incierta y actualización del JSON únicamente tras verificar. Los VPK grandes o con tamaño desconocido seguirán pendientes. Nunca borrar versiones previas ni crear otro ítem por release.

**Cuidado operativo:** una rama de pruebas no debe sustituir `.github/workflows/sync_homebrew_releases.yml`; el flujo actual puede seguir cambiando enlaces a GitHub para apps que no tengan `backup-first` activado. El cambio de comportamiento requiere un corte gradual aparte.