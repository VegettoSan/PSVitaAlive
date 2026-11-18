# Sincronización automática con PSVitaAlive

Esta integración conecta el monitor de releases con el catálogo canónico de PSVitaAlive.
**No convierte el monitor en una fuente de verdad para el catálogo.** Solo genera
un snapshot de releases y avisa al workflow oficial de PSVitaAlive.

## Qué sucede

1. Se modifica `apps/` en PSVitaAlive y se ejecuta `validate.yml`.
2. Tras finalizar satisfactoriamente su publicación, PSVitaAlive emite
   `repository_dispatch: psvitaalive_catalog_published` al monitor con el SHA
   del commit publicado.
3. El monitor consulta el `catalog.json` de ese SHA (no una copia antigua de
   `main`), descubre repositorios de GitHub/GitLab, revisa sus releases, actualiza GitHub Pages
   y guarda `data/upstream_latest.json`.
4. El monitor emite `release_monitor_synced` a PSVitaAlive con su SHA de commit.
5. PSVitaAlive evalúa el snapshot con `scripts/sync_release_updates.py`,
   reconoce el **único enlace .vpk** entre los assets de la release y
   solo modifica `apps/*.json` cuando la actualización es inequívoca.
   No descarga, descomprime ni inspecciona binarios.
6. El workflow oficial `validate.yml` vuelve a validar y genera
   `catalog.json`, `authors.json` y `categories.json`.

**Prioridad absoluta: respetar los respaldos manuales.** En cada escaneo,
el actualizador primero compara la versión de la release contra la versión
existente en el JSON canónico de `apps/`. Si son iguales, si el catálogo ya
es más reciente, o si no se pueden comparar con seguridad, **no modifica
ningún campo**: ni versión, ni fecha, ni tamaño, ni enlaces de descarga,
ni Mirrors de Archive.org. Así, después de que el mantenedor reemplace el
enlace automático de GitHub por su respaldo en Archive.org, futuras
sincronizaciones de la misma versión nunca revierten ese cambio. Solo una
release con una **versión estrictamente posterior** puede volver a modificar
el enlace del botón `Download` llamado `VPK`. No crea Mirrors adicionales
ni conserva el enlace antiguo en un botón nuevo.

No hay un bucle infinito: una versión ya incorporada no genera más cambios.
La tarea programada del monitor cada tres horas sigue como respaldo, a minuto
17 para evitar la congestión de las horas en punto.

## Notificaciones de Discord tras actualizaciones automáticas

Cuando un release de GitHub/GitLab actualiza `apps/*.json`, el workflow
`Sync Verified Homebrew Releases` ejecuta el validador oficial
`Validate and Generate Local Catalog`. Una vez que el validador termina
de publicar `catalog.json`, emite el evento interno
`repository_dispatch: psvitaalive_catalog_ready` para ejecutar
`PSVitaAlive Discord Notifications`.

Este evento usa **el `GITHUB_TOKEN` existente** con el permiso
`contents: write` del validador; no requiere un secreto nuevo. GitHub
permite `repository_dispatch` entre workflows aunque los commits
realizados por un bot no originen ejecuciones `push` adicionales.
La vía anterior por `workflow_run` sigue presente como respaldo.

El notificador compara el catálogo ya publicado con
`.github/discord/state.json` mediante **Title ID y versión**. Si no
hay cambios, no manda mensajes. Si hay versiones nuevas aún no
anunciadas, publica los avisos a `#homebrew-updates` en grupos de hasta
10 y guarda el estado confirmado; los avisos posteriores de las mismas
versiones se omiten. Si falla una publicación, no avanza el estado.

Las novedades de los desarrolladores detectadas por Orbit son un flujo
**distinto**: el anuncio del catálogo solo ocurre después de incorporar
la versión a PSVitaAlive. No se descargan VPK y no se modifica la
arquitectura ni el formato de los catálogos.

## Configuración manual de los dos secrets

GitHub limita el `GITHUB_TOKEN` a su propio repositorio. Por tanto, se necesitan
dos fine-grained personal access tokens (o equivalentes de GitHub App) para la
comunicación entre repositorios. **Nunca pegar tokens en commits, Issues ni chats.**

- En **VegettoSan/PSVitaAlive**, crear el secreto de Actions
  `PSVA_TRACKER_DISPATCH_TOKEN` con acceso **solo** al repositorio
  `VegettoSan/PsVita-Homebrews-New-Releases` y permiso **Contents: Read and write**.
- En **VegettoSan/PsVita-Homebrews-New-Releases**, crear el secreto de Actions
  `PSVA_AUTOUPDATE_DISPATCH_TOKEN` con acceso **solo** al repositorio
  `VegettoSan/PSVitaAlive` y permiso **Contents: Read and write**.

Ruta: Settings → Secrets and variables → Actions → New repository secret.
Se recomienda establecer una fecha de expiración y rotarlos periódicamente.

Si faltan los secrets, el workflow registra un aviso y sigue funcionando; no
se afirma que exista sincronización instantánea sin ellos.

### Diagnóstico de permisos de dispatch

