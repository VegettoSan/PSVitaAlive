# PSVitaAlive — Plan de respaldo automático y preservación en Archive.org

> **Estado: propuesta de arquitectura / NO IMPLEMENTADA.**
> Fecha de documentación: 2026-11-23.
> Alcance de este cambio: **solo documentación**. No se habilitan subidas, servicios Cloudflare, credenciales, modificaciones del sincronizador ni escrituras nuevas sobre el catálogo.

## 1. Objetivo aprobado para estudiar

Automatizar **dos casos con el mismo motor de preservación**:

1. **Pendientes actuales (backfill):** Homebrew cuya versión catalogada o última release ya detectada por Orbit todavía no está verificada en Archive.org. No esperar a que aparezca otra release para procesarlos.
2. **Actualizaciones futuras:** cuando Orbit descubre una nueva versión válida, respaldar el VPK en Archive.org y, una vez comprobado, poner la URL de ese VPK en la ficha canónica.

**Política de preservación:** mantener un **ítem de Archive.org por Homebrew**, incorporar cada versión como **un archivo nuevo con nombre versionado** (estilo `Nombre v1.2.3.vpk`) y **no borrar, reemplazar ni ocultar** las versiones previas. No crear un ítem nuevo para cada actualización. Se permite una política de nombre exacto que respete el estilo ya usado por el mantenedor; normalizar sin destruir el nombre original y no generar colisiones.

Ejemplo ilustrativo, no inventario verificado:

```text
archive.org/details/psvitaalive-ejemplo
  Ejemplo v1.0.vpk
  Ejemplo v1.1.vpk
  Ejemplo v1.2.vpk  <- nueva versión
  icon0.png
  shot_01.png
  ...
```

No automatizar la publicación de contenido comercial, juegos oficiales, PKG, DLC, Game Files ni otros binarios ajenos al VPK principal. La preservación de otros tipos se estudia por separado, si hay derechos y autorización.

## 2. Situación actual, confirmada en el repositorio

**PSVitaAlive**:
- `.github/workflows/sync_homebrew_releases.yml` recibe `release_monitor_synced` y ejecuta `scripts/sync_release_updates.py`; en modo automático ya puede escribir el JSON individual.
- `scripts/sync_release_updates.py` exige reglas conservadoras (origen fiable GitHub/GitLab, versión superior, fecha válida, VPK único y botón `Download` denominado `VPK`). **Actualmente solo lee metadatos y cambia versión/URL; no descarga, verifica ni sube VPK.**
- `docs/AUTOMATIC_RELEASE_CATALOG_SYNC.md` describe el protocolo entre el monitor y el catálogo. La modificación actual de `apps/*.json` dispara `.github/workflows/validate.yml`, que genera los catálogos.
- `scripts/archive_upload_log.csv` conserva correspondencias históricas `app_id -> identifier` y resultados de subidas. Es **evidencia inicial**, no autorización perpetua ni prueba de que el VPK de una versión esté disponible.
- El catálogo usa actualmente esquema compacto V2 en los archivos publicados; usar los conversores/validadores ya existentes, no asumir claves largas en disco.

**Orbit / monitor** (repositorio `VegettoSan/PsVita-Homebrews-New-Releases`):
- `data/upstream_latest.json` es el snapshot de releases que consume el sincronizador, no la fuente de verdad del catálogo.
- `preservation_status.py` y el dashboard ya distinguen `matched`, `missing`, `outdated` y `review`; sus indicadores se basan en URL, versión y nombres. **No prueban integridad, disponibilidad ni titularidad del ítem.**
- El dashboard identifica dos situaciones relevantes: «Falta respaldo en Archive.org» (catálogo ya actualizado con URL GitHub/GitLab) y «Versión nueva sin respaldo» (upstream más nuevo que la versión enlazada de Archive.org).

**No duplicar sistemas existentes:** reutilizar el descubrimiento, normalización de versiones, reglas de prereleases y validación actuales. El nuevo componente agrega orquestación, transferencia, verificación y reconciliación.

