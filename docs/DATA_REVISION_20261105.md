# PSVitaAlive — Game/Data Files revision (`dr`)

**Actualizado:** 2026-11-05  
**Estado:** implementado en el cliente, generador web, catálogo y validación; falta prueba funcional en hardware.

## Contrato público (sin cambios)

La fuente es siempre `apps/*.json`; GitHub Actions genera `catalog.json` y la web y el cliente consumen el archivo generado. Nunca editar `catalog.json` a mano.

```json
{
  "version": "1.4.0",
  "dr": 2
}
```

`dr` es un entero opcional entre 1 y 65535. Se permite solo si la app tiene un enlace `Game Files` o `Data Files`. El editor web permite crear, importar, modificar, validar y exportar el campo; una app sin `dr` lo omite del JSON. En la ficha pública se muestra cuando corresponde.

**Cuándo incrementarlo:** únicamente cuando los Game/Data Files que necesita la app cambien de manera incompatible. Una actualización exclusiva del VPK conserva el mismo `dr`. La primera vez que se añade `dr: 1` a una app anteriormente publicada sin `dr`, **no se ofrece una actualización de inmediato si la versión del VPK permanece igual**. Cuando posteriormente se publica una versión nueva del VPK, un `dr` local ausente o inferior hace que ese UPDATE utilice **UPDATE ALL**. El número `dr` no es una segunda versión que genere avisos de actualización por sí misma.

## Flujo del cliente: matriz de decisiones

**Regla principal:** `version` (o el detector de versión VPK existente) decide **si hay una actualización**. `dr` decide **cómo instalar esa actualización**, nunca **cuándo** ofrecerla. En el filtro de actualizaciones solo aparecen apps con una actualización VPK detectada.

| VPK instalado | VPK en catálogo | `dr` local | `dr` catálogo | Acción |
|---|---|---:|---:|---|
| 1.2 | 1.2 | 0 (sin registro) | 1 | `OPEN`; **no** UPDATE |
| 1.2 | 1.2 | 1 | 2 | `OPEN`; **no** UPDATE |
| 1.2 | 1.3 | 0 (sin registro) | 1 | **`UPDATE ALL`** |
| 1.2 | 1.3 | 1 | 2 | **`UPDATE ALL`** |
| 1.2 | 1.3 | 2 | 2 | `UPDATE` solo VPK |
| 1.3 | 1.3 | 2 | 2 | `OPEN` |
| 1.2 | 1.3 | Cualquiera | Sin `dr` | `UPDATE` solo VPK |
| No instalada | Cualquiera | Cualquiera | Cualquiera | `INSTALL ALL` normal, cuando corresponda |

Cuando hay una actualización VPK real, `UPDATE ALL` tiene prioridad si el `dr` indicado por el catálogo es superior al `dr` registrado localmente. Si no hay actualización VPK, cambiar `dr` no cambia el botón ni hace que la aplicación aparezca en el filtro **Updates**. Las instalaciones sin `dr` local se tratan como revisión 0 **solo para decidir el tipo de instalación durante un UPDATE VPK posterior**.

Al pulsar `UPDATE ALL`, aparece la **confirmación habitual de Install All** con una advertencia contextual sobre los nuevos Game/Data Files. `Continuar` lanza el asistente existente de selección de fuentes, verificación de espacio, instalación secuencial y resumen final; `Cancelar` no hace cambios.

**Enlaces alternativos:** varios enlaces de `Game Files` pueden ser espejos o paquetes por idioma; lo mismo ocurre con `Data Files`. El asistente **elige un único enlace Game Files y un único enlace Data Files**, si existen ambas categorías, además de una única fuente de VPK. Si existe una sola opción, se selecciona directamente; con varias, se muestra el selector y se prioriza la marcada como recomendada. **Nunca se descargan todas las alternativas de una misma categoría.** Los plugins conservan su selección independiente habitual.

No se ejecuta ninguna eliminación preventiva de los datos antiguos. El ZIP pasa por el instalador/extractor que ya existe, con sus políticas de sobreescritura y recuperación. La eliminación de archivos obsoletos que no estén en el ZIP nuevo no está garantizada.

## Registro local único + migración

Se mantiene un solo recibo principal en:

`ux0:data/psvitaalive/installed/<TITLEID>.json`

Ejemplo (versión del esquema 2):

