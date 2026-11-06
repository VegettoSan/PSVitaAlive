# PSVitaAlive — Mejoras del análisis de Game/Data Files

**Fecha:** 5 de noviembre de 2026  
**Motivo:** informe en PS Vita de más de cinco minutos en «Procesando» al abrir el administrador de datos.

## Diagnóstico en la implementación anterior

1. `InstalledDataManager::scan` recorría **todas** las aplicaciones del catálogo incluso cuando el usuario elegía «Desinstalar» desde una única ficha.
2. Cada archivo visitado requería una llamada adicional a `sceIoGetstat()`, además de la lectura de directorio `sceIoDread()`. En instalaciones con miles de archivos, esto provoca demoras evitables.
3. El cuadro solo mostraba «Procesando», sin contador de carpetas, archivos, porcentaje ni opción de cancelar. No existía un límite de duración total.
4. Al concluir una operación de borrado/desinstalación también volvía a recorrer todas las aplicaciones.

## Comportamiento corregido

### Administrador UX0

La sección **Game/Data Files de Homebrew instalados** está **primera** en el menú de almacenamiento; las secciones existentes de temporales, ZIP, catálogo e imágenes mantienen sus funciones y «Limpiar todo» no toca Game/Data Files.

Al abrirla, se lanza una tarea separada de lectura del sistema de archivos. La ventana muestra una barra con el **porcentaje de aplicaciones completadas (0–100)**, `X / Y` aplicaciones, número de **raíces declaradas detectadas**, número de **directorios visitados** y número de **archivos revisados**. Una marca animada indica actividad cuando se encuentra un único directorio enorme y el porcentaje por aplicaciones permanece temporalmente inmóvil. Esto **no es una estimación de segundos restantes**, porque el número de archivos por juego es desconocido.

Se sustituyen consultas redundantes `sceIoGetstat` para los archivos del interior de cada carpeta por los metadatos `SceIoDirent.d_stat` devueltos con `sceIoDread`, con fallback a `sceIoGetstat` para entradas sin metadatos suficientes. No se cachean listas de archivos individuales en RAM.

**Límite:** el análisis general dispone de un presupuesto de **22 segundos** de recorrido; si una carpeta tarda demasiado, se cierra de forma cooperativa, el tamaño sin medir se muestra como **pendiente** y el total se marca como **parcial**. No debe afirmarse que ese total representa toda la instalación. Algunas llamadas individuales del dispositivo pueden bloquear más de lo previsto: el presupuesto no es un temporizador que interrumpa la llamada de sistema.

**Cancelación:** pulsar **Círculo** durante una inspección sin operaciones destructivas solicita la cancelación; el trabajador finaliza de forma segura, marca los resultados parciales y permite regresar. Nunca se cancela una desinstalación o eliminación de carpetas a mitad de ejecución para evitar estados inconsistentes.

### Ficha individual de un Homebrew

Al entrar por **Desinstalar**, el cliente identifica la aplicación mediante `title_id`, revisa **solo sus rutas externas declaradas** y comprueba de forma barata sus posibles colisiones contra los metadatos de las demás aplicaciones, **sin recorrer sus archivos**. El presupuesto de inspección del juego seleccionado es **7 segundos**. Los Game Files de un juego enorme podrían seguir necesitando más lectura, pero no bloquearán la interfaz durante varios minutos; el resultado será parcial/protegido si se supera el límite.

Tras desinstalar **solo VPK** no se vuelve a medir el contenido de Game/Data Files, pues no ha cambiado. Tras eliminar datos, solo se revalida la aplicación afectada y se recalcula el total conocido sin doble conteo. Si la inspección general terminó parcialmente y el usuario abre una aplicación con tamaño pendiente, se intenta una inspección individual adicional (7 segundos) para esa aplicación.

### Precauciones de borrado

- Se conservan las validaciones de `installed_paths`, el rechazo de rutas peligrosas y el bloqueo ante carpetas compartidas por diferentes Title ID.
- Las raíces de **otras** aplicaciones se emplean para detectar colisiones **sin inspeccionar sus archivos** cuando la consulta es individual.
- Que el análisis global termine parcialmente **no impide gestionar otra aplicación completamente inspeccionada**. Solo se permite borrar los datos de una aplicación si **sus propias rutas** se midieron íntegramente, no están compartidas y pasan de nuevo las validaciones de seguridad.
- Un análisis individual parcial **nunca habilita el borrado de esos datos**. Se mantiene habilitada, independientemente, la opción de desinstalar solo VPK mediante PromoterUtility si el paquete está instalado.
- No se calculan los bytes descomprimidos de `ux0:app/<title_id>/`, no se borran recursivamente esas carpetas y `dr` sigue sin producir avisos de actualización por sí mismo.
- Las confirmaciones siguen identificando los destinos externos y advierten sobre partidas guardadas y copias de seguridad en VitaShell.

## Comprobaciones

- Probar conteos, porcentajes, límites, cancelación y protección de rutas en pruebas puras y compilación VitaSDK.
- Comprobar el VPK en una PS Vita física con pocos y muchos Game/Data Files: tiempo de inicio del administrador, contador de archivos en movimiento, actualización de la barra, resultados completos/parciales y retorno tras cancelar.
- Abrir la ficha de un juego con datos grandes y verificar que **no** se vuelvan a inspeccionar carpetas de todo el catálogo.
- Comprobar carpetas compartidas (`ctr`, `smworld`) y rutas sin `installed_paths` para garantizar que siguen bloqueadas.
- No probar borrado con partidas valiosas. Guardarlas previamente con VitaShell.

Estas comprobaciones en dispositivo son imprescindibles antes de considerar terminada la validación de rendimiento o desinstalación. No se garantiza una duración exacta si el firmware o la unidad de almacenamiento bloquean una lectura individual.