Si el paso `Trigger release tracker after successful catalog publication` responde
`HTTP 403 Resource not accessible by personal access token`, revisar que el
secreto `PSVA_TRACKER_DISPATCH_TOKEN` contenga un token con alcance al
**repositorio receptor**, `VegettoSan/PsVita-Homebrews-New-Releases`, y permiso
**Contents: Read and write**. Cambiar los permisos requiere volver a ejecutar
la validación y comprobar el evento en el workflow del monitor; un commit de
documentación también ejecuta `validate.yml` sin editar `apps/`.

### Verificar la conexión sin activar la escritura de aplicaciones

Una ejecución del workflow `Validate and Generate Local Catalog` en PSVitaAlive
con `PSVA_TRACKER_DISPATCH_TOKEN` configurado debe enviar el evento
`psvitaalive_catalog_published` a Orbit. Comprobar que el monitor reciba
el evento y regenere `data/upstream_latest.json` antes de configurar
`PSVA_AUTOUPDATE_DISPATCH_TOKEN`, que habilita el camino de vuelta.
Nunca copiar el valor de un token en archivos o mensajes.

## Política estricta de actualizaciones automáticas

Por defecto solo se actualizan aplicaciones `Verified` o `Legacy` cuando:

- Hay un **único** Homebrew asociado explícitamente al repositorio de GitHub
  o GitLab. En GitLab se admiten proyectos con grupos y subgrupos.
- El Title ID no está duplicado; el tag estable es una versión numérica
  estrictamente superior y con fecha válida.
- La release publica **exactamente un asset con extensión .vpk** entre los
  metadatos de GitHub/GitLab. Si hay **cero VPK** (por ejemplo, solo ZIP)
  o **dos o más VPK**, se registra `REVIEW` y **no se edita el catálogo**.
- El asset elegido tiene una URL de release aceptada del repositorio:
  GitHub Releases o un enlace permanente de GitLab Releases/archivo adjunto
  de ese mismo proyecto. Se prioriza `direct_asset_url` de GitLab si existe.
  Se rechazan URL de dominios ajenos o proyectos distintos.
- Existe **exactamente un enlace de tipo `Download` con nombre `VPK`**
  en el JSON individual. Si falta ese botón o hay dos con el mismo nombre,
  se solicita revisión manual. Los botones `VPK Lite`, `VPK debug` o
  de otras configuraciones no se modifican.
- La identificación del VPK se hace **solo a partir del nombre y del enlace
  incluidos en los metadatos de GitHub Releases**. No se descarga ni se abre
  el VPK; tampoco se consulta `param.sfo` ni se calcula un hash.
- La fecha `version_date` se obtiene de la publicación de la release;
  el tamaño se actualiza cuando GitHub informa un valor positivo.
- El botón `VPK` existente apunta a Archive.org o a una fuente GitHub/GitLab
  registrada en esa aplicación. **No es necesario que sea el recomendado**;
  no se modifican sus atributos de recomendación ni los de otros botones.
- Los enlaces **Game Files, Data Files, Plugin, Mod, Patch, otros Download y
  Mirror no bloquean** la actualización de un VPK principal inequívoco.
  Permanecen exactamente como están, con sus URLs, atributos y posiciones.
  La compatibilidad de datos y plugins existentes con el nuevo VPK
  **no se puede garantizar** sin inspeccionar archivos y requiere
  comprobación manual cuando el desarrollador cambie los requisitos.

Se preservan Title ID, autores, categorías, status, iconos, capturas,
enlaces secundarios y otros metadatos. Se actualizan exclusivamente
`version`, `version_date`, `size` (si GitHub lo suministra; GitLab puede
no incluirlo) y **la URL del mismo botón `Download` llamado `VPK`**.
No se crean nuevos botones, no se renombran, no se borran otros enlaces
y no se añaden Mirrors de la versión anterior. El respaldo anterior deja
de estar enlazado desde ese botón cuando se reemplaza la URL, exactamente
igual que en el mantenimiento manual habitual de PSVitaAlive.

El monitor sigue mostrando ambos estados EN LA MISMA ficha del release:
si el catálogo ya ofrece la nueva versión desde GitHub/GitLab, marca
«Falta respaldo en Archive.org» hasta que el mantenedor suba el VPK y cambie
la URL principal a la nueva descarga de Archive.org. Los VPK antiguos con
nombre sin número de versión siguen reconociéndose por el enlace principal
y el contexto de la versión del catálogo. No hay página o apartado adicional.

**Pre-releases con período de gracia:** por defecto se prefieren versiones
estables. Un pre-release puede promoverse sin opt-in si tiene una versión
estrictamente superior y se publicó **más de siete días completos después de
la última versión estable**, o si GitHub confirma mediante `/releases/latest`
(HTTP 404) que no existe ninguna estable. No se calcula una semana a partir
de la fecha actual, sino entre las publicaciones de ambos releases. El monitor
busca la estable entre los primeros 20 releases y consulta
`/releases/latest` cuando no aparece ninguna. Los errores de la API, fechas
incompletas y versiones no comparables se dejan para revisión.