```json
{
  "schema_version": 2,
  "app_id": "ejemplo",
  "title_id": "EXAM00001",
  "catalog_version": "1.4.0",
  "version_date": "2026-11-05",
  "release_revision": 0,
  "dr": 2,
  "verification": {
    "path": "eboot.bin",
    "algorithm": "sha256",
    "digest": ""
  }
}
```

- `catalog_version` y `release_revision` siguen siendo metadatos de la **instalación del VPK**. No se confunden con `dr`.
- `dr: 0` significa **sin revisión de datos registrada** (por ejemplo, apps antiguas); `dr: 1` o superior es la última revisión instalada mediante un lote de datos completado satisfactoriamente.
- Los recibos antiguos `schema_version: 1` se leen como hasta ahora. Si existe el archivo de la primera implementación de `dr`, `ux0:data/psvitaalive/data_revisions/<TITLEID>.txt`, se recupera su número. Cuando el JSON unificado se escribe correctamente, se retira el .txt heredado. No se borra antes de haber escrito el JSON.
- Al actualizar **solo el VPK**, el instalador conserva el `dr` anterior y solo actualiza los metadatos del VPK.
- `Install All` escribe `dr` **solo si todos los Game/Data Files seleccionados terminaron correctamente**. Una descarga fallida, una extracción incompleta, el aborto del asistente o la instalación aislada del VPK **no** adelantan `dr`.
- El detector de instalaciones sigue utilizando el caché y el presupuesto de lecturas existente; no se introduce un escaneo del disco por frame. La clave del caché incorpora el `dr` del catálogo para detectar cambios de este campo aun cuando `version` no cambie.

El recibo indica el resultado de una instalación realizada por PSVitaAlive; no verifica hashes de los archivos de datos ni detecta modificaciones manuales posteriores. Si los datos se instalaron fuera de PSVitaAlive no hay una revisión fiable; cuando llegue una **nueva versión del VPK**, un `dr` local sin registro hará que se ofrezca `UPDATE ALL`. Cambiar solo `dr` en el catálogo no presenta actualización. La actualización de datos que el usuario confirme sobreescribe lo que el extractor existente admita, sin borrado preventivo.

## Casos de prueba

1. VPK 1.2 instalado, catálogo todavía 1.2: añadir `dr: 1` con recibo antiguo sin `dr` **no** muestra actualización ni aparece en filtro Updates.
2. VPK 1.2 instalado, catálogo 1.3 y `dr: 1`: aparece `UPDATE ALL` y filtro Updates sí lo incluye.
3. VPK 1.2 instalado, catálogo 1.3, `dr` local 1 / catálogo 2: `UPDATE ALL`. Con `dr` local 2 / catálogo 2: solo `UPDATE` VPK.
4. Catálogo sin `dr`, o `dr` menor/igual al local: comportamiento VPK anterior.
5. Tres enlaces Game Files (distintos idiomas/mirrors): el usuario escoge **solo uno**. Repetir para varios Data Files. El batch encola solo el seleccionado de cada tipo, más el VPK.
6. Confirmación `UPDATE ALL`, cancelar: no descargar ni actualizar el recibo.
7. Confirmación `UPDATE ALL`, aceptar: mismos pickers, comprobación de espacio, ZIP recovery y resumen que `INSTALL ALL`.
8. Fallo de un ZIP de datos: `dr` sin cambios. Instalación aislada del VPK: tampoco actualizar `dr`.
9. Éxito de VPK + datos: registro JSON del VPK preservado y `dr` actualizado; migrar .txt anterior si existe.
10. Comprobar navegación con mando/táctil, idiomas y migración real en PS Vita/Vita3K. Las pruebas CI (VitaSDK y lógica de política) **no sustituyen** la prueba real.

ategorías, además de una única fuente de VPK. Si existe una sola opción, se selecciona directamente; con varias, se muestra el selector y se prioriza la marcada como recomendada. **Nunca se descargan todas las alternativas de una misma categoría.** Los plugins conservan su selección independiente habitual.

No se ejecuta ninguna eliminación preventiva de los datos antiguos. El ZIP pasa por el instalador/extractor que ya existe, con sus políticas de sobreescritura y recuperación. La eliminación de archivos obsoletos que no estén en el ZIP nuevo no está garantizada.

## Registro local único + migración

Se mantiene un solo recibo principal en:

`ux0:data/psvitaalive/installed/<TITLEID>.json`

Ejemplo (versión del esquema 2):

```json
{
  "schema_version": 2,
  "app_id": "ejemplo",
  "title_id": "EXAM00001",
  "catalog_version": "1.4.0",
  "version_date": "2026-11-05",
  "release_revision": 0,
  "dr": 2,
  "verification": {
    "path": "eboot.bin",
    "algorithm": "sha256",
    "digest": ""
  }
}
```

