# PSVitaAlive — plan de identificación, espacio y desinstalación de Game Files / Data Files

**Fecha del acuerdo:** 2026-11-05  
**Estado:** DISEÑO DOCUMENTADO / PENDIENTE DE IMPLEMENTACIÓN. **Ninguna característica de este documento debe describirse como ya implementada.**  
**Alcance:** Homebrew/ports del catálogo `apps/`, cliente PS Vita, generador JSON web y validación/CI. No modifica los catálogos de juegos comerciales.  
**Motivo:** conocer exactamente qué archivos/carpetas deja cada enlace Game Files o Data Files, permitir examinar ocupación y residuos de instalaciones incompletas, y habilitar borrado selectivo o desinstalación completa sin penalizar el arranque, la navegación ni la RAM.

## 1. Decisiones acordadas (mantener el hilo)

1. Conservar la arquitectura oficial: `apps/ + authors/ + categories/` → GitHub Actions → `catalog.json + authors.json + categories.json` → cliente Vita + web. **Nunca editar a mano los JSON generados**.
2. Añadir **un único campo opcional nuevo** en **cada enlace** `Game Files` y `Data Files`: `installed_paths`, array de rutas relativas (archivo o carpeta). No colocar un `folder` a nivel de la app.
3. Seguir usando el `extract_path` ya existente como destino/base de extracción; `installed_paths` identifica los elementos instalados bajo esa base. **No modificar la semántica del extractor**.
4. Reutilizar el `size` **existente de cada enlace** para estimar su tamaño instalado. El mantenedor utiliza ZIP en modo **STORE/sin compresión** para Game/Data Files del catálogo: sus bytes descargados suelen aproximarse a la suma de archivos extraídos. **No introducir `installed_size` en la primera versión**.
5. Game Files y Data Files **pueden compartir la misma carpeta**; los Data Files a menudo se extraen dentro de la carpeta de Game Files. Detectar rutas idénticas y relaciones padre/hijo **antes de medir o borrar**. No contabilizar ni borrar dos veces una ruta física.
6. Para una carpeta compartida con propiedad de archivos no diferenciable, **no prometer borrado independiente de cada enlace**: tratar el directorio como unidad compartida; borrado conjunto con aviso y confirmación, o solo informativo si hay dudas.
7. Detectar la existencia mediante metadatos del sistema de archivos (p. ej. `sceIoGetstat`), **no por la extensión del nombre**: una carpeta puede contener puntos y un archivo puede carecer de extensión.
8. Todos los escaneos detallados serán **bajo demanda y asíncronos** al abrir «Administrar espacio», «Datos instalados» o «Desinstalar»; nunca durante arranque, filtrado de tarjetas, pintado, ni en cada frame. No mantener inventarios de todos los archivos en RAM.
9. Los datos dentro de `ux0:app/<title_id>` pertenecen al paquete de la app: gestionar su baja con el mecanismo de desinstalación de paquetes Vita, **nunca mediante borrado recursivo directo de esa carpeta**. Para datos externos seguros en `ux0:data/`, permitir selección individual o conjunta.
10. Mantener intactas las cuatro categorías actuales del administrador UX0 (temporales, ZIP conservados, catálogos protegidos, imágenes protegidas). Añadir una quinta sección separada de **«Aplicaciones y datos instalados»**. **No incluir datos de juegos en «Limpiar todo»**.
11. El generador web mostrará `installed_paths` solo para Game Files / Data Files, permitirá múltiples entradas, importará/exportará sin perder datos y validará rutas. Los JSON antiguos, sin campo, seguirán funcionando: se mostrará «ruta no registrada», sin suposiciones destructivas.
12. Si una extracción tiene éxito en el futuro, registrar opcionalmente un **recibo local** pequeño (fuera del JSON del catálogo) por enlace con sus archivos reales. Eso mejorará desinstalación selectiva y diagnóstico de integridad, pero es **fase posterior**, no requisito inicial.
13. Diseñar y probar primero en rama de desarrollo. **No tocar código funcional de `main` hasta validar fase por fase.**

