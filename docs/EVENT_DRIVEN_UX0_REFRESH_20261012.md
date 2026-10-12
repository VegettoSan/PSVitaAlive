# PSVitaAlive — UX0: actualizar capacidad solo al iniciar y al terminar transferencias

**Fecha:** 2026-10-12.
**Estado:** prueba de implementación; NO stable, NO main.
**Nueva rama:** `fix/event-driven-ux0-space-20261012`.
**Padre:** `fix/async-ux0-space-20261012`; última estable real verificada en hardware
`stable/async-image-buckets-20261012` @
`321c806da986c2f37e1757d9376d19520478d79a`.
La corrección padre aún no había sido promovida a estable.

## Sesión real recibida: corrección async con consulta cada 3 segundos

Log original: `session.log` del 12 de octubre, **612 líneas**,
marcador `async-ux0-space-20261012 main=untouched fix=1`.
La caché de imágenes inicial era **2.581.771 B**,
el catálogo de 1.118 aplicaciones se sirvió desde el JSON
local de 2.101.356 B.
`parser_complete=6563ms` (DOM 5445ms,
conversión 1052ms). Desde
`startup_homebrew_request t_us=3267273`
hasta `startup_screen_first_set t_us=10601278`:
**7.334 s**.

Se confirmó `[Ux0Space] async worker started refresh_ms=3000 ui_io=none`.
No aparece `[Ux0Space] async worker stopped` ni
`PSVitaAlive session END`; esta sesión no confirma
el cierre normal del hilo. Tampoco aparecen registros de
instalaciones/descargas de Homebrew del usuario: **esta prueba
NO valida todavía actualizaciones por eventos**.
La interfaz continuó descargando imágenes y navegando en
detalle, sin el anterior frame de 3.017 ms en esta sesión.

**Lentitud residual (independiente de UX0):**
80 eventos `[Perf] slow frame` publicados; pico
**935.861 ms**, otros 678.467ms, 545.421ms, 516.797ms,
mayormente `mode=0`, `textures=18`,
`deferred=9` (scroll con reciclado de texturas).
21 `[ImageCache] ready`, 0 failed,
3 `[ImageCache] cancelled` por cambios de pantalla/visibilidad.
Algunos `vita2d_load_PNG_file` y trabajo de memoria
siguen siendo síncronos; el cambio de UX0 no los
soluciona. No comparar distribuciones generales de frames
sin un muestreo equivalente.

## Petición del usuario y criterio funcional

Eliminar refresco fijo de 3000ms; conservar la barra de
espacio y actualizar únicamente:
1. Una vez durante `FullCatalogScreen::init()`, fuera del render.
2. Tras una descarga gestionada por el instalador, cuando
   `InstallStatus` pasa de `Downloading` a `Installing`
   (archivo disponible y transferido).
3. Tras un resultado terminal de instalación/descarga
   `Completed`, `Failed` o `Cancelled` (incluye espacio
   liberado por limpieza). El detector usa **transiciones**
   para evitar consultas repetidas durante cada frame.
4. Al resolver un ZIP recuperable, por ejemplo al borrar
   el archivo en una decisión de Keep/Delete.
5. Una vez al acabar el asistente **opcional** de descarga
   masiva de imágenes (`runImageWarmup`). Las descargas de
   iconos/capturas automáticas al navegar **NO** disparan
   actualizaciones independientes.
6. **Sin** consulta periódica, ni al desplazarse, ni en
   cada operación de `drawFooterBar`.

## Implementación

- `Client PSVitaAlive/source/ui/full_catalog_screen.cpp`:
  el worker VitaSDK espera bloqueado con
  `sceKernelWaitSema` sobre un semáforo de contador 0..1,
  sin bucle `sceKernelDelayThread`. El inicio señala
  exactamente una petición; las completaciones llaman
  `sceKernelSignalSema`.
- El semáforo de un solo permiso **coalescea**
  varias peticiones casi simultáneas. El hilo consulta
  `sceIoDevctl("ux0:",0x3001,...)` fuera del render
  y fuera del mutex, y escribe los datos correctos en
  una instantánea protegida por un mutex liviano.