## 3. Arquitectura propuesta (no implementada)

```text
Orbit (detección existente + snapshot)
      |
      +---- auditoría de pendientes actuales (reconciliación programada)
      |
      +---- nuevas releases elegibles
                     |
                     v
         validador / generador de propuesta (PSVitaAlive)
                     |
          solicitud autenticada + idempotency_key
                     |
                     v
       Cloudflare Worker (API de control, sin VPK en memoria)
                     |
                     v
       Cloudflare Workflow (estados, espera, reintentos, límites)
                     |
       [R2 TEMPORAL, solamente si hace falta]
                     |
                     v
        Archive.org: MISMO item, archivo VPK NUEVO
                     |
           metadata + tamaño + hash + acceso
                     |
                     v
       resultado autenticado al repositorio PSVitaAlive
                     |
           revalidación del JSON canónico actual
                     |
                     v
   apps/<id>.json (solo campos VPK/versión autorizados)
                     |
                     v
          validate.yml -> catálogos generados
                     |
           web + cliente PS Vita
```

**Arquitectura oficial que se debe mantener**:

```text
apps/ + authors/ + categories/
               -> GitHub Actions
               -> catalog.json + authors.json + categories.json
               -> web + cliente PS Vita
```

Ni Cloudflare ni Orbit reemplazan `apps/` como fuente canónica. **Jamás editar manualmente** los JSON generados ni crear un segundo catálogo público paralelo. El estado interno de tareas de preservación no debe introducir campos incompatibles en el esquema público V2.

**Propuesta de responsabilidades**:
- **Orbit**: descubre y clasifica releases; continúa funcionando si el sistema de respaldo está apagado.
- **GitHub / PSVitaAlive**: determina elegibilidad, mapea el destino, recibe confirmaciones, revisa la versión actual y hace la actualización canónica y la validación.
- **Worker**: puerta de entrada autenticada; solo acepta trabajos creados por el backend autorizado. Nunca es un proxy público de subida de binarios.
- **Workflow**: pasos duraderos, control de reintentos y verificación asincrónica.
- **R2 (opcional)**: staging de archivos, si una subida directa por streaming no es viable o no ofrece reanudación/medición suficiente. No se asume gratis ni ilimitado.
- **Archive.org**: almacenamiento histórico final de VPK. La cuenta debe tener permiso de escritura en el ítem.
- **Repositorio / registro de tareas**: conserva auditoría idempotente más allá de la retención limitada de ejecuciones Workflows; su ubicación definitiva queda pendiente de diseño.

## 4. Resolver el identificador del ítem existente

No inferir permisos ni destino solamente por el nombre de la aplicación.

**Prioridad de identificación propuesta**:
1. Un **mapa explícito, revisado y único** `app_id / title_id -> archive_identifier`, generado inicialmente a partir de `scripts/archive_upload_log.csv` y verificado con el ítem real. Futuro registro versionado independiente, sugerido `registry/archive_items.json`, sujeto a validación; **no existe todavía**.
2. Verificación cruzada con el identificador en el enlace `Download/VPK` y en enlaces de `Mirror`.
3. Icono, capturas y URL de Game Files/Data Files de Archive.org como **indicios auxiliares**. Estos recursos pueden estar en ítems diferentes: no seleccionarlos como única prueba.
4. Consultar los metadatos del ítem y verificar su existencia, que pertenezca al conjunto autorizado y que las credenciales de subida tengan acceso antes de escribir.

Si hay varios ítems candidatos, el registro indica otro ítem, no puede confirmarse propiedad/permiso, faltan datos o el destino tiene una identidad ambigua: `REVIEW`, **sin subida**. No crear automáticamente un ítem inexistente. La creación inicial requiere una decisión/flujo aparte, incluidos metadatos correctos y licencia/redistribución autorizada.

## 5. Dos vías que deben funcionar

