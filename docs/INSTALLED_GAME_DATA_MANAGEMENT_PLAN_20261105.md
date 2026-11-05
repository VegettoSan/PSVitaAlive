# PSVitaAlive — plan de identificación, espacio y desinstalación de Game Files / Data Files

**Fecha del acuerdo:** 2026-11-05  
**Estado:** DISEÑO DOCUMENTADO / PENDIENTE DE IMPLEMENTACIÓN. **Ninguna característica de este documento debe describirse como ya implementada.**  
**Alcance:** Homebrew/ports del catálogo `apps/`, cliente PS Vita, generador JSON web y validación/CI. No modifica los catálogos de juegos comerciales.  
**Motivo:** conocer exactamente qué archivos/carpetas deja cada enlace Game Files o Data Files, permitir examinar ocupación y residuos de instalaciones incompletas, y habilitar borrado selectivo o desinstalación completa sin penalizar el arranque, la navegación ni la RAM.

## 1. Decisiones acordadas (mantener el hilo)

1. Conservar la arquitectura oficial: `apps/ + authors/ + categories/` → GitHub Actions → `catalog.json + authors.json + categories.json` → cliente Vita + web. **Nunca editar a mano los JSON generados**.
2. Añadir **un único campo nuevo** por enlace `Game Files` / `Data Files`: `installed_paths` (array de rutas relativas, archivo o carpeta). **Game Files:** obligatorio para nuevas entradas si su `extract_path` no es exactamente `ux0:app/`; cuando extrae directamente en `ux0:app/`, se omite y se considera parte de la app (`title_id`). **Data Files:** siempre opcional; ausente o `[]` indica que comparte la ubicación identificada por Game Files. No colocar un `folder` a nivel de la app.
3. Seguir usando `extract_path` como destino/base real de extracción; `installed_paths` identifica elementos instalados bajo esa base. La herencia de Data Files **es solo para detección/contabilidad, nunca cambia dónde se extrae el ZIP**. Si destino y ruta heredada no son compatibles, no asumir ubicación: avisar y solicitar revisión. **No modificar la semántica del extractor**.
4. Reutilizar el `size` **existente de cada enlace** para estimar su tamaño instalado. El mantenedor utiliza ZIP en modo **STORE/sin compresión** para Game/Data Files del catálogo: sus bytes descargados suelen aproximarse a la suma de archivos extraídos. **No introducir `installed_size` en la primera versión**.
5. Game Files y Data Files **pueden compartir la misma carpeta**; los Data Files a menudo se extraen dentro de la carpeta de Game Files. Detectar rutas idénticas y relaciones padre/hijo **antes de medir o borrar**. No contabilizar ni borrar dos veces una ruta física.
6. Para una carpeta compartida con propiedad de archivos no diferenciable, **no prometer borrado independiente de cada enlace**: tratar el directorio como unidad compartida; borrado conjunto con aviso y confirmación, o solo informativo si hay dudas.
7. Detectar la existencia mediante metadatos del sistema de archivos (p. ej. `sceIoGetstat`), **no por la extensión del nombre**: una carpeta puede contener puntos y un archivo puede carecer de extensión.
8. Todos los escaneos detallados serán **bajo demanda y asíncronos** al abrir «Administrar espacio», «Datos instalados» o «Desinstalar»; nunca durante arranque, filtrado de tarjetas, pintado, ni en cada frame. No mantener inventarios de todos los archivos en RAM.
9. Los datos dentro de `ux0:app/<title_id>` pertenecen al paquete de la app: gestionar su baja con el mecanismo de desinstalación de paquetes Vita, **nunca mediante borrado recursivo directo de esa carpeta**. Para datos externos seguros en `ux0:data/`, permitir selección individual o conjunta.
10. Mantener intactas las cuatro categorías actuales del administrador UX0 (temporales, ZIP conservados, catálogos protegidos, imágenes protegidas). Añadir una quinta sección separada de **«Aplicaciones y datos instalados»**. **No incluir datos de juegos en «Limpiar todo»**.
11. El generador web mostrará `installed_paths` solo en Game Files / Data Files; **obligatorio en Game Files con extracción externa**, **no exigido en Game Files con `extract_path: "ux0:app/"`** y **opcional en Data Files** (vacío = hereda la ubicación de Game Files). Al importar, editar o exportar, preservar y validar esa semántica. Los JSON históricos incompletos deben seguir siendo legibles durante una migración gradual, sin inventar rutas ni autorizar borrados.
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

