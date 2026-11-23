# Archive.org — respaldos automáticos por lotes con GitHub Actions

**Estado:** automatización probada con FMVita, VitaDB Downloader y Arx Libertatis. Ampliación a **todas las aplicaciones elegibles con ítem propio** y ejecución coordinada cada 3 horas. No utiliza Cloudflare, R2 ni servidores propios.

## Funcionamiento

`Orbit (release snapshot)` + `apps/*.json` + `scripts/archive_upload_log.csv` → `scripts/archive_backup_batch.py` → propietario del ítem y VPK oficial → `Archive.org` → comprobación de metadatos + **descarga pública completa con SHA-256** → solo entonces `apps/<id>.json` → `validate.yml` → catálogo oficial.

El motor aprovecha el validador de nuevas versiones `plan_changes()` y soporta:
- **SAME_VERSION_BACKFILL**: la versión ya está en el JSON pero el botón VPK todavía apunta al origen.
- **NEW_RELEASE**: el monitor validó una versión superior y debe preservarse antes de publicar su nueva versión y enlace.
- Solo cambia el botón principal `Download/VPK` y, cuando corresponde, `version`, `version_date`, `size`. Los enlaces de **Game Files, iconos, mirrors, repositorios, autoría y demás datos no cambian**.

**Nunca** edita a mano los archivos `catalog.json`, `authors.json` o `categories.json`.

## Configuración y seguridad

- Credenciales en GitHub Actions Secrets: `IA_ACCESS_KEY`, `IA_SECRET_KEY`, `IA_EXPECTED_UPLOADER`; no se publican sus valores.
- Solo permite escrituras en ítems con una subida propia registrada como `ok` y con `metadata.uploader` igual al valor de la cuenta esperado. Un Archive.org de terceros en Game Files/capturas/iconos no acredita propiedad.
- Las aplicaciones elegibles se descubren automáticamente: no hace falta habilitar una por una. **La versión ni la actualización del catálogo se bloquean por el tipo de licencia o por ausencia de SPDX.** `config/archive_backup_approved_apps.json` define únicamente `default_enabled: true` y `excluded_apps` para detener una redistribución cuando corresponda.
- El sistema automático exige un VPK de release oficial identificable, versión única validada, tamaño comprobable, un ítem administrado previamente por PSVitaAlive y `metadata.uploader` de la cuenta esperada. Lo que no cumpla sigue pendiente/manual; un enlace externo de Archive.org no concede permisos sobre la cuenta ajena.
- **Preservación y redistribución no son lo mismo:** la ausencia de SPDX no debe bloquear la detección de actualizaciones, pero un binario publicado en GitHub tampoco implica autorización universal para rehosting. Ante restricciones de redistribución o solicitudes de retirada, añadir el `app_id` a `excluded_apps`, manteniendo la información y el enlace original del catálogo.
- Nombre remoto determinista: `<app-id> v<version>.vpk`. Rechaza cualquier colisión de nombre, incluso diferencias de mayúsculas; no borra ni sobrescribe nada.
- Admite solo fuentes HTTPS en GitHub Releases con redirecciones restringidas a hosts oficiales, exactamente un asset VPK validado por el monitor, tamaño conocido, cabecera ZIP/VPK y SHA-256. No intenta respaldar juegos o paquetes de datos externos.
- Se ejecuta solo en `main` del repositorio oficial; sin subidas desde PR. Usa el mismo bloqueo de concurrencia que la publicación del catálogo.

## Protección contra cortes

`data/archive_backup_jobs.json` es un **checkpoint interno**, no un catálogo de la web o Vita. Se confirma en GitHub **ANTES** de comenzar el PUT de IA-S3. Si el runner se cierra, Archive.org cae o la respuesta es incierta, una ejecución posterior **no vuelve a subir**: primero comprueba **solo** el nombre del VPK de esa versión. Si coincide tamaño y MD5 con el original y la descarga pública coincide por SHA-256, finaliza el JSON; si no está, deja `UPLOAD_RESULT_UNCERTAIN` para revisión segura.

Las consultas de metadatos no revisan todo el historial de versiones anteriores. Solo se consulta el ítem destino y el nuevo nombre.

El motor **no reintenta a ciegas** escrituras que podrían haber terminado correctamente. Ante 429/503/preflight temporal queda `WAITING_ARCHIVE` para la siguiente ejecución. Si el servicio está inestable, detiene el resto de la tanda. **Si los metadatos ya confirman el VPK pero su descarga pública aún devuelve 404, marca solo ese archivo `VERIFYING`, conserva el checkpoint y continúa con las otras aplicaciones aprobadas**; una ejecución posterior reconciliará el archivo sin repetir el PUT.

## Límites iniciales

| Variable | Por defecto | Máximo del motor |
|---|---:|---:|
| VPK por ejecución | 12 | 12 |
| Tamaño individual | 200 MiB | 200 MiB |
| Tamaño del lote | 800 MiB | 800 MiB |
| Tiempo de procesamiento | 38 minutos | 38 minutos |

Se ejecuta con GitHub Actions cada **3 horas** (minuto 47 del bloque de 3 horas) y además escucha el evento `workflow_run` al terminar correctamente la sincronización oficial `Sync Verified Homebrew Releases` iniciada por Orbit. El monitor Orbit revisa releases al minuto 17 cada 3 horas. **No se sube simultáneamente con la sincronización**: el mismo `concurrency.group` serializa las escrituras para evitar conflictos. Si una ejecución coincide con otra, los pendientes se recalculan en la siguiente tanda.

