# PSVitaAlive — Corrección aislada: consulta asíncrona del espacio ux0

> **Estado actualizado (12-10-2026):** este documento relata la variante anterior de UX0 asíncrono con sondeo periódico. La variante **definitivamente aprobada para integración es UX0 por eventos** (lectura inicial y al terminar operaciones relevantes), incluida en `main` mediante [PR #28](https://github.com/VegettoSan/PSVitaAlive/pull/28). Se conserva la historia técnica inferior, pero “main intacta” ya no indica el estado presente. La comprobación de esos eventos y del cierre en consola real continúa pendiente. Ver [estado actual](CLIENT_PERFORMANCE_STATUS_20261012.md) y [diseño por eventos](EVENT_DRIVEN_UX0_REFRESH_20261012.md).

**Fecha:** 2026-10-12.
**Estado:** corrección en rama experimental, pendiente de pruebas hardware.
**Rama:** `fix/async-ux0-space-20261012`.
**Base inmutable:** `stable/async-image-buckets-20261012` @ `321c806da986c2f37e1757d9376d19520478d79a`.
**main:** intacta. **No publicar release sin autorización.**

## Evidencia: quinta sesión PS Vita real

**Build:** `image-request-contention-profile-20261012`, procedente de
`perf/image-request-contention-20261012` (no de stable).
`session.log`: 568 líneas. Inicio caché imágenes = **2.018.318 B**
(no vacía), catálogo local = 2.101.356 B; **1118** aplicaciones.

**Marcadores claves del mismo incidente (detalle Arcaea):**

```text
[PERFUX0] devctl_ms=463 ret=0 origin=footer_or_space_label
[PERFSPLIT2] total_ms=1551 ... footer_ms=1545 report_ms=0 news_ms=0 ...
[PERFSEG] total_ms=3017 ... images_ms=1 draw_ms=3015
[Perf] slow frame us=3017106 mode=2 textures=18 deferred=0
```

**Interpretación rigurosa:**
- **CONFIRMADO:** `sceIoDevctl("ux0:",0x3001,...)` tardó al menos 463 ms
  dentro del camino que dibuja el footer. La respuesta fue correcta `ret=0`.
- **CONFIRMADO:** el footer concentró 1545 ms de dibujo antes de emitir
  su marcador. `draw_ms=3015` y `frame=3017` incluyen las llamadas
  a `diagnostics::log()` de esos marcadores, las cuales escriben
  sincrónicamente en `ux0:` (mutex + fopen/write/close).
- **NO CONFIRMADO:** que los restantes ~2,55 s procedan exclusivamente
  de `sceIoDevctl`, GPU, chips o IO de capturas. La instrumentación
  cambia condiciones y contamina las medidas.
- `[PERFIMGRQ2]`: **0** eventos >=90 ms en esta prueba;
  `[PERFREQUEST]`: **0**; no atribuir esta pausa a
  `ImageCache::request`. Es posible que las solicitudes
  de menor duración acumulen algún coste en otros frames.
- Segundo mayor frame registrado: **709 ms** (preparación de
  imágenes `images_ms=700`, `draw_ms=8`);
  también 674 ms y 573 ms de preparación de imágenes.
  Es **otro problema aún pendiente**, no resuelto por el cambio ux0.
- `[PERFTEX2]`: 9 eventos, mediana `load_ms=154`,
  `log_ms=56`, `stat_ms=2`,
  `insert_ms=0` ms redondeados. Son lecturas de la sesión
  diagnóstica, NO de esta corrección.
- `[ImageCache] ready`: **14** (9 iconos, 5 capturas);
  `failed=0`, `cancelled=0`. No se alteraron los enlaces ni el
  failover Archive.org.
- `parser_complete=5952 ms` (DOM 4919, conversión 963);
  `startup_homebrew_request` a `startup_screen_first_set` = 6,671 s
  con catálogo local. No es una prueba de optimización del parser.

## Alcance de la corrección

**Archivo de código:** `Client PSVitaAlive/source/ui/full_catalog_screen.cpp`.

Antes:
`drawFooterBar()` → `queryUx0Space()` → hasta cada 3 segundos
**`sceIoDevctl` en el hilo que dibuja toda la interfaz**.

Después:
- Worker de bajo nivel de prioridad `PSVitaAliveUx0Space`,
  16 KB de stack, consulta `sceIoDevctl` fuera del render.
- Cadencia nominal **3 s después de cada consulta**,
  con interrupción por stop cada 100ms al dormir.
- Pequeño mutex VitaSDK independiente protege **solo copia**
  de `Ux0SpaceInfo` (válido, bytes libres, total).
  **NO** se adquiere mutex durante `sceIoDevctl` ni `log()`.
- `drawFooterBar()` y `ux0FreeSpaceLabel()` siguen consumiendo
  `queryUx0Space()`, que ahora lee instantáneamente una
  instantánea de memoria y no ejecuta ninguna operación de disco.
- En el primer instante antes de la respuesta, la barra muestra
  `ux0 n/a`, sin congelar. Cuando hay una respuesta correcta,
  muestra los mismos valores y barra gráfica que antes.
- Si una actualización posterior falla, mantiene el último
  dato válido; no sustituye lo anterior por cero.
- El worker se inicia al inicializar UI y se detiene/junta
  antes de liberar las texturas/UI; elimina thread y mutex,
  admite `shutdown()` repetido.
- En logs: `[Ux0Space] async worker started refresh_ms=3000 ui_io=none`
  al arrancar; al apagar, un único resumen
  `[Ux0Space] async worker stopped queries=... slow_ge_150ms=... longest_ms=...`
  medidos por el trabajador SIN logs repetidos por cada lectura.
  Si falla la creación de recursos, no hace fallback a I/O
  sincrónico: el footer conserva `ux0 n/a`.
- **No se incorpora** la instrumentación `PERF*` masiva:
  se parte de la rama estable ya probada en PS Vita.

**Fuera de alcance y sin cambios:** `ImageCache` v3,
hilos de descarga de iconos/capturas, CURL,
sondeo/ranking/failover de Archive.org, instalador y VPK/PKG,
datos de `apps/`/`authors/`/`categories/`,
catálogos generados, UI de botones, tipografías,
detección de instalados/updates, plugins.

## Riesgos y validaciones pendientes

1. **Ejecución asíncrona:** primera lectura puede mostrar
   `ux0 n/a` brevemente. Confirmar que valores y barra
   reaparezcan en hardware y reflejen cambios de espacio.
2. **Memoria/hilos:** +1 hilo a 16 KiB de pila, mutex
   y tres enteros de métrica; se debe verificar arranque,
   cierre limpio, ausencia de crash y que la UI no
   tenga contención de locks.
3. **Si IO se bloquea durante cierre**: `shutdown()`
   espera a que la llamada devctl del worker retorne;
   ya no congela fotogramas durante navegación,
   pero podría alargar un cierre excepcional.
   Validar ese escenario y evaluar timeout solo si real.
4. En builds de instrumentación anteriores, los logs
   `PERFUX0` contaminaban algunos frames. No extrapolar
   cambios cuantitativos exactos entre escenas diferentes.
5. Las pausas de **300–700ms en iconos/scroll** persisten
   como línea de trabajo posterior: preparar imágenes,
   PNG Vita2D y logging síncrono de diagnóstico.

## Protocolo de aceptación

- [x] VitaSDK oficial CI `vitasdk/vitasdk:2026.08`
  compilado; commit exacto, tamaño, integridad y SHA256 registrados.
- [ ] En PS Vita real, verificar build marker
  `async-ux0-space-20261012 main=untouched fix=1`
  y `[Ux0Space] async worker started`.
- [ ] Cargar catálogo con caché caliente.
  Pasearse por Homebrew en grid y detalles
  al menos durante el período de actualizaciones del
  espacio en `ux0:`. Confirmar barra UX0 y cero
  pausas de ~1–3 s en el footer.
- [ ] Repetir con imágenes vacías y
  varias capturas descargándose (conservando
  datos y configuración; renombrar directorio v3
  con la aplicación cerrada y guardar backup).
- [ ] Comprobar cambios de espacio libre tras
  descargas/instalaciones (con operaciones seguras)
  y que los valores refresquen sin bloquear.
- [ ] Cerrar/abrir cliente y confirmar
  `[Ux0Space] async worker stopped`;
  no cierres dobles ni errores.
- [ ] Evaluar efectos de logger, imágenes, Archive.org,
  memoria y permisos `ux0:` sin degradación.
- [ ] Tras validar, crear **nueva stable inmutable**
  o registrar explícitamente rollback; no modificar
  `stable/async-image-buckets-20261012`.
- [ ] Solo después, preparar integración eventual a
  `main` mediante PR, conservar cambios independientes
  de `main` (especialmente `apps/halo-ce.json`)
  y regenerar JSON por Action. Solicitar aprobación.

**Rollback disponible:** `stable/async-image-buckets-20261012`.

## Build oficial y paquete verificado

- **CI:** https://github.com/VegettoSan/PSVitaAlive/actions/runs/38312277356
- **Resultado:** SUCCESS.
- **SHA del código compilado:** `37066473764e68da925774a5c4a4c575888521ba`.
  (Es anterior a la documentación adicional; el VPK incluye el código
  de esta corrección exactamente, no los commits de documentación.)
- **Artifact ID:** `11775670477`.
- **VPK de prueba:** `PSVitaAlive-Async-UX0-Space-20261012.vpk`.
- **Tamaño:** 9.648.566 bytes. **120** entradas ZIP.
- **Contenido:** `eboot.bin` y `sce_sys/param.sfo` presentes.
  `ZipFile.testzip()` sin errores.
- **SHA-256:** `b0d574fb7ed38350e71754a13878c5f58b58ec95fdd2b11261194d0e8cd6264a`.
- **Estado:** compilado y empaquetado; **NO VALIDADO TODAVÍA EN
  PS VITA REAL**. Esperar nuevo `session.log`.

**Procedimiento inmediato:** instalar el VPK de esta rama;
revisar texto `[Ux0Space] async worker started` y
que el footer actualice el espacio; explorar grid
y detalle durante descargas para repetir el
escenario del bloqueo de 3,017 segundos.
Esperar que ya NO aparezca ese retraso por consulta
UX0 en el hilo UI, pero medir antes de asegurar
su desaparición. Abrir/cerrar una vez para registrar
resumen `[Ux0Space] async worker stopped ...`.