**Aclaración del mantenedor (2026-11-05):** la obligatoriedad de `installed_paths` **depende del tipo de enlace y del destino**. Los siguientes ejemplos son de diseño; **no se ha cambiado ningún JSON de aplicaciones ni el generador web**.

### 3.1 Tabla de obligatoriedad — regla principal

| Enlace | `extract_path` | `installed_paths` | Semántica |
|---|---|---|---|
| Game Files | `ux0:data/` u otro destino externo permitido | **Obligatorio** para nuevas entradas | Identifica carpeta(s) o archivo(s) del Game Files |
| Game Files | **Exactamente** `ux0:app/` (normalizar barra final) | **No requerido** (preferible omitir) | Datos dentro de `ux0:app/<title_id>`, gestionados con la desinstalación de la aplicación |
| Data Files | Cualquier destino soportado | **Opcional** | Si falta o es `[]`, hereda la ubicación de Game Files de la **misma aplicación**, siempre que sea inequívoca y compatible con la extracción |
| Data Files | Cualquier destino soportado | Opcional, pero **explícito cuando los datos están en ruta distinta** | Permite mostrar/borrar archivos propios si no se solapan |
| Data Files sin Game Files asociable | Cualquier destino | Opcional en esquema, **recomendado completarlo** | Si está vacío, no hay ubicación que heredar: estado «no identificable», sin borrado automático |

**Importante:** «obligatorio para Game Files» corresponde a **nuevos registros que se generen o editen** en la fase de implementación. El catálogo histórico puede contener muchos enlaces sin ese campo: no introducir una validación global que rompa CI al habilitar la característica. Planificar auditoría y migración progresiva, con advertencias para registros antiguos y política de transición explícita.

### 3.2 Game Files externo: ubicación explícita obligatoria

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

Ruta resuelta: `ux0:data/MiJuego`. También puede contener archivos sueltos, varias rutas o rutas anidadas.

### 3.3 Data Files dentro de Game Files: **sin nuevo campo**

~~~json
{
  "type": "Data Files",
  "name": "Data Files",
  "url": "https://example.org/data-files.zip",
  "size": 500000000,
  "extract_path": "ux0:data/"
}
~~~

Es equivalente, **solo para ubicación**, a `"installed_paths": []`. La ubicación detectada/heredada será `ux0:data/MiJuego`, porque Game Files del mismo Homebrew declara `["MiJuego"]`.

- **No** significa que todos los archivos de `MiJuego` pertenezcan exclusivamente a Data Files.
- Si el usuario pide borrar **solo Data Files**, esta herencia **no basta** para saber qué archivos borrar. Bloquear la eliminación selectiva y explicar que están compartidos con Game Files; ofrecer, si es seguro, borrado completo de la carpeta compartida.
- El `size` del enlace Data Files no determina qué archivos son suyos. Los tamaños de uso real no deben duplicarse.

También se admite `"installed_paths": []` en Data Files importados; el generador **debe omitir el campo** en exportaciones nuevas cuando está vacío para mantener JSON limpios.

### 3.4 Data Files con ubicación propia: campo opcional explícito

~~~json
{
  "type": "Data Files",
  "name": "Data Files",
  "url": "https://example.org/extra.zip",
  "size": 120000000,
  "extract_path": "ux0:data/",
  "installed_paths": ["MiJuego/extra.dat", "MiJuego/voices"]
}
~~~

Como esas rutas están dentro de la carpeta Game Files, una eliminación del directorio padre afectaría a Data Files. Si se demuestra que las rutas explícitas son exclusivas, podrá ofrecerse borrado individual, nunca suponiendo propiedad por el nombre.

### 3.5 Game Files dentro de `ux0:app/`: no requiere campo

~~~json
{
  "type": "Game Files",
  "name": "Game Files",
  "url": "https://example.org/inside-app.zip",
  "size": 816840704,
  "extract_path": "ux0:app/"
}
~~~

Si la aplicación tiene `title_id: "BLPM12345"`, el destino informativo esperado es `ux0:app/BLPM12345` **sin configurar una segunda carpeta en el JSON**. Antes de mostrar que existe, verificarla; el ZIP podría tener una estructura excepcional. **No agregar `installed_paths` por defecto ni alterar el lugar de extracción**. Para desinstalar, usar el mecanismo de paquetes Vita, no borrar recursivamente `ux0:app`. Esta excepción aplica al destino raíz `ux0:app/`, no a cualquier cadena que empiece por `ux0:app`.

Si Data Files no declara `installed_paths` y hereda Game Files dentro de `ux0:app/<title_id>`, queda bajo la **misma regla de desinstalación de la aplicación**, no como archivo externo independiente.

### 3.6 Si Data Files y Game Files tienen exactamente la misma ruta