## 2. Situación vigente, antes de implementar

- `apps/*.json` ya puede incluir `links[].size` y `links[].extract_path` para Game Files / Data Files; el generador web ofrece ambos campos.
- `Client PSVitaAlive/include/ui/ui_types.hpp`: `CatalogLink` ya conserva `extractPath` y `size`; el lector está en `source/catalog/catalog_parser.cpp`.
- `StorageMaintenance::scan()` calcula tamaños, clasifica temporales/ZIP conservados y protege cachés. `StorageMaintenance::removeSelected()` revalida su inventario propio antes del borrado. **No ampliar sus permisos actuales a carpetas arbitrarias**.
- `FullCatalogScreen` tiene el modal de almacenamiento y el trabajo asíncrono; aprovechar su UI, navegación, temas e idiomas existentes.
- El instalador de ZIP ya maneja `Game Files`/`Data Files` mediante `extract_path`. Un ZIP con destino `ux0:app/` puede traer dentro el directorio `BLPM12345/` (caso Blasphemous): **no duplicar el Title ID** al configurar destino.
- Existe un precedente muy específico de `scePromoterUtilityDeletePkg()` para borrar la burbuja temporal del actualizador (`UpdateChecker::cleanupUpdaterBubble`). Su utilización para desinstalar Homebrew normales exige investigación y prueba en Vita real; no se considera aún implementada.
- Documentación relacionada: [administrador UX0](STORAGE_CLEANUP_FOOTER_20261025.md), [enrutamiento Game/Data ZIP](GAME_DATA_ZIP_INSTALL_FIX_20261019.md), [preflight Install All](INSTALL_ALL_SPACE_PREFLIGHT_20261025.md), [recuperación ZIP](SAVED_ZIP_SEGMENTS_20261025.md).

## 3. Contrato JSON propuesto (NO implementado)

Los siguientes son **ejemplos de diseño**, no registros reales ya modificados:

### 3.1 Carpeta completa de Game Files

~~~json
{
  "type": "Game Files",
  "name": "Game Files",
  "url": "https://example.org/game-files.zip",
  "size": 1500000000,
  "extract_path": "ux0:data/",
  "installed_paths": ["MiJuego"]
}
~~~

Ruta resuelta: `ux0:data/MiJuego`.

### 3.2 Data Files anidados dentro de Game Files

~~~json
{
  "type": "Data Files",
  "name": "Data Files",
  "url": "https://example.org/data-files.zip",
  "size": 500000000,
  "extract_path": "ux0:data/",
  "installed_paths": ["MiJuego/extra.dat", "MiJuego/voices"]
}
~~~

El enlace de Game Files podría declarar `["MiJuego"]`: detectaremos que `extra.dat` y `voices/` **están contenidos en su carpeta**. Nunca permitir borrar `MiJuego` creyendo que ello conserva los Data Files.

### 3.3 Dos enlaces que declaran exactamente la misma carpeta

~~~json
{
  "type": "Data Files",
  "name": "Data Files",
  "url": "https://example.org/extra.zip",
  "size": 120000000,
  "extract_path": "ux0:data/",
  "installed_paths": ["MiJuego"]
}
~~~

Si Game Files también declara `["MiJuego"]`, **no existe prueba de pertenencia por enlace**. La UI presentará una única ubicación compartida, con acción conjunta explícita o bloqueada si hay riesgo. No asignar arbitrariamente archivos a uno de los dos paquetes.

### 3.4 Game Files en la carpeta de la aplicación

~~~json
{
  "type": "Game Files",
  "name": "Game Files",
  "url": "https://example.org/inside-app.zip",
  "size": 816840704,
  "extract_path": "ux0:app/",
  "installed_paths": ["BLPM12345"]
}
~~~

