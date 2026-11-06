# PSVitaAlive — Índice persistente de directorios y tamaños bajo demanda

**Estado:** implementación de prueba, noviembre de 2026. La compilación automatizada **no equivale** a validar rendimiento, cambios externos ni eliminación en PS Vita real.

## Problema confirmado

El cliente ya dispone de las rutas declaradas en `links[].installed_paths`. El coste grande no está en encontrar esas rutas, sino en recorrer recursivamente todos sus archivos para sumar el tamaño en cada apertura del administrador. Una PS Vita con cientos de directorios y miles de archivos puede tardar mucho. El análisis con tiempo máximo anterior seguía recorriendo archivos automáticamente y marcaba muchos tamaños como parciales.

## Solución

- **Fuente de verdad de las rutas:** únicamente `apps/*.json` → GitHub Actions → `catalog.json` → parser → `installed_paths`. Nunca sustituir ni corregir esas rutas a partir de un fichero local.
- **Índice de descubrimiento local:** `ux0:data/psvitaalive/cache/installed_directory_index.tsv`, formato `PSVA_DIR_INDEX_V1`, tamaño máximo 128 KiB y 1024 registros. Clave `title_id + ruta`. Guarda la ruta encontrada y, si se ha medido antes, el último tamaño completo y una huella de **la fecha de modificación de su raíz**. No guarda listas de archivos ni usa SQLite.
- **Al abrir el administrador:** el trabajador lee ese índice, reconcilia las rutas con el catálogo vigente, verifica únicamente la existencia de cada raíz con `sceIoGetstat` y comprueba colisiones a partir de declaraciones. **No llama al explorador recursivo `sceIoDread` para calcular tamaños.** Sigue siendo necesario comprobar `sceIoGetstat` para no mostrar como instalada una carpeta eliminada por VitaShell.
- **Al abrir la ficha individual:** solo valida los directorios del Title ID seleccionado; mantiene todas las reclamaciones de rutas del resto del catálogo para bloquear colisiones sin visitar sus archivos.
- **Medir tamaño (nuevo botón):** el usuario solicita expresamente la medición del juego elegido. El recorrido recursivo y la barra de progreso se ejecutan solo sobre sus raíces, bajo límite cooperativo de 18 segundos. Un resultado parcial no se guarda como tamaño completo.
- **Segundas consultas:** el índice puede recuperar mediciones anteriores y mostrarlas con `*`, indicando **«Última medición»**, no tamaño exacto actual. Cuando `st_mtime` de la carpeta raíz cambia, se invalida el tamaño para esa ruta. Si no hay tamaño completo disponible, se muestra **«Sin calcular»** en lugar de `0 B`. Si falta medir alguna carpeta, el total muestra **«Parcial»** o «Sin calcular».
- **Al instalar/actualizar Game/Data Files con INSTALL ALL exitosamente:** invalida solo el registro del Title ID. Al eliminar datos externos, también se invalida, incluso si ocurre un fallo parcial. Desinstalar solamente el VPK no vuelve a recorrer Game/Data Files.
- **Cambios externos:** los usuarios pueden alterar archivos con VitaShell, incluso dentro de subcarpetas sin modificar `st_mtime` de la raíz. La huella es solo una señal, **no prueba de que el tamaño actual sea igual**. La interfaz mantiene el aviso de última medición, y el usuario puede volver a medir voluntariamente.
- **Protección:** el índice no se usa como autorización para borrar. Las rutas siempre se extraen del catálogo validado; `InstalledDataManager::removeExternal` revalida las rutas y bloquea coincidencias compartidas, títulos protegidos y destinos ambiguos. `ux0:app/<title_id>` sigue correspondiendo a PromoterUtility.
- **RAM/almacenamiento:** sin SQLite ni inventarios por archivo; el índice se lee solo al abrir el inspector o actualizarlo y se almacena en un fichero pequeño. Guardado mediante fichero temporal y copia de recuperación para tolerar cortes de energía.
- **Compatibilidad:** sin cambios en descarga, extracción, `dr`, catálogos generados, caché de imágenes, otros modos de UX0 ni la acción «Limpiar todo».

## Interfaz

El administrador muestra instantáneamente la lista de aplicaciones con carpetas encontradas y el tamaño anterior si existe. En cada ficha hay **Desinstalar VPK**, **Borrar datos**, **Borrar ambos**, **Medir tamaño** y **Volver**. La acción de medir no requiere confirmación destructiva, mientras que las acciones de borrado la conservan e informan de las rutas y posibles partidas guardadas. La barra de progreso sigue disponible durante el cálculo solicitado.

## Verificaciones antes de publicar como estable

1. Abrir el administrador varias veces, reiniciar la Vita y verificar que la lista conserva rutas y mediciones históricas sin recorrer miles de archivos.
2. Medir una aplicación pequeña; volver a abrir; comprobar `*` y «Última medición».
3. Medir un juego con miles de archivos y cancelar; comprobar resultado parcial, que el índice no guarde un tamaño falsamente definitivo y que se pueda volver a abrir.
4. Modificar/eliminar una raíz con VitaShell, abrir de nuevo y comprobar que la ruta ausente desaparece o que su tamaño anterior se invalida.
5. Instalar Game/Data Files con Install All y verificar la invalidación selectiva sin afectar otras mediciones.
6. Probar carpetas compartidas (`ctr`, `smworld`), rutas ambiguas, protector del Title ID del cliente y confirmaciones sin borrados accidentales.
7. Probar táctil, PS TV con mando y los ocho idiomas.
8. Verificar consumo de RAM, latencia, lectura en consola real y casos de apagado inesperado al guardar el índice.

**Limitación deliberada:** no hay forma genérica de conocer cambios en descendientes de una carpeta sin instrumentar el instalador, disponer de inventario exacto o volver a inspeccionarlos. Mostrar la medición anterior de manera transparente es preferible a afirmar un total actual sin fundamento.