Se puede iniciar manualmente en `Actions → Archive.org owned VPK backups (BATCH) → Run workflow`. En ejecución manual, `execute=false` permite previsualizar sin subir; marcar `execute=true` para usar los Secrets y transferir.

Los VPK que exceden el límite siguen **`TOO_LARGE_PENDING`**, los identificadores que no sean de ítems propios pasan a revisión manual y los casos ambiguos nunca se suben. La disponibilidad del enlace original del catálogo se conserva durante las caídas del servicio. Para los demás casos el catálogo sigue actualizándose según su flujo habitual, aun si el respaldo sigue pendiente.

## Publicación y seguimiento

Después de confirmar que la descarga pública de Archive.org devuelve el mismo VPK, Actions valida los JSON canónicos y publica **solo los JSON individuales**. **Al completarse el lote**, el evento GitHub Actions `workflow_run` lanza el workflow oficial `validate.yml` (únicamente para la ejecución exitosa del repositorio oficial y rama `main`). Así se regenera el catálogo con **todos** los respaldos que haya publicado esa tanda y se evita saturar o cancelar validadores en la misma cola de concurrencia. Los commits hechos con `GITHUB_TOKEN` no activan por sí solos los workflows normales de tipo `push`.

La página de cada ejecución muestra una tabla de `PUBLISHED_VERIFIED`, `DEFERRED`, `WAITING_ARCHIVE`, `UPLOAD_RESULT_UNCERTAIN`, `PENDING_MANUAL`, `TOO_LARGE_PENDING`, etc. El archivo `archive-backup-batch-report` se conserva durante 14 días. No contiene secretos.

**No borrar ni editar manualmente un checkpoint `TRANSFER_PREPARED` sin comprobar primero el archivo objetivo en Archive.org.** Las claves IA-S3 se configuran directamente en GitHub, nunca se deben copiar a commits ni a este documento.

## Fases de activación

1. Revisar el PR y superar las pruebas offline, sin subir VPK.
2. Primera ejecución con **tres aplicaciones aprobadas**, máximo 3 VPK. Revisar el reporte de Actions, la descarga pública y los cambios exactos de JSON.
3. Solo tras comprobar esa ejecución, ampliar `enabled: true` a nuevos Homebrew con derechos y origen revisados.
4. Mantener el workflow cada 6 horas para las futuras versiones detectadas; incorporar más apps gradualmente, sin revisar todos los VPK históricos.

Documentos relacionados: [selector de lotes](ARCHIVE_BACKUP_ACTIONS_BATCH_PILOT.md), [primer piloto exitoso](ARCHIVE_UPLOAD_FIRST_VPK_PILOT.md) y [plan general](AUTOMATIC_ARCHIVE_ORG_PRESERVATION_PLAN.md).

## Primer lote y ajuste de publicación tardía

- Primera ejecución: [GitHub Actions #45225329131](https://github.com/VegettoSan/PSVitaAlive/actions/runs/45225329131). FMVita v0.6.2 alcanzó verificación de tamaño y MD5 en Archive.org, pero la descarga pública todavía no respondió; quedó `VERIFYING`, sin actualizar el catálogo, con checkpoint en `data/archive_backup_jobs.json`.
- Ajuste: cuando solo falte que Archive.org exponga la URL pública de un archivo ya subido, procesar el resto de las aplicaciones habilitadas. Mantener la detención ante cortes del servicio o un resultado de subida incierto. No eliminar el checkpoint ni hacer una segunda subida de FMVita por un 404 temporal.

## Resultado validado de las tres aplicaciones iniciales

- [Primera ejecución #45225329131](https://github.com/VegettoSan/PSVitaAlive/actions/runs/45225329131): FMVita v0.6.2 quedó inicialmente `VERIFYING` por disponibilidad pública tardía.
- [Segunda ejecución #45226443852](https://github.com/VegettoSan/PSVitaAlive/actions/runs/45226443852): FMVita `PUBLISHED_VERIFIED`, Arx Libertatis v2.0 `PUBLISHED_VERIFIED`, VitaDB Downloader v2.8 `VERIFYING`.
- [Reconciliación #45227721334](https://github.com/VegettoSan/PSVitaAlive/actions/runs/45227721334): VitaDB Downloader `PUBLISHED_VERIFIED` sin resubirlo. El archivo de checkpoints quedó con `inflight: {}`.
- Hallazgo operativo: una validación solicitada mediante `workflow_dispatch` durante el lote podía quedar cancelada por el bloqueo de concurrencia. Corregido usando `workflow_run` **después** de la finalización del lote, con pruebas de CI antes de promover la corrección.

## Ampliación a todos los candidatos elegibles (2026-11-23)

El motor ya no usa una lista positiva de tres proyectos ni comprueba `license.spdx_id`. La prueba de aceptación incluye un proyecto sin campo SPDX, varios proyectos elegibles no nombrados en la configuración y una exclusión explícita que sí se respeta. Se mantienen los bloqueos de seguridad y coherencia: origen verificable, propiedad en Archive.org, no sobrescribir ningún nombre, comprobación de tamaño/hash y checkpoint durable antes de subir. **Los límites son 12 VPK, 200 MiB por VPK, 800 MiB por tanda y 38 minutos de procesamiento.** El resto queda para la siguiente ejecución de tres horas. Los proyectos fuera del alcance actual de detección segura (por ejemplo enlaces de terceros sin ítem propio, releases sin asset VPK único o alojamientos que no sean GitHub Releases) seguirán pendientes para revisión, no se publicarán como respaldados.