Ruta informativa: `ux0:app/BLPM12345`. Para desinstalar, usar el **flujo seguro de paquetes**, no borrar manualmente su árbol. No asumir que ese `size` representa el tamaño total del VPK + datos.

### 3.5 Reglas del campo

- Array **opcional** de 1 o más cadenas no vacías, sin duplicados. Omitirlo si todavía se desconoce la estructura real; no adivinar nombres.
- Cada entrada es **relativa** a `extract_path`: sin `ux0:`, `uma0:`, barra inicial, `..`, `.`, barras invertidas, caracteres de control, ni componentes vacíos. Admitir subrutas seguras como `MiJuego/game.dat`.
- Respetar nombres y mayúsculas de los archivos reales; no derivar si es archivo/directorio por extensión. Resolver y comprobar existencia/tipo al consultar.
- Establecer límites de cantidad y longitud de rutas en validador web, Python y cliente. Las rutas de **borrado** solo serán elegibles en raíces expresamente permitidas y con ownership validado; los demás casos pueden ser informativos.
- La función debe aceptar JSON históricos sin `installed_paths` sin romper instalación, visualización, generador o filtro.
- Si falta `extract_path` y el usuario selecciona un destino manual, **no reconstruir una ruta potencialmente errónea**: estado desconocido salvo que exista un recibo local con destino efectivo.
- Si cambia la ruta de extracción o el registro del catálogo, una ruta previamente medida debe revalidarse antes de operar.

## 4. Identificación de rutas compartidas, propiedad y contabilidad

- Construir durante la consulta una lista pequeña de **rutas canónicas normalizadas** (solo de enlaces del Homebrew seleccionado o del subconjunto que se examine).
- Para cada ruta: marcar tipo real, existencia, enlace(s) que la declaran y relaciones **igual / ancestro / descendiente** por componentes de ruta, **no por simple prefijo textual** (`MiJuego` no equivale a `MiJuego2`).
- Si dos enlaces apuntan al mismo directorio, calcular su uso **una sola vez**. Si una ruta es descendiente de otra, no volver a sumar sus bytes. Al eliminar un padre, identificar todas las rutas hijas afectadas antes de mostrar confirmación.
- Si un enlace designa únicamente un archivo y no tiene solapamiento, podrá borrarse ese archivo individualmente. Si hay una subcarpeta expresamente designada como exclusiva, podrá borrarse solo tras comprobar que no afecta a otros enlaces/aplicaciones.
- Considerar también colisiones **entre diferentes Homebrew**, no solo entre Game Files y Data Files de una misma app. Si varias apps referencian la misma ruta o una relación padre/hijo, el borrado automático de esa ubicación queda bloqueado hasta revisión/solución explícita. El catálogo es metadato, no prueba definitiva de propiedad.
- En ausencia de inventario por archivo, **no identificar ni borrar únicamente «la parte de Data Files» de una carpeta compartida**. La opción segura es eliminación conjunta de la carpeta si está validada y no es compartida con otros juegos, o solo informar.
- Evitar sumar el `size` de descarga al **uso real del disco**: usar tamaño medido de rutas físicas únicas. El `size` por enlace sirve solo como referencia aproximada de tamaño esperado.
- Si dos ZIP escriben archivos idénticos o se sobrescriben, la suma de sus `size` no equivale necesariamente al tamaño instalado; presentar la comparación de integridad como **indeterminada**.

## 5. Tamaños, estados e integridad (estimación, no certificación)

**Situación del catálogo:** por convención del mantenedor, los ZIP Game/Data se publican **sin comprimir (STORE)**. El `links[].size` existente se usa como estimación próxima al contenido extraído (diferencias por cabeceras/entradas ZIP, y otros detalles). No modificar ni debilitar el control actual de espacio en instalaciones: estas estimaciones son para **administración/diagnóstico**, no sustituyen medidas y guardias del extractor.

