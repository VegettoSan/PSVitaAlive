# Archive.org — respaldos automáticos por lotes con GitHub Actions

**Estado:** implementación escalonada posterior al piloto real #100. Solo **tres aplicaciones autorizadas** en la primera fase. No utiliza Cloudflare, R2 ni servidores propios.

## Funcionamiento

`Orbit (release snapshot)` + `apps/*.json` + `scripts/archive_upload_log.csv` → `scripts/archive_backup_batch.py` → propiedad del ítem, licencia, VPK oficial → `Archive.org` → comprobación de metadatos + **descarga pública completa con SHA-256** → solo entonces `apps/<id>.json` → `validate.yml` → catálogo oficial.

El motor aprovecha el validador de nuevas versiones `plan_changes()` y soporta:
- **SAME_VERSION_BACKFILL**: la versión ya está en el JSON pero el botón VPK todavía apunta al origen.
- **NEW_RELEASE**: el monitor validó una versión superior y debe preservarse antes de publicar su nueva versión y enlace.
- Solo cambia el botón principal `Download/VPK` y, cuando corresponde, `version`, `version_date`, `size`. Los enlaces de **Game Files, iconos, mirrors, repositorios, autoría y demás datos no cambian**.

**Nunca** edita a mano los archivos `catalog.json`, `authors.json` o `categories.json`.

## Configuración y seguridad

- Credenciales en GitHub Actions Secrets: `IA_ACCESS_KEY`, `IA_SECRET_KEY`, `IA_EXPECTED_UPLOADER`; no se publican sus valores.
- Solo permite escrituras en ítems con una subida propia registrada como `ok` y con `metadata.uploader` igual al valor de la cuenta esperado. Un Archive.org de terceros en Game Files/capturas/iconos no acredita propiedad.
- Cada aplicación debe estar marcada `enabled: true` en `config/archive_backup_approved_apps.json`, con repositorio GitHub y licencia permitida. El motor comprueba **también la licencia actual del repositorio** en GitHub, no solo la anotación de la lista. Una licencia de código no autoriza necesariamente los Game Files; la lista es una decisión explícita del mantenedor para el VPK.
- Política inicial: `fmvita`, `vitadb-downloader`, `arx-libertatis` (GPL-3.0). `apollo-save-tool`, `vita-moonlight`, `retroflow-launcher` siguen **deshabilitadas** hasta revisar esta primera tanda.
- Nombre remoto determinista: `<app-id> v<version>.vpk`. Rechaza cualquier colisión de nombre, incluso diferencias de mayúsculas; no borra ni sobrescribe nada.
- Admite solo fuentes HTTPS en GitHub Releases con redirecciones restringidas a hosts oficiales, exactamente un asset VPK validado por el monitor, tamaño conocido, cabecera ZIP/VPK y SHA-256. No intenta respaldar juegos o paquetes de datos externos.
- Se ejecuta solo en `main` del repositorio oficial; sin subidas desde PR. Usa el mismo bloqueo de concurrencia que la publicación del catálogo.

## Protección contra cortes

`data/archive_backup_jobs.json` es un **checkpoint interno**, no un catálogo de la web o Vita. Se confirma en GitHub **ANTES** de comenzar el PUT de IA-S3. Si el runner se cierra, Archive.org cae o la respuesta es incierta, una ejecución posterior **no vuelve a subir**: primero comprueba **solo** el nombre del VPK de esa versión. Si coincide tamaño y MD5 con el original y la descarga pública coincide por SHA-256, finaliza el JSON; si no está, deja `UPLOAD_RESULT_UNCERTAIN` para revisión segura.

Las consultas de metadatos no revisan todo el historial de versiones anteriores. Solo se consulta el ítem destino y el nuevo nombre.

El motor **no reintenta a ciegas** escrituras que podrían haber terminado correctamente. Ante 429/503/preflight temporal queda `WAITING_ARCHIVE` para la siguiente ejecución. Si el servicio está inestable, detiene el resto de la tanda.

## Límites iniciales

| Variable | Por defecto | Máximo del motor |
|---|---:|---:|
| VPK por ejecución | 3 | 12 |
| Tamaño individual | 30 MiB | 200 MiB |
| Tamaño del lote | 60 MiB | 800 MiB |
| Tiempo de procesamiento | 38 minutos | 38 minutos |

Se ejecuta con GitHub Actions cada **6 horas** y se puede iniciar manualmente en `Actions → Archive.org owned VPK backups (BATCH) → Run workflow`. En ejecución manual, `execute=false` por defecto permite ver la selección sin subir nada; marcar `execute=true` para usar los Secrets y transferir.

Los VPK grandes siguen **`TOO_LARGE_PENDING`**, los no aprobados no se suben y los casos ambiguos siguen como revisión manual. La disponibilidad del catálogo viejo se conserva durante las caídas del servicio.

## Publicación y seguimiento

Después de confirmar que la descarga pública de Archive.org devuelve el mismo VPK, Actions valida los JSON canónicos, publica **solo los JSON individuales** y llama explícitamente a `validate.yml` con `workflow_dispatch`. Este paso es indispensable porque los commits hechos con `GITHUB_TOKEN` no activan automáticamente workflows normales de tipo `push`. El validador oficial genera el catálogo.

La página de cada ejecución muestra una tabla de `PUBLISHED_VERIFIED`, `DEFERRED`, `WAITING_ARCHIVE`, `UPLOAD_RESULT_UNCERTAIN`, `PENDING_MANUAL`, `TOO_LARGE_PENDING`, etc. El archivo `archive-backup-batch-report` se conserva durante 14 días. No contiene secretos.

**No borrar ni editar manualmente un checkpoint `TRANSFER_PREPARED` sin comprobar primero el archivo objetivo en Archive.org.** Las claves IA-S3 se configuran directamente en GitHub, nunca se deben copiar a commits ni a este documento.

## Fases de activación

1. Revisar el PR y superar las pruebas offline, sin subir VPK.
2. Primera ejecución con **tres aplicaciones aprobadas**, máximo 3 VPK. Revisar el reporte de Actions, la descarga pública y los cambios exactos de JSON.
3. Solo tras comprobar esa ejecución, ampliar `enabled: true` a nuevos Homebrew con derechos y origen revisados.
4. Mantener el workflow cada 6 horas para las futuras versiones detectadas; incorporar más apps gradualmente, sin revisar todos los VPK históricos.

Documentos relacionados: [selector de lotes](ARCHIVE_BACKUP_ACTIONS_BATCH_PILOT.md), [primer piloto exitoso](ARCHIVE_UPLOAD_FIRST_VPK_PILOT.md) y [plan general](AUTOMATIC_ARCHIVE_ORG_PRESERVATION_PLAN.md).