También es posible que Data Files declare `["MiJuego"]` mientras Game Files declara `["MiJuego"]`, o que Data Files lo herede por omisión. **En ambos casos** el destino es compartido, el tamaño de disco se mide una sola vez y no hay evidencia suficiente para borrar cada paquete por separado.

### 3.7 Reglas de resolución y validación

- Formato: `installed_paths` debe ser un **array** de cadenas relativas no vacías y sin duplicados cuando haya elementos. El estado «vacío» de Data Files se representa por **ausencia** del campo o por `[]`; `""`, `null` o tipos incorrectos no son representación canónica válida.
- **Game Files externo**: no permitir `installed_paths` ausente ni `[]` en nuevas entradas cuando el `extract_path` no es la raíz `ux0:app/`. Si falta el destino de extracción, el generador no debe presumir `ux0:app/`; requiere rutas explícitas o debe señalar metadatos no resolubles hasta conocer el destino efectivo.
- **Game Files en `ux0:app/`**: omitir el campo por defecto. Si algún JSON histórico lo incluye, no usarlo para autorizar borrado; comprobar `title_id` y el destino de paquete.
- **Data Files vacío**: buscar Game Files del **mismo Homebrew**, resolver su destino efectivo y heredar su(s) ruta(s) como **referencia compartida**, no como prueba de archivos exclusivos. Si hay **varios destinos distintos** de Game Files o enlaces alternativos ambiguos, no elegir uno arbitrariamente: marcar «herencia ambigua».
- Si Data Files tiene `extract_path` distinto/incompatible con la ubicación de Game Files, **no cambiar el destino de extracción** para intentar que coincida; informar que la referencia heredada es incompatible y exigir revisión antes de medir o borrar.
- Si no existe Game Files, o Game Files externo carece de ruta válida (registro antiguo), Data Files vacío **no puede identificarse**: mostrar estado desconocido; no inventar una ruta ni habilitar borrado. El generador avisará y recomendará completar la ruta explícita, sin convertir la opcionalidad de Data Files en requisito.
- Cada componente relativo se concatena con `extract_path`, sin `ux0:`, `uma0:`, barra inicial, `..`, `.`, barras invertidas, controles ni componentes vacíos. Admitir `MiJuego/game.dat`. Respetar nombres/mayúsculas; determinar archivo/carpeta con `sceIoGetstat`, no por extensión.
- Establecer límites de longitud y número de rutas en web, Python y cliente. Rutas de borrado solo en raíces aprobadas, con comprobación de propiedad y colisiones. Una ruta `ux0:app/<title_id>` es solo informativa para la limpieza externa.
- Los JSON antiguos sin campo **no deben romper instalación, visualización ni carga del catálogo** durante la migración. Su capacidad de identificación/borrado puede quedar restringida hasta corregirlos.
- Si el instalador permite seleccionar manualmente `extract_path`, no inventar una ubicación efectiva si no hay recibo local del destino elegido. Revalidar siempre en disco antes de operar.


## 4. Identificación de rutas compartidas, propiedad y contabilidad

- Construir durante la consulta una lista pequeña de **rutas canónicas normalizadas** (solo de enlaces del Homebrew seleccionado o del subconjunto que se examine).
- Para cada ruta: marcar tipo real, existencia, enlace(s) que la declaran y relaciones **igual / ancestro / descendiente** por componentes de ruta, **no por simple prefijo textual** (`MiJuego` no equivale a `MiJuego2`).
- Si dos enlaces apuntan al mismo directorio, calcular su uso **una sola vez**. Si una ruta es descendiente de otra, no volver a sumar sus bytes. Al eliminar un padre, identificar todas las rutas hijas afectadas antes de mostrar confirmación.
- Si un enlace designa únicamente un archivo y no tiene solapamiento, podrá borrarse ese archivo individualmente. Si hay una subcarpeta expresamente designada como exclusiva, podrá borrarse solo tras comprobar que no afecta a otros enlaces/aplicaciones.
- Considerar también colisiones **entre diferentes Homebrew**, no solo entre Game Files y Data Files de una misma app. Si varias apps referencian la misma ruta o una relación padre/hijo, el borrado automático de esa ubicación queda bloqueado hasta revisión/solución explícita. El catálogo es metadato, no prueba definitiva de propiedad.
- Si Data Files **omite** `installed_paths` (o declara `[]`), asociarlo a la ubicación de Game Files del **mismo Homebrew** solo cuando la herencia es unívoca y el destino de extracción es compatible. Marcar esa ruta como **compartida/heredada**, nunca como propiedad exclusiva de Data Files.
- En ausencia de inventario por archivo, **no identificar ni borrar únicamente «la parte de Data Files» de una carpeta compartida**. La opción segura es eliminación conjunta de la carpeta si está validada y no es compartida con otros juegos, o solo informar.
- Evitar sumar el `size` de descarga al **uso real del disco**: usar tamaño medido de rutas físicas únicas. El `size` por enlace sirve solo como referencia aproximada de tamaño esperado.
- Si dos ZIP escriben archivos idénticos o se sobrescriben, la suma de sus `size` no equivale necesariamente al tamaño instalado; presentar la comparación de integridad como **indeterminada**.