Estados propuestos:

| Estado | Evidencia | Qué debe decir la UI |
|---|---|---|
| Sin ruta registrada | No hay `installed_paths` válido | Datos no identificados; no borrar |
| No encontrado | La ruta declarada no existe | Sin datos detectados en esa ruta |
| Detectado | La ruta existe, tamaño desconocido o no comparable | Datos detectados; integridad desconocida |
| Posiblemente incompleto | Tamaño medido muy inferior al esperado y la comparación es válida | Advertencia: podría estar incompleto |
| Tamaño compatible | Tamaño medido cercano al esperado, comparación válida | Tamaño compatible; **no garantiza integridad** |
| Instalación registrada | Recibo local escrito después de extracción exitosa, fase futura | El cliente registró una extracción exitosa; cambios posteriores posibles |
| Error/parcial | No puede completarse el recorrido o excede límites | Tamaño parcial/desconocido; nunca «0 bytes = completo» |

- Margen **tentativo para ensayos**, pendiente de pruebas con ZIP reales: diferencia del orden del 5 % o 32 MiB (el mayor), **no regla final de integridad**. Ajustar para paquetes pequeños, errores de medida y ZIP con sobrescrituras. No marcar «incompleto» por comparación si el caso es compartido/ambiguo.
- Solo sumar tamaños `size` de varios enlaces para comparar contra **una carpeta compartida** cuando se pueda confirmar que sus contenidos no se pisan. Si no, estado «no determinable».
- Sumas en `uint64_t`, con protección frente a overflow y resultados truncados por límites de recorrido.
- Nunca afirmar que una extracción quedó completa basándose únicamente en bytes; tamaño igual no implica archivos correctos.

## 6. UI/UX deseada

**Ficha del Homebrew:** nuevo apartado «Datos instalados» con filas por ruta detectada: Game Files/Data Files, ubicación, tamaño real, estado estimado, procedencia del enlace y aviso de carpeta compartida. Botón «Desinstalar» accesible para apps instaladas; opciones separadas cuando la eliminación es segura: «Solo aplicación», «Solo datos externos», «Aplicación y datos externos». Para archivos/carpetas exclusivos, selección individual.

**Administrador UX0:** quinta sección «Aplicaciones y datos instalados» agrupada por Homebrew y elementos externos. Las cuatro secciones previas y sus políticas permanecen intactas. Los datos de Homebrew **no** entran en «Limpiar todo» de temporales. Si analizar todos los registros sería costoso, mostrar primero lista/candidatos de rutas declaradas y medir progresivamente en segundo plano solo al abrir el panel.

**`ux0:app/<title_id>`:** mostrar como parte de la instalación del Homebrew; no permitir borrado independiente por el nuevo limpiador de carpetas. Verificar Title ID y operación de desinstalación. Evitar afirmar que el paquete se desinstaló hasta confirmar el resultado.

**Confirmación destructiva:** enumerar rutas afectadas, otros enlaces afectados, bytes estimados liberables, posible pérdida de configuraciones/partidas, y confirmar explícitamente. No permitir acciones si hay un job de instalación/extracción activo. Si la ruta ya no coincide con la vista, actualizar y solicitar nueva confirmación.

**Locales/temas:** usar los componentes visuales existentes, navegación Vita/PS TV/táctil, colores del tema y el sistema actual de traducciones; añadir cadenas a todos los idiomas mantenidos. Nunca bloquear la UI con un escaneo sin progreso.

## 7. Seguridad: separación de responsabilidades

Proponer un módulo específico, p. ej. `InstalledDataManager` (nombre sujeto a revisión), con:

