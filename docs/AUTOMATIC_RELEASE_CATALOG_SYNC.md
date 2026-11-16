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
   descarga y verifica el único VPK candidato y solo modifica `apps/*.json`
   cuando todas las comprobaciones son inequívocas.
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
- La release tiene **exactamente un asset con extensión .vpk** y enlace de
  descarga directa bajo el tag exacto publicado en GitHub. Si hay **cero VPK**
  (por ejemplo, solo ZIP) o **dos o más VPK**, se registra `REVIEW` y
  **no se edita el catálogo**: la app seguirá «Pendiente por actualizar».
- En una ejecución automática real (`--apply`) se descarga temporalmente el
  único VPK, se verifica el tamaño completo, el formato ZIP, la presencia de
  `sce_sys/param.sfo` y el `TITLE_ID` interno contra el de `apps/*.json`.
  Si el archivo no descarga, no abre o el Title ID difiere: `REVIEW` sin cambios.
  El VPK temporal se elimina después de comprobarlo.
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

Los VPK se **descargan temporalmente para validación binaria**, pero
**nunca se instalan ni ejecutan**; tampoco se guardan en GitHub Releases,
GitHub Pages ni Archive.org. La comprobación del `TITLE_ID` se hace leyendo
el `param.sfo` del ZIP, no deduciendo su identidad por el nombre del archivo.

Hay límites para no agotar GitHub Actions: hasta 4 GiB por VPK,
180 segundos por descarga y un máximo de 6 verificaciones por ejecución.
Los casos que excedan estos límites quedan pendientes para revisión manual
o un escaneo posterior. El hash SHA-256 del archivo descargado se registra
en el log de Actions para diagnóstico; no se inventan hashes en los JSON.

**Importante:** la ejecución manual sin `--apply` es solo un preview:
no descarga binarios ni afirma haber verificado su Title ID. Solo la ejecución
automática real acepta candidatos tras la descarga y comprobación.

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