La opción por aplicación `allow_prereleases: true` (V2: `apr: true`)
**elimina la espera** para desarrolladores que utilizan betas habitualmente,
pero nunca permite versiones anteriores o un pre-release previo a la última
estable. Los sufijos reconocidos incluyen `dev/test`, `alpha`, `beta`,
`pre/preview` y `rc`; estable `1.2` es posterior a `1.2-rc1`.
Fuentes GitLab y GitHub-folder sin confirmación de ausencia de estables
mantienen el opt-in explícito para pre-releases.

**Repositorios GitHub renombrados:** un repositorio puede cambiar de
propietario sin perder sus releases. Cuando los assets pasan a un nombre
distinto, el monitor consulta `/repos/{owner}/{repo}` del nombre original,
sigue la redirección oficial de GitHub y verifica `full_name` e ID numérico.
Solo entonces publica `canonical_repo` y `repo_id` en el snapshot.
El sincronizador mantiene el vínculo de origen antiguo pero permite la URL
GitHub Releases del repositorio canónico, únicamente cuando coincide el tag y
hay un VPK inequívoco. Esto resuelve, por ejemplo, la migración
`fr3dsp/sonic-r-vita` → `PupueStudios/sonic-r-vita`.

Siguen excluidos los repositorios compartidos sin identidad única, los
VPK ambiguos o ausentes, las fuentes no verificadas, las versiones iguales
o anteriores, las múltiples fuentes que compitan por el mismo JSON y los
metadatos sin una fecha fiable. Los enlaces de Game Files, plugins y mirrors
no se modifican al sincronizar el VPK principal. Las fuentes GitLab sin
`direct_asset_url` continúan sujetas a sus restricciones anteriores;
se utiliza `REVIEW` para los casos que requieren intervención manual.

La validación automática es deliberadamente ligera: comprueba versiones
comparables, origen único y presencia de un solo archivo `.vpk` en los
metadatos del release. **No demuestra que el VPK sea instalable ni que su
Title ID interno coincida**, ni que los Game Files o plugins existentes
sean compatibles con la versión nueva. Los casos de identificación
ambigua siguen pendientes para revisión manual.

En los workflows manuales se muestra `DRY-RUN` sin editar el catálogo;
las ejecuciones automáticas autorizadas modifican exclusivamente los JSON
de `apps/` y usan `validate.yml` para generar los catálogos oficiales.

## Prueba controlada y orden de activación

1. Fusionar primero la rama del workflow en **PSVitaAlive**, pero dejar los
   secrets sin configurar.
2. Fusionar luego la rama del **monitor**.
3. Ejecutar el monitor manualmente y verificar `data/upstream_latest.json`.
4. En PSVitaAlive → Actions → **Sync Verified Homebrew Releases** →
   **Run workflow** (rama main, monitor_ref = main). Las ejecuciones
   **manuales son siempre simulaciones**; revisa `DRY-RUN` y `REVIEW`
   en los logs. No se modifica ningún JSON.
5. Ejecutar la suite de tests de PSVitaAlive. Configurar el secret de salida
   hacia el monitor primero; esperar a tener validados los candidatos antes
   de configurar el token de retorno que activa escrituras automáticas.
6. Ejecutar `validate.yml` manualmente y verificar que el monitor vuelve a
   sincronizarse. Comprobar la segunda comunicación, el validador oficial,
   y que no modifica innecesariamente `apps/`.

No modificar nunca `catalog.json` a mano ni habilitar antiguos feeds externos.


## VPKs almacenados en carpetas de GitHub (sin Releases)

Para repositorios revisados que colocan cada port en una carpeta propia, el monitor recopila VPKs desde Git Trees y el README de cada carpeta, sin descargar los binarios. Primer proveedor habilitado: https://github.com/papilenote/vita-ports (rama main).

El monitor publica las entradas independientes en folder_releases dentro de data/upstream_latest.json, sin modificar el esquema de releases tradicionales. Cada entrada proporciona carpeta, Title ID, version, fecha del commit que modifico el VPK, tamano, SHA del blob y URL RAW fijada al commit (no a main).

Para asociar cada carpeta con un Homebrew, su JSON individual en apps/ debe contener un enlace Repository u Official Website a la carpeta exacta, por ejemplo https://github.com/papilenote/vita-ports/tree/main/Tap%20Sonic, y un enlace Download llamado VPK. El sincronizador exige coincidencia de repositorio, rama, carpeta y Title ID del README; una version mayor comparable; fecha valida y VPK unico con URL y SHA de commit confiables. Si hay varias versiones en la carpeta, el monitor exige que el nombre de un unico VPK corresponda a la version declarada. La ausencia de version, los duplicados y las ambiguedades impiden el cambio automatico.

Los Game Files, espejos, plugins y cualquier otro boton se conservan. Si la version registrada es igual o mayor, nunca se reemplaza un enlace respaldado en Archive.org. No se crean aplicaciones automaticamente a partir de carpetas desconocidas: requieren un JSON canonico y validacion. Para habilitar un nuevo repositorio multiport es necesario revisar sus convenciones y agregarlo explicitamente a la lista de proveedores del monitor y del sincronizador.