1. **Inventario de metadatos**: rutas efectivas, pertenencia, tamaño, estado de escaneo.
2. **Política pura y testeable de rutas/ownership**: validación de raíces, componentes, cruces y solapamientos.
3. **Operaciones selectivas**: revalidar en disco justo antes de borrar, confirmar y gestionar errores parciales.
4. **Integración con desinstalación del paquete**: API oficial de Vita para apps, no `eraseTree(ux0:app/...)`.
5. **Presentación**: la UI usa snapshots del worker, sin I/O dentro de `draw`.

**Nunca** permitir que `installed_paths` habilite por sí solo el borrado de raíces como `ux0:data/`, `ux0:app/`, `ux0:temp/`, `ur0:`, `uma0:` o directorios del cliente, cachés, configuración, sistema u otras apps. Prohibir rutas padre, enlaces/alias no resueltos y objetivos que escapen del árbol aprobado. Añadir límites defensivos de profundidad y cantidad de entradas; al superarlos, reportar parcial y bloquear cualquier borrado inseguro.

**Diferencia fundamental:** `StorageMaintenance` gestiona únicamente los temporales/ZIP propios, con `cleanupTransient()` en arranque. **No añadir datos instalados a `cleanupTransient()` ni a `removeSelected()` sin política independiente estricta**. La extensión de UI puede compartir componentes, no la autoridad de borrado.

## 8. Rendimiento PS Vita

- En carga de catálogo, parsear solo metadatos de rutas **existentes**. Evitar strings duplicados o una copia completa de inventarios de archivos por Homebrew; medir el incremento real de RAM del catálogo. Considerar representación compacta y parsing bajo demanda si fuese necesario.
- No acceder al sistema de archivos para estas rutas en arranque, tarjetas, búsqueda, filtros, animaciones o render.
- Crear un worker de baja prioridad cuando el usuario abre la consulta. Recorrer con un buffer fijo, acumulador `uint64_t`, profundidad/entradas limitadas, y liberar resultados al cerrar o invalidar. Cancelación cooperativa y no competir con descargas/instalaciones.
- Usar `sceIoGetstat` o las primitivas ya encapsuladas por `StorageManager`; enumeración incremental sin cargar el contenido del archivo.
- Evitar escanear todo `ux0:data/` buscando carpetas por nombre; inspeccionar **solo rutas declaradas** o registradas en recibos. Para listado global, procesamiento progresivo y sin duplicados.
- No hacer escrituras del registro de instalación durante navegación; recibos futuros solo al finalizar correctamente una extracción y con escritura segura/atómica.

## 9. Generador web, generación y validación

Modificar **en fase 1**:

- `web/tools/app-generator/app-generator.js`: campo visible solo en Game Files/Data Files, entrada múltiple (una ruta relativa por línea), conversión a `installed_paths` array; import/export que preserve el dato. Al cambiar tipos, no filtrar campos incorrectamente a otros enlaces.
- `web/tools/app-generator/index.html` + CSS/README: instrucciones y ejemplos explícitos para carpeta, archivo sin extensión, subruta, carpeta compartida y `ux0:app/`.
- `scripts/validate_catalog.py`: validar `installed_paths` solo para tipos permitidos, array/cadenas, cantidad/longitud, normalización y rutas no peligrosas; rechazar bases/raíces peligrosas; advertir conflictos conocidos.
- `scripts/generate_catalog.py` y fases de normalización/merge: **preservar** el campo del JSON canónico al generar, sin inferir datos externos no verificados ni editar fuentes externas; verificar que los campos nuevos sobreviven al pipeline.
- Pruebas web de generador e importación, pruebas Python de compatibilidad, validación de JSON antiguos y casos de rutas maliciosas. No tocar directamente `catalog.json`.

Se debe permitir comenzar la migración de los muchos Homebrew actuales **gradualmente**. Futuro ayudante para PC: inspeccionar las entradas del ZIP sin modificarlo y sugerir rutas raíz; marcar ambigüedades/duplicados y requerir revisión humana antes de actualizar `apps/*.json`.

## 10. Fases con puerta de validación