## 5. Tamaños, estados e integridad (estimación, no certificación)

**Situación del catálogo:** por convención del mantenedor, los ZIP Game/Data se publican **sin comprimir (STORE)**. El `links[].size` existente se usa como estimación próxima al contenido extraído (diferencias por cabeceras/entradas ZIP, y otros detalles). No modificar ni debilitar el control actual de espacio en instalaciones: estas estimaciones son para **administración/diagnóstico**, no sustituyen medidas y guardias del extractor.

Estados propuestos:

| Estado | Evidencia | Qué debe decir la UI |
|---|---|---|
| Sin ruta identificable | Game Files externo antiguo sin ruta, Data Files sin Game Files heredable, o herencia ambigua | Datos no identificados; no borrar |
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

**Ficha del Homebrew:** nuevo apartado «Datos instalados» con filas por ruta detectada: Game Files/Data Files, ubicación, tamaño real, estado estimado, procedencia del enlace y aviso de carpeta compartida. Indicar explícitamente «Data Files — misma carpeta que Game Files» cuando su ruta sea heredada y no ofrecer borrado selectivo sin inventario por archivo. Botón «Desinstalar» accesible para apps instaladas; opciones separadas cuando la eliminación es segura: «Solo aplicación», «Solo datos externos», «Aplicación y datos externos». Para archivos/carpetas exclusivos, selección individual.

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

Modificar **en fase 1** siguiendo estas reglas **condicionales** (no implementadas todavía):

| Tipo seleccionado en la web | Destino de extracción | Mostrar campo de rutas | Validación para JSON nuevo |
|---|---|---|---|
| Game Files | `ux0:data/` u otra base externa | Sí | **Requerido**, al menos una ruta |
| Game Files | `ux0:app/` | Oculto/deshabilitado o con aclaración «incluido en aplicación» | **No requerido**; omitirlo al exportar |
| Data Files | Cualquier destino | Sí, **opcional** | En blanco equivale a **misma ubicación de Game Files**, si resoluble |
| Otros enlaces, incluidos Plugin/Mod/Patch | Los existentes | **No** para esta nueva función | No serializar `installed_paths` |

Requisitos concretos:

1. En `web/tools/app-generator/app-generator.js`, mostrar el campo como una lista de rutas relativas (una por línea); validarlo únicamente según tipo y `extract_path`. Si el tipo es Game Files, ajustar obligatoriedad en tiempo real cuando `extract_path` cambia hacia/desde `ux0:app/`. Conservar los valores temporales mientras se alternan tipos sin filtrarlos a enlaces ajenos.
2. En Data Files, explicar junto al campo: **«Opcional. Déjalo vacío si los Data Files se instalan dentro de la misma carpeta de Game Files»**. Si no hay Game Files inequívoco, informar «No se pudo determinar la carpeta heredada; indica la ruta para permitir su identificación», pero **no convertirlo en error de obligatoriedad**.
3. En Game Files con `extract_path: "ux0:app/"`, explicar «Los Game Files forman parte de la aplicación; no se requiere indicar carpeta». No rellenar automáticamente `BLPM12345` en `installed_paths`; el cliente tomará `title_id` al consultar.
4. Al crear JSON, **omitir el campo** si Data Files está vacío o Game Files está en `ux0:app/`. Al importar JSON históricos con `[]`, reconocerlo como herencia sin convertirlo en cadena vacía. Los archivos editados preservan sus otros metadatos.
5. Mantener la validación histórica de `extract_path`; la nueva variable describe lo que se detecta o borra y **no cambia las rutas que utiliza ZIPExtractor**.
6. En `scripts/validate_catalog.py`, validar tipos y rutas; definir una **transición que no rompa el catálogo legado** (regla estricta en el generador y en registros nuevos/modificados, diagnóstico para existentes). No activar una regla global de «Game Files obligatorio» hasta cubrir esos registros o disponer de compatibilidad/versionado controlado.
7. En `scripts/generate_catalog.py` y normalización/merge, preservar datos canónicos, omisiones y `[]` sin inferir rutas inexistentes. **Nunca** editar catálogos generados manualmente.
8. Añadir pruebas del generador (`generator.test.cjs`), validadores Python y documentación del generador (`README.md`) para todas las combinaciones de la tabla; probar importar, cambiar tipo, cambiar `extract_path`, descargar JSON y usar modo Edit.