- `catalog_version` y `release_revision` siguen siendo metadatos de la **instalación del VPK**. No se confunden con `dr`.
- `dr: 0` significa **sin revisión de datos registrada** (por ejemplo, apps antiguas); `dr: 1` o superior es la última revisión instalada mediante un lote de datos completado satisfactoriamente.
- Los recibos antiguos `schema_version: 1` se leen como hasta ahora. Si existe el archivo de la primera implementación de `dr`, `ux0:data/psvitaalive/data_revisions/<TITLEID>.txt`, se recupera su número. Cuando el JSON unificado se escribe correctamente, se retira el .txt heredado. No se borra antes de haber escrito el JSON.
- Al actualizar **solo el VPK**, el instalador conserva el `dr` anterior y solo actualiza los metadatos del VPK.
- `Install All` escribe `dr` **solo si todos los Game/Data Files seleccionados terminaron correctamente**. Una descarga fallida, una extracción incompleta, el aborto del asistente o la instalación aislada del VPK **no** adelantan `dr`.
- El detector de instalaciones sigue utilizando el caché y el presupuesto de lecturas existente; no se introduce un escaneo del disco por frame. La clave del caché incorpora el `dr` del catálogo para detectar cambios de este campo aun cuando `version` no cambie.

El recibo indica el resultado de una instalación realizada por PSVitaAlive; no verifica hashes de los archivos de datos ni detecta modificaciones manuales posteriores. Si los datos se instalaron fuera de PSVitaAlive no hay una revisión fiable; cuando llegue una **nueva versión del VPK**, un `dr` local sin registro hará que se ofrezca `UPDATE ALL`. Cambiar solo `dr` en el catálogo no presenta actualización. La actualización de datos que el usuario confirme sobreescribe lo que el extractor existente admita, sin borrado preventivo.

## Casos de prueba

1. App antigua instalada, recibo JSON v1 sin `dr`; catálogo nuevo con `dr: 1`: `UPDATE ALL` y aparición en filtro `Updates`, incluso si versión VPK coincide.
2. App antigua con `schema_version: 1` y antiguo archivo .txt válido: recuperar `dr` al consultar y migrar al próximo guardado de recibo JSON.
3. `dr` local 1, catálogo 2: `UPDATE ALL`. `dr` local 2, catálogo 2: VPK-only `UPDATE` u `OPEN`. Local 3, catálogo 2: nunca rebajar datos automáticamente.
4. Catálogo sin `dr`: comportamiento anterior completo.
5. Confirmación `UPDATE ALL`, cancelar: no descargar archivos ni actualizar recibo.
6. Confirmación `UPDATE ALL`, aceptar: mismos pickers, preflight de espacio, ZIP recovery y resumen que `INSTALL ALL`.
7. Fallo de un ZIP de datos: `dr` sin cambios, aunque el VPK hubiera terminado correctamente.
8. Éxito de VPK más Game/Data Files: metadatos del VPK preservados y `dr` actualizado. Si VPK falla, nunca debe falsificarse su versión.
9. Actualización VPK-only después de instalar `dr`: no borrar el valor.
10. Comprobar navegación con mando/táctil, idiomas y migración real en PS Vita/Vita3K. Las pruebas CI (VitaSDK y lógica de política) **no sustituyen** una prueba real de instalación.

## Implementación

- `Client PSVitaAlive/include/update/data_revision_policy.hpp`: comparación de revisiones.
- `Client PSVitaAlive/source/ui/full_catalog_screen.cpp`: detección y botón `UPDATE ALL`, filtro Updates, confirmación y cola Install All.
- `Client PSVitaAlive/source/update/installed_release_tracker.cpp`: JSON v2, revisión de datos y migración.
- `Client PSVitaAlive/source/installer/install_controller.cpp`: conservación de `dr` al instalar un VPK.
- `Client PSVitaAlive/include/update/data_revision_tracker.hpp`: lectura y limpieza del .txt antiguo.
- `tests/test_data_revision_policy.cpp`: pruebas unitarias sin consola.
- `web/tools/app-generator/`, `scripts/validate_catalog.py` y `scripts/generate_catalog.py`: esquema público opcional, validación y exportación.

**Arquitectura preservada:** `apps/ + authors/ + categories/` → GitHub Actions → `catalog.json + authors.json + categories.json` → cliente PS Vita + web.
