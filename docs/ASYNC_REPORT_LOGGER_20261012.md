# PSVitaAlive — Logger para reportes sin E/S durante la navegación (12-10-2026)

## Estado e integración aprobada

- **Integración autorizada:** este documento pertenece al [PR #30](https://github.com/VegettoSan/PSVitaAlive/pull/30), aprobado por el mantenedor tras probar en **PS Vita real** el VPK experimental del logger. Se incorpora a `main` junto con la implementación; no se ha creado ni publicado una release mediante este PR.
- **Base del experimento:** `main` @ `c720189d4733176718169cd5871c9b8877edaae2`; código verificado de la prueba: `7e5a861948edc3f5175e897dd0ca57ad37064ed4`. Las modificaciones posteriores de este PR son de documentación, salvo que los checks indiquen lo contrario.
- **Versión:** `02.10`, Title ID `PSVAS1178`; se preservan datos y rutas anteriores.
- **GitHub Actions:** compilación VitaSDK [run 38327869414](https://github.com/VegettoSan/PSVitaAlive/actions/runs/38327869414) **SUCCESS** y validación [run 38327869439](https://github.com/VegettoSan/PSVitaAlive/actions/runs/38327869439) **SUCCESS**.
- **Verificación real reportada por el mantenedor:** tras instalar y navegar con el VPK experimental, confirmó que la fluidez quedó «super bien» y autorizó la integración a `main`. Es una **validación funcional cualitativa**, no una medición instrumental de FPS sostenidos.
- **Pendientes específicos:** comprobar sistemáticamente la integridad de reportes automáticos tras fallos controlados, el marcador de cierre del worker y su funcionamiento con presión sostenida de E/S. No afirmar que se ha demostrado 60 FPS constantes en todos los escenarios.


## Cambios

### Logger asíncrono (diagnostic_logger.cpp)

- `diagnostics::log()` formatea un registro y lo deposita en una **cola fija de 64 mensajes × 1024 B** (aprox. 64 KiB de datos, sin asignación heap por mensaje).
- Un hilo VitaSDK independiente abre `session.log` **una sola vez** y agrupa líneas en escrituras de hasta **4096 B**. Ninguna E/S del registro ocurre en el hilo de UI cuando el trabajador arranca correctamente.
- Semáforo de capacidad 1 para despertar el trabajador, sin sondeo ni espera en el hilo UI.
- Si la cola se llena durante un atasco de `ux0:`, descarta los registros **más antiguos** y conserva los más recientes; al cerrar se escribe un aviso con el número de descartes. Así se evita bloquear la UI por almacenamiento lento.
- Si VitaSDK no permite crear el worker, queda un **fallback sincrónico**, acompañado de marcador de diagnóstico; es la excepción, no el comportamiento previsto.
- `diagnostics::shutdown()` drena la cola, espera a que termine el worker y libera hilo, semáforo y mutex. El cierre normal sigue siendo seguro.
- `ErrorReporter::buildLogBlock()` llama a `diagnostics::flush()` **solo al confeccionar el reporte**: espera como máximo 1 segundo por las líneas existentes y posteriormente lee `session.log` / `install.log`. **No llamar `flush()` desde render/navegación.**

### Trazas descartadas / resumidas

- `PSVITAALIVE_ENABLE_CATALOG_TRACE=0` por defecto: elimina la emisión de `[CATTRACE]` y **evita construir los strings** al compilar con macros de llamadas. Se puede activar explícitamente en otra compilación de investigación.
- Los mensajes `[Perf] slow frame` se sustituyen por un **resumen como máximo una vez cada 10 segundos** cuando hay frames superiores a 25 ms: total >25 ms, total >100 ms, máximo, modo y ocupación de texturas/colas.
- Los logs individuales de decodificación/liberación de textura se reservan para eventos **mayores de 50 ms** (con intervalos de 5–10 s). Se conserva información sobre bloqueos considerables sin inundar `ux0:`.
- Se silencian cancelaciones rutinarias y cargas correctas de imágenes, inventarios de navegación, cambios de foco y liberación habitual de capturas.
- La normalización de imágenes solo emite mensaje `[Perf]` si supera 150 ms.

### Información preservada para reportes

- Errores HTTP/libcurl/TLS, respuestas problemáticas, reintentos/failover Archive.org y MediaFire.
- Descargas e instalaciones: enlaces, errores de ZIP/VPK/PKG, fallos de extracción, fin de operaciones y cancelaciones de usuario.
- Errores del catálogo, fallos de archivos o buckets de imágenes, actualizaciones del cliente, plugins y contexto del dispositivo.
- `session.log` y `install.log` siguen teniendo **las mismas rutas** para el sistema de reportes. `session.log` se reinicia al abrir el cliente; guardar una copia antes de una nueva ejecución.
- El filtrado de reportes de Discord (`ErrorReporter`) **no se elimina ni modifica en estructura**.

## Riesgos deliberadamente no ocultados

- El logger reduce contención por E/S; **no garantiza** 60 FPS, porque `vita2d_load_PNG_file()` y `vita2d_wait_rendering_done()` todavía pueden consumir tiempo en el hilo de interfaz.
- Si la PS Vita se bloquea de forma abrupta pueden faltar mensajes todavía pendientes del worker; se mitiga enviando la cola inmediatamente y haciendo flush al preparar reportes. Un cierre normal drena lo pendiente.
- La cola llena prioriza los mensajes nuevos, no los más graves. Cualquier contador `[Logger] dropped` indica que se debe repetir el ensayo con menos ruido o comprobar almacenamiento.
- No se cambian políticas TLS, integridad de descarga, ZIP, instalación, render, estructura JSON, librerías ni límites GPU.

## Pruebas de regresión en PS Vita real tras integrar

1. Instalar el VPK de esta rama sobre `PSVAS1178` **sin borrar cachés**. Desactivar temporalmente autoupdate al comparar dos builds de la misma versión.
2. Verificar al iniciar: `[System] diagnostic logger initialized (async, bounded, session.log reset)`. No debe aparecer el fallback salvo problema de recursos.
3. Con la misma tarjeta/red y un catálogo Homebrew equivalente, navegar 30–60 segundos por múltiples filas; abrir detalles y capturas; probar ida y vuelta entre pantallas.
4. Comprobar que el marcador `[Perf] 10s summary` aparece solo por intervalos y que no se emiten `[CATTRACE]` ni mensajes `[ImageCache] ready` repetidos.
5. Verificar que las imágenes se descargan y presentan sin errores y que el caché v3 no se reinicia ni vuelve a descargar sin necesidad.
6. Forzar un fallo **seguro y controlado** de enlace de descarga (sin tocar datos del catálogo) y verificar que el reporte incluye la secuencia HTTP/curl relevante.
7. Comprobar cancelar descarga, una instalación normal, una fallida y un ZIP recuperable; confirmar utilidad del `session.log` y del reporte (sin enviar datos personales).
8. Salir con START en estado inactivo, verificar que la última línea es `[System] shared diagnostic logger shutdown`; revisar `[Logger] dropped`.
9. Comparar las pausas que se ven al navegar con el VPK 02.10 actual. Si continúan, instrumentar `vita2d` **en memoria** en otra rama en vez de editar `main` a ciegas.

## Rollback

El VPK `02.10` anterior a PR #30 y el checkpoint `main` @ `c720189d4733176718169cd5871c9b8877edaae2` son referencias previas al logger asíncrono. Volver a compilar ese commit o reinstalar el VPK anterior no exige borrar la caché. No revertir parcialmente la cola sin revisar el acoplamiento `ErrorReporter::buildLogBlock() -> diagnostics::flush()`. La integración de PR #30 fue aprobada tras la prueba funcional del mantenedor; futuras regresiones se investigan por separado.

Arquitectura de catálogo preservada:

```text
apps/ + authors/ + categories/
   -> GitHub Actions
   -> catalog.json + authors.json + categories.json
   -> Cliente PS Vita y Web
```
