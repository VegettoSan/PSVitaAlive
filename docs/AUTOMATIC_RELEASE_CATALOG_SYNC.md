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
   `main`), descubre repositorios, revisa sus releases, actualiza GitHub Pages
   y guarda `data/upstream_latest.json`.
4. El monitor emite `release_monitor_synced` a PSVitaAlive con su SHA de commit.
5. PSVitaAlive evalúa el snapshot con `scripts/sync_release_updates.py`,
   reconoce el **único enlace .vpk** entre los assets de la release y
   solo modifica `apps/*.json` cuando la actualización es inequívoca.
   No descarga, descomprime ni inspecciona binarios.
6. El workflow oficial `validate.yml` vuelve a validar y genera
   `catalog.json`, `authors.json` y `categories.json`.

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

## Política estricta de actualizaciones automáticas

Por defecto solo se actualizan aplicaciones `Verified` o `Legacy` cuando:

- Hay un **único** Homebrew asociado explícitamente al repositorio de GitHub.
- El Title ID no está duplicado; el tag estable es una versión numérica
  estrictamente superior y con fecha válida.
- La release publica **exactamente un asset con extensión .vpk** y enlace
  directo de GitHub bajo el mismo tag. Si hay **cero VPK** (por ejemplo,
  solo ZIP) o **dos o más VPK**, se registra `REVIEW` y **no se edita
  el catálogo**: la app seguirá «Pendiente por actualizar».
- La identificación del VPK se hace **solo a partir del nombre y del enlace
  incluidos en los metadatos de GitHub Releases**. No se descarga ni se abre
  el VPK; tampoco se consulta `param.sfo` ni se calcula un hash.
- La fecha `version_date` se obtiene de la publicación de la release;
  el tamaño se actualiza cuando GitHub informa un valor positivo.
- Hay un solo Download actual/recomendado que sea VPK oficial de GitHub
  del mismo repositorio **o** VPK de Archive.org, y ningún recomendado distinto.
- No hay Game Files, Data Files, plugins, mods o instalación compleja.

Se preservan Title ID, autores, categorías, status, iconos, capturas, enlaces
secundarios y otros metadatos. Se actualizan exclusivamente versión,
fecha, tamaño y URL del VPK actual. Si el Download anterior apuntaba a
Archive.org, se añade una copia como Mirror etiquetada claramente
«respaldo anterior (versión previa)», antes de cambiar el Download principal
al nuevo VPK de GitHub. Se mantiene como único enlace recomendado el
Download actual; el Mirror antiguo queda con recommended=false.

El monitor sigue mostrando ambos estados EN LA MISMA ficha del release:
si el catálogo ya ofrece la nueva versión desde GitHub, marca
«Falta respaldo en Archive.org» hasta que el mantenedor suba el VPK y cambie
la URL principal a la nueva descarga de Archive.org. Los VPK antiguos con
nombre sin número de versión siguen reconociéndose por el enlace principal
y el contexto de la versión del catálogo. No hay página o apartado adicional.

Se **omiten** (no se fuerzan) instalaciones complejas, GitLab, prereleases,
versiones no numéricas, repositorios compartidos, fuentes de descarga
ajenas a GitHub/Archive.org y situaciones de identidad dudosa. El log indica
`REVIEW` para que puedan catalogarse manualmente.

La validación automática es deliberadamente ligera: comprueba versiones
comparables, origen único y presencia de un solo archivo `.vpk` en los
metadatos del release. **No demuestra que el VPK sea instalable ni que su
Title ID interno coincida**. Los casos ambiguos o con instalación compleja
se dejan pendientes para revisión manual.

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
