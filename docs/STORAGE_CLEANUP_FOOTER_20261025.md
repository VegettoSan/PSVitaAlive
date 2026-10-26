# PSVitaAlive — mantenimiento de almacenamiento y administrador UX0

**Estado:** aprobado para integrar en `main` el 26 de octubre de 2026, tras prueba visual y de navegación en PS Vita real por el mantenedor.

**PR de integración:** [#52](https://github.com/VegettoSan/PSVitaAlive/pull/52). **Rama de desarrollo:** `test/storage-cleanup-footer-20261025`.

## 1. Alcance y decisiones aprobadas

Se implementa un administrador de almacenamiento accesible desde el bloque **UX0** del footer, junto con la recuperación de espacio temporal usado por las instalaciones. El objetivo es liberar espacio sin borrar los datos persistentes del cliente ni perder un ZIP que el usuario decidió conservar.

- **Se mantiene el guardado automático de ZIP a los 10 segundos** en la recuperación de errores de extracción de **Install All**, con constancia en el informe final. La integración no elimina ese temporizador ni su comportamiento.
- Tras éxito, fallo o cancelación, los recursos temporales *que pertenezcan al trabajo* deben retirarse al llegar a los puntos seguros de limpieza. Los residuos tras interrupción se inspeccionan al iniciar el cliente.
- **Nunca se eliminan automáticamente los ZIP conservados para recuperación**, aunque procedan de un guardado automático por tiempo de espera. Solo se borran mediante una acción explícita del usuario en la interfaz.
- **Catálogos, imágenes y configuración se protegen.** No existe una opción para eliminarlos desde el administrador. `config.json` no figura en el inventario del menú.
- El inventario solo reconoce rutas y nombres concretos del cliente. No se usa un borrado recursivo indiscriminado de `ux0:data`, `ux0:app`, `ux0:temp/app` o unidades externas.

## 2. Interfaz UX0: estado final aprobado

### Footer

El bloque UX0 ocupa **280 × (FOOTER_H − 4)** píxeles (en vez de 220 píxeles de ancho), manteniendo una zona táctil compartida con su representación visual. Contiene:

1. La etiqueta **UX0**, el espacio libre y la capacidad total (instantánea calculada fuera del renderizado).
2. Una acción de alto contraste **«Administrar espacio ›»** de casi todo el ancho, usando los colores **ACCENT/BG** del tema activo.
3. Una barra inferior que representa la capacidad usada.
4. **Sin el marco de color alrededor de todo el bloque**, retirado después de validar el resultado en hardware real.

Las posiciones del bloque UX0, **Noticias** y **Reportar**, y las correspondientes áreas táctiles usan una constante común `UX0_FOOTER_PANEL_W`; no deben alterarse por separado.

### Ventana «Administrar almacenamiento»

La ventana tiene un resumen superior independiente y una lista desplazable. **Las cuatro secciones aparecen siempre**, incluso con cero archivos:

| Sección | Contenido mostrado | Borrado permitido |
|---|---|---|
| Archivos temporales | Bytes y trabajos temporales conocidos; mensaje de vacío cuando no hay ninguno | Sí, si hay archivos |
| ZIP conservados | Bytes y ZIP de recuperación detectados, incluidos ZIP segmentados | Sí, **solo con confirmación** |
| Catálogos (protegidos) | Tamaño informativo de la caché de catálogos | **No** |
| Imágenes (protegidas) | Tamaño informativo de la caché de imágenes | **No** |

La lista tiene filas separadas visualmente, tipografía mayor que el prototipo inicial, selección resaltada, barra de desplazamiento cuando se necesita y nombres largos recortados **mediante clipping gráfico** sin cortar cadenas UTF-8.

**Entrada y navegación:**
- **Táctil:** pulsar UX0 para abrir, tocar una fila para seleccionarla y confirmar; desplazar verticalmente arrastrando la lista.
- **Controles:** arriba/abajo para navegar; izquierda/derecha salta varias filas; `X` selecciona/solicita confirmación; `O` cancela o cierra; `Triángulo` ofrece «Limpiar todo».
- **PS TV/mando:** la combinación `SELECT + CUADRADO` abre el administrador desde el catálogo, aunque no haya pantalla táctil.
- El foco ajusta el scroll automáticamente para hacer visible la fila seleccionada.

### Confirmaciones de limpieza

- «Limpiar esta sección» solo tiene efecto cuando existen archivos en esa categoría.
- El archivo individual se puede borrar, previa confirmación.
- El botón «Limpiar todo» solo aparece si hay bytes eliminables y **también incluye ZIP conservados**. Requiere confirmación de la acción exacta, no se ejecuta por un único toque accidental.
- Si no existen archivos, se muestra «Sin archivos para limpiar», sin ofrecer borrar catálogos, imágenes o configuración.
- El cliente impide iniciar borrados de este menú mientras hay una instalación activa; tras la operación vuelve a escanear y solicita actualizar el indicador UX0.

Las nuevas cadenas se encuentran en los **ocho paquetes de idiomas**: `en`, `es`, `fr`, `de`, `it`, `pt-BR`, `pt-PT` y `ru`.

## 3. Arquitectura técnica y archivos

### Inventario y limpieza

Implementado en:
- `Client PSVitaAlive/include/storage/storage_maintenance.hpp`
- `Client PSVitaAlive/source/storage/storage_maintenance.cpp`
- `Client PSVitaAlive/include/storage/storage_cleanup_policy.hpp`

`StorageMaintenance::scan()` clasifica trabajos temporales, ZIP de recuperación y, cuando se solicita, los tamaños de las cachés protegidas. `UsageSnapshot` conserva bytes para cada grupo y elementos individuales con sus rutas asociadas. `StorageMaintenance::removeSelected()` compara el elemento solicitado contra **un nuevo inventario**, comprobando la lista exacta de rutas antes de borrar. `cleanupTransient()` se ejecuta al arrancar y omite categorías conservadas.

La enumeración tiene **límites defensivos** (profundidad de directorios, elementos del inventario y visitas de archivos); es intencional y no debe reemplazarse por un borrado recursivo global. Los tamaños pueden ser aproximados o incompletos cuando se superan esos límites.

### Rutas conocidas

| Ruta | Política |
|---|---|
| `ux0:data/psvitaalive/downloads/jobs/job_*/` | Se limpia si es trabajo temporal; un trabajo con indicador `.psva_keep_zip` o un ZIP completado antiguo y ambiguo se clasifica como recuperable |
| `ux0:data/psvitaalive/manual/<id>/*.zip` más `.info.txt` | ZIP guardado; eliminación exclusivamente manual |
| `ux0:data/psvitaalive/manual/<id>/*.zip.001`, `.002`, hasta cuatro partes, más `.info.txt` | ZIP de recuperación segmentado; se gestiona junto con todas sus partes |
| `ux0:data/psvitaalive/tmp/nested_source.vpk`, `nested_vpk_stage`, `work.bin`, `pkg_*.pkg` | Temporales reconocidos por política de nombres |
| `ux0:data/psva_vpk/` | Staging VPK del instalador |
| `ux0:data/PSVA_ZIP_STAGE_<n>_<n>/` y `uma0:data/PSVA_ZIP_STAGE_<n>_<n>/` | Staging de extracción profunda; solo se acepta el patrón numerado propio |
| `ux0:data/psvitaalive/cache/catalog` y `cache/images` | Se contabilizan para información; **nunca** borrables desde este menú |
| `ux0:data/psvitaalive/config.json`, archivos de usuario o destinos instalados | Fuera del inventario de borrado |

La detección de ZIP guardados usa su archivo informativo y comprueba los archivos presentes. Un ZIP de origen incierto no debe clasificarse como temporal por conveniencia.

### Integración en VitaSDK

- `source/main.cpp`: registra la comprobación de actividad del instalador, ejecuta limpieza temporal al inicio y actualiza el snapshot UX0.
- `source/ui/full_catalog_screen.cpp` y `include/ui/full_catalog_screen.hpp`: pinta el bloque del footer, gestiona el modal, la lista, el scroll, la confirmación y el trabajador asíncrono.
- `source/storage/storage_maintenance.cpp`: enumera, cuantifica y elimina únicamente rutas conocidas.
- `source/archive/zip_extractor.cpp`: registra salidas **nuevas** durante la extracción y trata de retirarlas si falla; elimina los directorios temporales de staging que creó.
- `source/installer/install_controller.cpp`: corrige una ruta de fallo de instalación de plugins que dejaba un job de descarga preparado.
- `Client PSVitaAlive/CMakeLists.txt`: incluye el módulo de almacenamiento.
- `tests/test_storage_cleanup_policy.cpp`: verifica nombres/rutas reconocidas y rechaza configuración, cachés y rutas no válidas.
- `.github/workflows/client-build.yml`: ejecuta la prueba de política y comprueba las traducciones.

**Rendimiento:** el espacio libre UX0 ya utiliza un snapshot asíncrono; el nuevo inventario y el borrado también se ejecutan en un hilo de trabajo. **Nunca se debe recorrer el almacenamiento dentro de `drawFooterBar()` ni en cada fotograma**. El tamaño de catálogos/imágenes se lee únicamente al consultar el panel y se omite en la limpieza de arranque.

## 4. Validación e integración

### Comprobaciones automáticas

- **Build del cliente con VitaSDK:** GitHub Actions [run 40342616259](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40342616259) — **success**, commit de cliente `16a8af0ca62b282cf96fb32e16288cd0f071fc33`.
- **Validación/generación del catálogo:** [run 40342616258](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40342616258) — **success**.
- Incluye pruebas de pertenencia de rutas y presencia de claves de traducción.

### Validación manual

El mantenedor instaló el VPK de prueba en PS Vita real, aportó capturas del bloque UX0 y de la pantalla de almacenamiento, solicitó ajustes visuales y confirmó posteriormente que la versión final se veía excelente y **aprobó integrarla en `main`** (26-10-2026).

Esta aprobación confirma principalmente la presentación y navegación observadas. **No implica que se hayan probado todos los casos de fallo de ZIP, corte de energía, archivos segmentados o recuperación de plugins.** Esos escenarios siguen siendo casos de regresión recomendados.

### Lista de regresión para futuras versiones

1. En una Vita con caché pero sin temporales, mostrar siempre las cuatro secciones y evitar controles de borrado sobre cachés.
2. Cambiar entre temas y los ocho idiomas; asegurar contraste y ajuste del texto.
3. Abrir desde pantalla táctil y mando; navegar, deslizar y confirmar/cancelar sin pulsaciones filtradas al catálogo.
4. Completar, cancelar y provocar error en instalaciones VPK/ZIP/PKG y plugins; comprobar limpieza de trabajos temporales.
5. Fallo de extracción con Install All, esperar diez segundos, comprobar ZIP conservado e informe; cerrar y reiniciar sin perderlo.
6. ZIP segmentados en dos o cuatro partes: inventariar, reintentar y eliminar todas las partes **solo** con confirmación explícita.
7. Cerrar o interrumpir la consola en plena operación, reiniciar y verificar limpieza de los staging conocidos.
8. Verificar que `catalog.json`, cachés, `config.json` y directorios instalados no cambian tras «Limpiar todo».

## 5. Límites y medidas preventivas

1. **Sobrescrituras:** si una extracción sobrescribió un archivo ya existente y falló después, no se puede restaurar automáticamente su contenido original. Se retiran únicamente archivos creados con certeza en ese intento; no se borra a ciegas el archivo preexistente.
2. **No borrar `ux0:temp/app` por rutina:** esa ruta puede participar en promociones/recuperación del instalador y no siempre es basura.
3. **Descargas externas o BGDL:** no se consideran propiedad de estos directorios privados; esta función no sustituye el administrador de descargas de PS Vita.
4. **Inventario acotado:** con cientos de trabajos o árboles excepcionalmente grandes podría requerirse una futura revisión de límites; mantener comportamiento conservador.
5. **Seguridad de las rutas:** cualquier ampliación de nombres de archivos temporales debe ir acompañada de pruebas explícitas que impidan borrar datos ajenos.
6. **Integración independiente del catálogo:** este cambio no altera el contrato `apps/ + authors/ + categories/ → GitHub Actions → catalog.json + authors.json + categories.json → cliente + web`. Los catálogos generados continúan sin editarse manualmente.

## 6. Nota de mantenimiento

Si más adelante se agrega un tipo de archivo temporal, se debe actualizar la política de identificación, el inventario, la recuperación tras error y sus pruebas, **antes** de ofrecerlo como eliminable. El principio principal sigue siendo: **limpiar solo lo que PSVitaAlive sabe que creó; conservar siempre lo que pueda ser el único archivo útil del usuario**.
