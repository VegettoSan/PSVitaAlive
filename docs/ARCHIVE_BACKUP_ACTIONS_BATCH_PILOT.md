# GitHub Actions: prueba inicial de selección por lotes de Archive.org

**Estado: prototipo en rama `feature/archive-backup-actions-batch-preview`. NO SUBE ARCHIVOS.**

El script `scripts/archive_backup_batch_preview.py` reutiliza el `plan_changes()` del sincronizador actual para versiones futuras, y añade la identificación conservadora de casos donde la versión catalogada ya está en GitHub/GitLab pero falta cambiar la URL al respaldo. Toma solo el archivo principal `Download/VPK`, no Game Files.

El destino puede proponerse **solamente** a partir de una subida previa correcta en `scripts/archive_upload_log.csv`, sin conflictos en la URL principal del VPK. Iconos, capturas, Game Files, otros enlaces y nombres parecidos **NO acreditan que el ítem pertenezca a nuestra cuenta**. Los casos sin evidencia de ítem administrado, con URL VPK de otro ítem o con identificadores contradictorios quedan `PENDING_MANUAL`, sin subir.

**Comprobación de propiedad obligatoria antes de cada subida real:** el futuro motor leerá `https://archive.org/metadata/<identifier>` y comparará `metadata.uploader` con la cuenta esperada, guardada en un secreto privado de GitHub (por ejemplo `IA_EXPECTED_UPLOADER`). Una coincidencia exacta autoriza solo continuar el preflight; no sustituye la comprobación de permisos de escritura de IA-S3. Si falta `uploader`, no coincide, Archive.org no responde o devuelve 403, **no se intenta subir ni crear ítem**. Si es de otra cuenta se marca `PENDING_MANUAL`; si Archive.org está temporalmente caído se deja `WAITING_ARCHIVE` para reintentar, sin confundirlo con ítem ajeno. No escribir ni mostrar el correo del propietario en reportes.

Los trabajos de esta vista llevan `ownership_status: REQUIRED_BEFORE_UPLOAD` porque **el workflow de preview NO usa secretos ni hace verificaciones remotas de titularidad**; aquí `SELECTED_PREVIEW` no significa que ya sea seguro subir. Tampoco se comprueban licencias o disponibilidad del nuevo archivo: esos controles corresponden a la fase de transferencia.

La ejecución crea un informe en `archive-batch-report` con los trabajos seleccionados, diferidos, demasiado grandes y los que requieren revisión. Los límites iniciales propuestos son **12 VPK, 200 MiB por VPK, 800 MiB por lote y 45 minutos para el futuro procesador**. El workflow de esta etapa tiene un máximo de 15 minutos porque **solo calcula la cola**; todavía no necesita 45 minutos ni secretos.

Para convertir el prototipo en respaldo real, en otra fase y con pruebas independientes, se deberá agregar `ia upload` usando claves IA-S3 en Actions Secrets, **verificación de `metadata.uploader` contra el secreto privado antes de subir**, límite de duración por archivo, checkpoints, control de conexión caída, comprobación del nuevo nombre en IA antes de reintentar una subida incierta y actualización del JSON únicamente tras verificar. Los VPK grandes o con tamaño desconocido seguirán pendientes. Nunca borrar versiones previas ni crear otro ítem por release.

**Cuidado operativo:** una rama de pruebas no debe sustituir `.github/workflows/sync_homebrew_releases.yml`; el flujo actual puede seguir cambiando enlaces a GitHub para apps que no tengan `backup-first` activado. El cambio de comportamiento requiere un corte gradual aparte.

## Comprobación de los tres secretos — siguiente prueba (SIN SUBIDA)

Hay un workflow nuevo, `.github/workflows/archive-owner-preflight.yml`, que se ejecuta **solo manualmente desde `main` por la cuenta VegettoSan**, con `contents: read`. Se añade al PR de prueba para validar su código antes de pasar a `main`, por lo que el botón **Run workflow no aparecerá** hasta que el workflow esté en la rama principal (restricción de GitHub para `workflow_dispatch`).

La primera prueba propone el ítem histórico `psvitaalive-z2`, que consta con subida exitosa en `scripts/archive_upload_log.csv`. Este preflight:

1. Comprueba que existen los secretos de repositorio `IA_ACCESS_KEY`, `IA_SECRET_KEY` y `IA_EXPECTED_UPLOADER` **sin imprimir sus valores**.
2. Solo permite identificadores de ítems con subida previamente exitosa registrada; los enlaces ajenos de iconos, Game Files u otros recursos no autorizan el destino.
3. Consulta la metadata de `https://archive.org/metadata/<identifier>` en modo lectura y compara `metadata.uploader` con el valor privado de `IA_EXPECTED_UPLOADER`.
4. Expone únicamente `OWNER_MATCH_PREVIEW`, `PENDING_MANUAL`, `WAITING_ARCHIVE` o `CONFIG_REQUIRED`. Los fallos de servicio no se interpretan como propietario externo.
5. **Nunca** descarga ni sube VPK, ni modifica ningún catálogo, ni publica la cuenta o las claves en logs.

**Alcance real de la prueba:** `OWNER_MATCH_PREVIEW` significa que el campo público `metadata.uploader` coincide con el propietario esperado. No demuestra que `IA_ACCESS_KEY` o `IA_SECRET_KEY` sean válidas, ni que IA-S3 admita nuevas escrituras en ese ítem. Esa verificación requerirá la primera subida real, aislada, con un archivo nuevo y aprobación expresa.

Después de fusionar **solo código probado** en la rama principal, la persona administradora podrá abrir `Actions → Internet Archive ownership preflight (NO UPLOAD) → Run workflow`, dejar el identificador `psvitaalive-z2` y ejecutar la comprobación. No ejecutar una subida ni fusionar el motor de transferencias hasta revisar el resultado.