### A. Cola de respaldos pendientes (backfill)

**Inventario ligero, sin reauditar versiones antiguas:** tomar los pendientes que Orbit ya clasifica y cruzarlos con el JSON canónico y el mapeo autorizado de ítems. **Las versiones que el mantenedor ya respaldó manualmente se consideran respaldadas**; no descargar, comparar hashes ni listar todo su historial. La metadata de Archive.org se consulta para validar un destino aún no confirmado, confirmar una **nueva subida** o resolver un resultado incierto (p. ej., reintento tras desconexión). El estado visual del monitor es una señal para crear trabajos, no un comprobante técnico de la nueva subida.

Posibles casos:

| Caso | Comportamiento propuesto |
| --- | --- |
| Catálogo versión 1.2, enlace en GitHub, Archive.org solo 1.1 | Preparar respaldo de 1.2, verificar y reemplazar **solo** el URL principal por el de Archive.org, aunque la versión del catálogo ya sea 1.2 |
| Catálogo versión 1.1 respaldada, release upstream 1.2 válida | Respaldar 1.2, luego actualizar versión/fecha/tamaño/URL canónicos |
| Ya existe 1.2 verificada en el ítem pero JSON apunta a GitHub | No duplicar archivo; validar y reconciliar únicamente la URL canónica |
| Archivo existe con nombre igual y hash/tamaño diferente | `REVIEW`: no sobrescribir, renombrar improvisadamente ni publicar |
| Versión ambigua, VPK múltiple, item ajeno o no autorizado | `REVIEW`, sin publicación |
| Sin ítem o sin permiso demostrado | Pendiente de alta/revisión manual, sin creación automática |

**Caso crítico:** `sync_release_updates.py` hoy exige una versión **estrictamente posterior** y evita editar enlaces cuando la versión es igual. Se necesita una **rama nueva y cuidadosamente validada de reconciliación «misma versión, solo URL»** tras confirmar el respaldo, sin alterar reglas del sincronizador actual hasta el corte. No hackear `newer_version()` para permitir cambios generales.

No prometer un respaldo de todas las versiones históricas que no estén en las fuentes consultadas: la primera meta es la versión pendiente/elegible conocida; backfill histórico adicional requeriría localizar fuentes y permisos separados.

### B. Actualizaciones futuras («backup-first»)

Con el respaldo automático **habilitado y validado para una app**, la release elegible crea un trabajo de preservación **antes** de actualizar el botón VPK del catálogo. Hasta confirmar, el catálogo mantiene su último enlace y versión válidos; la tarea pendiente aparece en el estado del monitor. Solo al confirmar, se publica la nueva versión con el URL Archive.org.

**Compatibilidad:** el modo actual (actualización directa GitHub/GitLab -> JSON) debe permanecer funcionando para apps **no inscritas**, ante fallo de despliegue de Cloudflare o durante la migración. No cambiar el orden global hasta haber validado las pruebas y definido la política de fallback. Una app con backup-first activo que falle **no** debe publicar automáticamente un enlace Archive.org inexistente.

**Concurrencia/race condition:** si llega otra release, alguien modifica el JSON manualmente o la versión actual se adelanta durante la subida, releer `apps/<id>.json` y el último estado antes de publicar. No retroceder versiones, no revertir cambios manuales de botones/mirrors/autoría, no modificar otras apps y no publicar sobre un VPK alternativo seleccionado por el mantenedor.

## 6. Selección de VPK, nombres y prevención de duplicados