**Matriz adicional de pruebas web:**

- Game Files en `ux0:data/` sin rutas → validación bloquea exportación; con `["MiJuego"]` → exporta.
- Game Files en `ux0:app/` sin rutas → exporta sin `installed_paths`; cambiar a `ux0:data/` → vuelve a exigirlas.
- Data Files con campo vacío → exporta sin `installed_paths` y describe la herencia; con `[]` importado → reconoce herencia.
- Data Files explícitos `["MiJuego/extra.dat"]` → exporta las rutas.
- Data Files vacío y sin Game Files → advertencia de ubicación desconocida, **no fallo de validación**.
- Data Files vacío y varios Game Files distintos, o con `extract_path` incompatible → advertencia de herencia ambigua/incompatible; nunca asignar una ruta silenciosamente.
- Cambio de tipo a Download/Repository/Plugin → no agregar `installed_paths` a tipos ajenos.
- JSON antiguos sin campo → importación y visualización compatibles; registrar falta de metadatos de Game Files para migración posterior.

**Migración progresiva:** crear en el futuro una herramienta PC que examine índices internos de ZIP (sin modificar archivos ni descargar todo si puede usarse inventario local), sugiera rutas raíz y marque ambigüedades; registrar en `apps/*.json` solo después de revisión humana.


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

1. App histórica sin `installed_paths`, instalación VPK/ZIP y render intactos; advertencia/migración para Game Files externos sin ruta.
2. Carpeta `ux0:data/MiJuego`, archivo `ux0:data/game.dat`, carpeta con punto y archivo sin extensión.
3. Game Files `MiJuego` y Data Files `MiJuego/extra.dat`: tamaño no duplicado; borrar padre reconoce que afecta al hijo.
4. Game/Data con exactamente la misma carpeta **explícita o heredada por Data Files vacío/ausente**: no ofrecer borrado separado engañoso; un solo conteo.
5. Datos de dos Homebrew apuntando a misma ruta: bloqueo de borrado peligroso.
6. Game Files con `extract_path: "ux0:app/"` y `installed_paths` omitido: inferir destino informativo `ux0:app/<title_id>` verificando existencia; solo mecanismo de desinstalación de paquete, sin limpieza recursiva externa.
7. Ruta inexistente, sin permiso, árbol muy profundo, demasiados archivos, interrupción de análisis y `uint64_t` para archivos grandes.
8. `..`, `/`, `ux0:data/`, `ux0:app/`, otros montajes, destino con caracteres/partes inválidas: rechazo.
9. No afectar catálogos, imágenes, ajustes, temporales, ZIP conservados ni botón «Limpiar todo».
10. Prueba de instalación activa/cancelada, ZIP fallido o incompleto: nunca borrar archivos ajenos ni afirmar integridad absoluta.
11. Memoria y tiempo: comparar antes/después de la fase 2 y rendimiento al abrir/cerrar el modal de la fase 4 en Vita real.
12. Pruebas web de Create/Edit/Import/Export y CI, incluyendo obligatoriedad condicional de Game Files, excepción `ux0:app/`, herencia de Data Files vacío y compatibilidad histórica, sin necesidad de servidor propio ni cambios manuales en catálogos generados.

## 12. Condiciones de aceptación funcional final

- El usuario puede saber **qué app/port** está asociada con cada carpeta o archivo de datos **conocido**, ver uso real, y qué paquetes lo referencian.
- Puede seleccionar borrado individual **solo si es seguro**; en datos compartidos se explica el alcance real de borrar la carpeta completa.
- Al desinstalar, distingue aplicación/VPK frente a datos externos y evita borrar dos veces.
- Las estimaciones de integridad son transparentes y no reemplazan checksums ni recibos verificados.
- Sin rastreo de carpetas en el arranque ni render, sin ralentización notable de navegación y con RAM acotada.
- Web + cliente + CI siguen basados en `apps/` → catálogos generados, sin nuevos servicios de pago.

---

**Recordatorio para quien implemente:** este archivo es el acuerdo de diseño (5 de noviembre de 2026), **no un changelog de funcionalidad entregada**. Antes de programar, contrastar estas referencias con el código más reciente; documentar diferencias y validar primero la fase 1.