| Fase | Entregable | Criterio para avanzar |
|---|---|---|
| 0 (actual) | Este documento en `main` | Diseño acordado, sin tocar código |
| 1 | Campo JSON, generador web, validators, tests | Import/export y catálogos anteriores compatibles |
| 2 | Parser cliente + modelo de rutas | Carga estable; medición RAM y tiempo, sin I/O nuevo de arranque |
| 3 | Inventario bajo demanda, cálculo físico único y detección de solapamientos | Pruebas con carpetas, archivos, rutas ausentes, padres/hijos, errores y cortes |
| 4 | Ficha de Homebrew + quinta sección UX0 | UI fluida, localización, navegación Vita/PS TV, snapshots correctos |
| 5 | Eliminación segura de datos externos y desinstalación de paquete | Confirmación, propiedad, reversión de UI ante error; prueba en Vita real |
| 6 (opcional) | Recibos locales de extracción exitosa | Integridad, persistencia, fallos de energía y archivos compartidos probados |
| 7 (opcional) | Herramienta de sugerencia de rutas para ZIP existentes | Sugerencias revisadas, ningún cambio automático de catálogo |

**No avanzar de fase sin aprobar pruebas de la anterior.** Nada de esta planificación autoriza borrados en segundo plano, limpieza automática de Game/Data Files, ni reescritura global de JSON.

## 11. Matriz mínima de regresión

1. App histórica sin `installed_paths`, instalación VPK/ZIP y render intactos.
2. Carpeta `ux0:data/MiJuego`, archivo `ux0:data/game.dat`, carpeta con punto y archivo sin extensión.
3. Game Files `MiJuego` y Data Files `MiJuego/extra.dat`: tamaño no duplicado; borrar padre reconoce que afecta al hijo.
4. Game/Data con exactamente la misma carpeta: no ofrecer borrado separado engañoso; un solo conteo.
5. Datos de dos Homebrew apuntando a misma ruta: bloqueo de borrado peligroso.
6. Game Files dentro de `ux0:app/BLPM12345`: solo mecanismo de desinstalación de app, sin limpieza recursiva externa.
7. Ruta inexistente, sin permiso, árbol muy profundo, demasiados archivos, interrupción de análisis y `uint64_t` para archivos grandes.
8. `..`, `/`, `ux0:data/`, `ux0:app/`, otros montajes, destino con caracteres/partes inválidas: rechazo.
9. No afectar catálogos, imágenes, ajustes, temporales, ZIP conservados ni botón «Limpiar todo».
10. Prueba de instalación activa/cancelada, ZIP fallido o incompleto: nunca borrar archivos ajenos ni afirmar integridad absoluta.
11. Memoria y tiempo: comparar antes/después de la fase 2 y rendimiento al abrir/cerrar el modal de la fase 4 en Vita real.
12. Pruebas web de Create/Edit/Import/Export y CI, sin necesidad de servidor propio ni cambios en catálogos generados.

## 12. Condiciones de aceptación funcional final

- El usuario puede saber **qué app/port** está asociada con cada carpeta o archivo de datos **conocido**, ver uso real, y qué paquetes lo referencian.
- Puede seleccionar borrado individual **solo si es seguro**; en datos compartidos se explica el alcance real de borrar la carpeta completa.
- Al desinstalar, distingue aplicación/VPK frente a datos externos y evita borrar dos veces.
- Las estimaciones de integridad son transparentes y no reemplazan checksums ni recibos verificados.
- Sin rastreo de carpetas en el arranque ni render, sin ralentización notable de navegación y con RAM acotada.
- Web + cliente + CI siguen basados en `apps/` → catálogos generados, sin nuevos servicios de pago.

---

**Recordatorio para quien implemente:** este archivo es el acuerdo de diseño (5 de noviembre de 2026), **no un changelog de funcionalidad entregada**. Antes de programar, contrastar estas referencias con el código más reciente; documentar diferencias y validar primero la fase 1.