Elegibilidad inicial: proyectos `Verified` y `Legacy` cuando la fuente/redistribución estén autorizadas y se cumplan **todas las reglas actuales** de fuente original, versión, fecha, estado y **único asset .vpk**. `Archive`, builds etiquetados como borrador/no probados, conflictos de Title ID y VPK múltiples permanecen en revisión; la designación `Verified`/**Legacy** por sí sola no equivale a licencia de redistribución.

Propuesta de nombre remoto: `<nombre seguro> v<version-normalizada>.vpk` (ejemplo `Ejemplo v1.2.3.vpk`), siguiendo el convenio del mantenedor «espacio + v + versión». La política concreta de mayúsculas, sufijos (beta/rc/hotfix), normalización y caracteres permitidos debe probarse. El origen original, nombre upstream, release/tag y checksum se registran para trazabilidad, aunque el nombre remoto sea distinto.

Identificador idempotente recomendado: `app_id + title_id + upstream_repo_id/canonical_source + release_id/tag + normalized_version + source_asset_id/URL`, procesado de forma determinista. No bastarse únicamente con `app_id + version` cuando el upstream reutiliza una etiqueta móvil; si su contenido cambia, revisión explícita y sin sobreescribir un nombre existente.

Antes de transferir: comprobar la identidad del ítem y reservar el nombre final en la bitácora idempotente del trabajo. **No hacer un escaneo general de versiones existentes** en cada actualización nueva. Si hay indicios de duplicado o un resultado anterior incierto, consultar **solo el archivo de la versión objetivo**: si existe con tamaño/hash coincidente y verificable, marcar respaldado sin resubir; si tiene contenido diferente o no se puede resolver el conflicto, detener y escalar a revisión. Evitar sobrescribir versiones antiguas incluso cuando Archive.org permita sobrescribir nombres.

## 7. Transferencia y verificación — experimento obligatorio

**Hipótesis por validar, no función ya probada:** usar Cloudflare Workflows + Worker para descargar por streaming el VPK de GitHub/GitLab y subirlo por IA-S3 a Archive.org (mismo item, nuevo nombre); R2 solo de ser necesario para staging/reanudación.

Antes del desarrollo final comprobar con un VPK pequeño y luego con uno grande:
- Acceso a la fuente oficial; redirecciones y dominios permitidos; fuente firmemente vinculada con el repositorio y release; tamaño conocido, no HTML ni página de login.
- Límite de RAM/CPU y comportamiento real del streaming; no `arrayBuffer()`, `text()` ni buffering completo de VPK.
- Compatibilidad IA-S3 y método de envío; cabecera `Content-Length`/`FixedLengthStream`, timeouts de escritura, reintentos y tratamiento de respuestas IA, transferencia de >1 GB, posibilidad real de reanudación.
- Hash local/de origen cuando sea fiable; metadata de Archive.org (`files`, `size`, `md5/sha1` si existen) y URL GET/Range o descarga de prueba. La presencia de una ficha no demuestra que el archivo pueda descargarse.
- Espera por procesamiento de Archive.org y posibles respuestas `503 SlowDown`, `429`, controles antispam y bloqueos de cuenta. Límites de concurrencia y backoff **sin reintentos agresivos**. Algunos fallos de permiso/antispam deben detenerse para revisión.
- Restricciones, cuotas y coste de Workers, Workflows y R2 **según el plan vigente antes de desplegar**. No asumir que la gratuidad cubre todos los VPK grandes.
- Borrado solo de objetos **temporales de R2** tras confirmación y ventana de recuperación; **nunca** borrar versiones anteriores de Archive.org.

**Comprobación final sugerida**: metadatos de IA confirman el nombre exacto, tamaño positivo y checksum compatible cuando existe; lectura real de la URL estable; registro `verified_at`. Si el checksum no está disponible, establecer política conservadora explícita antes de promover a `BACKED_UP`. No suponer que una respuesta HTTP 200 de subida basta.

Fuentes técnicas (revalidar en la implementación):
- https://internetarchive.readthedocs.io/en/stable/api.html
- https://internetarchive.readthedocs.io/en/stable/cli.html
- https://developers.cloudflare.com/workflows/reference/limits/
- https://developers.cloudflare.com/workers/platform/limits/
- https://developers.cloudflare.com/workers/runtime-apis/request/
- https://developers.cloudflare.com/r2/objects/upload-objects/

### Archive.org fuera de servicio, lento o intermitente

**Principio operativo:** una caída de Archive.org no debe perder actualizaciones ni bloquear Orbit/PSVitaAlive. Los trabajos se guardan en una cola **durable**; el monitor sigue detectando releases y el cliente sigue utilizando el enlace/version canónicos que ya tenía (o la versión ya publicada por el flujo antiguo mientras se migra).

- **Antes de subir:** si Archive.org no responde o devuelve 502/503/504, dejar el trabajo en `RETRYABLE_ERROR` o `WAITING_ARCHIVE`, guardando el `app_id`, release, URL fuente, `archive_identifier` y nombre esperado. No modificar el JSON canónico.
- **Durante la subida:** si ocurre timeout, caída de conexión o HTTP incierto, **no asumir fallo ni repetir ciegamente**; cuando el servicio vuelva, consultar únicamente el nombre del VPK que se estaba subiendo. Si el archivo está completo y coincide con los metadatos disponibles, continuar con verificación; si está ausente, reintentar; si está incompleto o en conflicto, `REVIEW`.
- **Subida aceptada pero procesamiento tardío:** pasar a `VERIFYING` y sondear solo el nuevo archivo con intervalos moderados. No subir una segunda copia solo porque el enlace `/download/` todavía no está listo.
- **Reintentos:** backoff exponencial con jitter, límite de concurrencia y respeto de `Retry-After` para 429/503 cuando se proporcione; introducir un **circuit breaker** que pause nuevas transferencias ante fallo generalizado y retome con una prueba pequeña cuando Archive.org se recupere. No saturar Archive.org con reintentos.
- **Errores no transitorios:** credenciales inválidas (401/403), bloqueo antispam, permisos sobre el ítem, rechazo por políticas o nombre en colisión no se reintentan en bucle: pasan a `BLOCKED`/`REVIEW` para intervención.
- **Caídas prolongadas:** permitir trabajos pendientes durante días sin caducar ni perder su identidad; persistir los datos fuera de la memoria/ejecución efímera de Workers/Workflows, con reconciliación periódica desde la cola durable. Al volver Archive.org, procesar por lotes con cuota y límites.
- **R2 opcional:** si ya hay un VPK temporal en R2, conservarlo dentro de una política de retención y presupuesto claramente definida para no repetir la descarga del origen; nunca dejar staging sin límite de tiempo/coste. Si no hay R2, guardar la referencia upstream y reintentar después.
- **Visibilidad:** exponer estado `Esperando Archive.org`, último error no sensible, siguiente intento y número de reintentos; poder pausar/reanudar manualmente. Evitar avisar a Discord de una nueva versión como «respaldada» hasta que el catálogo oficial realmente la publique.

El sistema **nunca elimina ni sustituye archivos históricos** a causa de una caída. La única verificación obligatoria en el camino normal es la **subida nueva**, su disponibilidad y su enlace final.

## 8. Estado y API interna de trabajos (propuesto)

Estados sugeridos:
- `DISCOVERED`: candidato de Orbit/auditoría.
- `REVIEW`: fuente o destino ambiguos, derechos faltantes, colisión o VPK no seguro.
- `QUEUED`: destino y release validados; job idempotente creado.
- `TRANSFERRING`: descargando/subiendo.
- `VERIFYING`: publicado en IA pero aún sin comprobación suficiente.
- `BACKED_UP`: VPK corroborado en ítem correcto.
- `PUBLISHING`: relectura del catálogo y escritura canónica controlada.
- `DONE`: JSON y catálogos validados y enlace confirmado.
- `WAITING_ARCHIVE` / `RETRYABLE_ERROR`: servicio externo caído, procesamiento lento o fallo temporal; mantener trabajo y reintentar luego.
- `BLOCKED`: permisos inválidos, rechazo permanente, conflicto o revisión humana obligatoria.

Registrar como mínimo: `job_id`, `app_id`, `title_id`, versión/tag, ID de release/asset, URL upstream, `archive_identifier`, nombre remoto, tamaño y hashes conocidos, timestamp, estado, intentos, causa del fallo y SHA de catálogo leído/publicado. Los registros de progreso no deben incluir tokens ni descargas privadas.

**Contrato conceptual de evento** (NO endpoint operativo):
```json
{
  "event": "psva_archive_backup_requested",
  "job_id": "<deterministic-id>",
  "app_id": "ejemplo",
  "title_id": "EXAM00001",
  "version": "1.2.3",
  "source_asset_url": "https://github.com/dev/repo/releases/download/v1.2.3/ejemplo.vpk",
  "archive_identifier": "psvitaalive-ejemplo",
  "remote_name": "Ejemplo v1.2.3.vpk",
  "catalog_commit_sha": "<sha>"
}
```

Este evento debe firmarse/autenticarse y **todos sus campos deben ser revalidados en el servicio receptor**. No confiar en un `archive_identifier` ni `source_asset_url` arbitrario enviado al Worker; resolverlos contra la lista autorizada y el snapshot fijado a un SHA. No introducir una API pública que permita SSRF o que suba archivos de terceros a nuestra cuenta.

**Contrato conceptual de confirmación**: `job_id`, `app_id`, `archive_identifier`, nombre remoto, tamaño/hash verificados, URL IA, `verified_at` y origen de la comprobación. GitHub debe revalidar esta confirmación y la versión canónica actual, no aceptarla como autoridad suficiente para escribir.

Retención: el historial verificable de jobs debe sobrevivir al vencimiento de logs de Workflows; definir almacenamiento durable y límites de uso antes de implementar (registro pequeño en GitHub o servicio gratuito adecuado, no un cuarto catálogo público consumido por Vita).

## 9. Seguridad, licencias y controles administrativos

- Internet Archive requiere credenciales IA-S3 válidas, privadas y capaces de escribir en **los ítems que controla el mantenedor**. No guardar secretos en JSON, commits, logs, frontends ni datos enviados a la Vita. Evaluar cuenta y alcances reales: IA-S3 no debe asumirse limitado por ítem sin comprobarlo.
- Usar secrets de Cloudflare y permisos mínimos GitHub (App/token granular). Firmar solicitudes y respuestas y proteger contra replay (nonce/timestamp, idempotency key).
- Verificar que se puede **redistribuir** legalmente cada VPK (licencia/permiso del autor y política Archive.org). En especial, no inferir permiso a partir de que la URL de descarga sea pública o de que la ficha sea Legacy.
- Prevenir SSRF, URLs `localhost`/IP privadas, esquemas no HTTPS, redirecciones a hosts no autorizados, path traversal y nombres de archivos maliciosos.
- Límite de concurrencia global y **bloqueo por ítem**, reintentos con backoff, cuota/budget y apagado de emergencia. Nunca habilitar subidas masivas sin pruebas.
- Acciones administrativas: `dry-run`, `pause`, `resume`, `retry` y `manual-review`; bitácora por aplicación y versión.
- No actualizar Game Files, datos, plugins, changelog, iconos, autores, categorías, título ni mirrors por efecto colateral.

## 10. Despliegue por fases: no avanzar sin validar la anterior

**Fase 0 — inventario ligero de pendientes, sin subir**
- Cruzar JSON canónicos, **pendientes señalados por Orbit** y `archive_upload_log.csv` para elegir destino y versión; dar por respaldadas las versiones antiguas que el mantenedor ya publicó, **sin recorrer ni revalidar todo el historial**.
- Consultar Archive.org **solo cuando sea necesario** para confirmar un destino dudoso o un archivo objetivo con resultado incierto; no hacer inspección masiva de VPK históricos.
- Emitir informe `ready / missing / outdated / review / no_item / already_backed_up` y revisar las excepciones.
- No escribir JSON ni tocar archivos de Archive.org.

**Fase 1 — prueba de IA sobre un ítem controlado**
- Elegir VPK pequeño, permiso demostrado y un ítem existente.
- Subirlo con nombre versionado *nuevo* y confirmar que conserva los VPK anteriores.
- Repetir sin crear otro archivo; forzar fallo y documentar comportamiento.

**Fase 2 — Cloudflare mínimo y VPK grande**
- Worker autenticado + Workflow; primero probar streaming directo a IA.
- Si falla o no reanuda, evaluar R2 staging/multipart y coste real.
- Medir memoria, tiempo, tamaño y fallos. Sin credenciales en GitHub público.

**Fase 3 — procesar pendientes en lotes controlados**
- Construir cola real con límites de concurrencia y confirmación humana inicial.
- Probar tanto URL de GitHub con versión ya en catálogo como nueva release superior.
- Habilitar reconciliación «igual versión, solo URL» únicamente con verificación de IA.

**Fase 4 — actualizaciones futuras backup-first**
- Activación por lista de apps habilitadas, nunca cambio global instantáneo.
- Integrar Orbit y retorno autenticado a GitHub; conservar fallback de sincronizador actual cuando la app no participa.
- Verificar no regresión de versiones, edición manual, enlaces Game Files y notificaciones Discord (solo después de catálogo publicado).

**Fase 5 — operación, auditoría y ampliación**
- Seguimiento de nuevos respaldos, manejo de caídas prolongadas de Archive.org, panel de pendientes y procedimientos de recuperación. No reauditar innecesariamente el historial antiguo.
- Expandir elegibilidad con evidencia de permisos y prueba de costes/salud operativa.

**Criterios mínimos de aceptación antes de activar producción:**
1. Todas las versiones anteriores siguen presentes y descargables.
2. Un mismo job repetido no sube duplicados ni modifica otros archivos.
3. El JSON canónico solo cambia tras confirmación verificable; los generados solo los escribe el pipeline oficial.
4. Una versión ya catalogada pero pendiente de respaldo se reconcilia sin degradar versión ni alterar otros links.
5. Funcionan reintentos acotados, cancelación, revisión humana y mecanismo de apagado.
6. Se prueba al menos un VPK pequeño y uno grande, incluyendo red/IA fallando.
7. No se presupone disponibilidad ni coste cero cuando Cloudflare/IA impongan límites.

## 11. Riesgos/decisiones abiertas

- Elegir entre streaming directo e intermedio R2 **según pruebas**, no por suposición.
- Calcular costes y límites reales; Workflows gratuito existe pero no elimina límites de CPU/subrequests/memoria/estado.
- Cómo persistir job state y eventos de callback con autenticación e idempotencia interrepositorios.
- Cómo confirmar control/permiso de ítem de forma automatizable y registrar excepciones manuales.
- Política de versiones móviles, prereleases, archivos con caracteres especiales y colisiones de nombre.
- Qué hacer si el catálogo ya anuncia una versión más nueva cuando finaliza el respaldo de una anterior.
- Qué hacer ante fuentes que retiraron una release; preservar solamente las que se puedan obtener de una fuente autorizada.
- Si un workflow existente actualiza el catálogo antes del nuevo respaldo durante el despliegue, tratarlo vía backfill, no forzar downgrade.

## 12. Regla final

**Una nueva versión detectada se considera pendiente, sin inspección masiva de versiones anteriores.**
**Si Archive.org falla, encolar/reintentar después sin modificar el catálogo ni perder el trabajo.**
**Primero preservar y verificar el archivo NUEVO; después actualizar el enlace canónico.**
**Los pendientes actuales y las actualizaciones futuras deben usar el mismo motor.**
**Nunca borrar versiones previas ni crear un ítem nuevo por actualización.**
**No romper ni desactivar el sincronizador actual hasta superar las pruebas.**

Este archivo es un **plan de diseño**, no una afirmación de que la integración esté activa.
