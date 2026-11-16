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

Se **omiten** (no se fuerzan) prereleases, versiones no numéricas,
repositorios compartidos, VPK con nombre principal ambiguo o ausente,
enlaces VPK de fuentes ajenas al repositorio registrado o Archive.org,
dos fuentes que intenten actualizar el mismo JSON en un escaneo y
situaciones de identidad dudosa. La presencia de Game Files o plugins
**ya no es un bloqueo** para cambiar solo el VPK principal.
En GitLab sin `direct_asset_url` solo se aceptan adjuntos alojados en
el mismo proyecto; enlaces externos requieren revisión manual. El log indica
`REVIEW` para que puedan catalogarse manualmente.

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