- `Client PSVitaAlive/include/ui/full_catalog_screen.hpp`:
  `FullCatalogScreen::requestUx0SpaceRefresh()`,
  interfaz de señalización **no bloqueante**.
- `Client PSVitaAlive/source/main.cpp`:
  el bucle principal observa transiciones de
  `InstallStatus::State` y solicita el refresco
  una única vez por borde relevante. No se consulta UX0
  desde el bucle principal; solo se emite una señal.
  También señala tras recuperación ZIP y descarga
  masiva opcional.
- El `shutdown` activa stop y **señala el semáforo**
  para despertar el hilo dormido antes de `WaitThreadEnd`;
  elimina hilo, semáforo y mutex. Se mantiene la
  precaución del caso excepcional donde un `devctl`
  en curso demore la salida.
- Al faltar un primer valor, el indicador muestra
  `ux0 n/a`. Cuando refrescar falla, sigue mostrando
  la última lectura válida. No bloquear la UI en fallo
  de recursos/hilo.
- Etiqueta esperada en log:
  `[Ux0Space] async worker started mode=events startup=1 after_transfer=1 periodic=0`
  y, si la app cierra normalmente,
  `[Ux0Space] async worker stopped queries=... slow_ge_150ms=... longest_ms=...`.

## Alcance y limitaciones

**No se cambian** la caché de imágenes v3,
`ImageCache::request`, CURL/SSL/Archive.org, PKG/BGDL,
instalador, permisos, plugins, i18n, VPK/ZIP,
ni los JSON oficiales/generados.

Las descargas PKG en BGDL pueden seguir después de que
PSVitaAlive entregue el trabajo al sistema. El evento
`Completed` del instalador refleja la finalización
**observada por el cliente**; no se introduce un sondeo
nuevo del sistema externo. En ese caso, puede ser necesaria
una consulta al volver a abrir la tienda (se ejecuta al
inicio).

**Limitación del detector:** el bucle UI recibe estados
sondeados del instalador. El estado `Downloading`→
`Installing` solo se observa si sobrevive al menos
un fotograma (normalmente hay un asentamiento de 3 s
en instalaciones directas). Siempre se refresca al
estado terminal detectado; esta limitación se verificará
con pruebas de descargas muy rápidas, BGDL y Install All.

## Pruebas y criterios pendientes

- [ ] CI VitaSDK de la rama: SUCCESS y VPK con `eboot.bin`,
      `sce_sys/param.sfo`, CRC y SHA256.
- [ ] Confirmar en PS Vita marcador de esta rama
      `event-driven-ux0-space-20261012`, indicador ux0
      inicialmente calculado, sin congelamientos por devctl.
- [ ] Navegar **sin descargar ni instalar Homebrew** por
      varios minutos: el contador de queries al salir debe
      ser 1 (o el mínimo justificable por peticiones explícitas).
      Las descargas automáticas de iconos no lo incrementan.
- [ ] Completar descarga sin instalación, instalación
      normal VPK/ZIP, fallo y cancelación: comprobar
      actualización y ausencia de consultas repetidas por frame.
- [ ] Revisar Install All, plugins y casos especiales
      BGDL/PSP/PS1 para no omitir resultados terminales.
- [ ] Probar recuperación ZIP Keep/Delete y asistente de
      descarga completa de imágenes.
- [ ] Comprobar cierre normal, reinicio repetido,
      fallos de worker y que el hilo o semáforo no
      se queden bloqueados.
- [ ] Guardar `session.log` real y revisar picos
      residuales de 300–900ms por imágenes
      **en una fase posterior independiente**.
- [ ] Después de pruebas reales, promover exclusivamente
      la corrección funcional en rama stable NUEVA y PR
      controlado; nunca editar `main` sin aprobación.

**Rollback:** `stable/async-image-buckets-20261012`.
**No sustituir** la rama estable por esta versión aún.
