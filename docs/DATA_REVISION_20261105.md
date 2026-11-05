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

**Cuándo incrementarlo:** únicamente cuando los Game/Data Files que necesita la app cambien de manera incompatible. Una actualización exclusiva del VPK conserva el mismo `dr`. La primera vez que se añade `dr: 1` a una app anteriormente publicada sin `dr`, se anuncia una revisión nueva para los usuarios sin registro local. Este comportamiento **no presupone** que los archivos antiguos estén corruptos; es una política editorial explícita de compatibilidad.

## Flujo del cliente: matriz de decisiones

| Instalación de la app | `dr` del catálogo | `dr` local | Resultado |
|---|---:|---:|---|
| No instalada | Cualquiera | Cualquiera | `INSTALL ALL` normal |
| Instalada | No existe | Cualquiera | Flujo VPK tradicional |
| Instalada | 1 | No existe (0) | **`UPDATE ALL`** |
| Instalada | 2 | 1 | **`UPDATE ALL`** |
| Instalada | 2 | 2 | `UPDATE` solo VPK, si VPK cambió; si no, `OPEN` |
| Instalada | 2 | 3 | No bajar automáticamente los datos; flujo VPK normal |

La acción `UPDATE ALL` tiene prioridad sobre `UPDATE` VPK, y aparece también en el **filtro de actualizaciones** si solo cambió `dr` y `version` sigue igual. Aplica a Homebrew instalado, incluidos releases cuya identidad de VPK está marcada como desconocida; **no** se cambia la lógica de juegos comerciales, PSP o PS1.

Al pulsar `UPDATE ALL`, aparece la **confirmación habitual de Install All** con una advertencia contextual: la revisión de Game/Data Files del catálogo es nueva y se reinstalarán VPK y datos. `Continuar` lanza exactamente el mismo asistente que una instalación nueva: selección de fuentes, selección de Game Files/Data Files, decisiones de plugins si las hay, comprobación de espacio, instalación secuencial y resumen final. `Cancelar` no realiza cambios. No hay un botón VPK-only en esta confirmación; quien quiera una instalación individual puede usar los enlaces de descarga habituales.

No se ejecuta ninguna eliminación preventiva de los datos antiguos. El ZIP pasa por el instalador/extractor que ya existe, con sus políticas de sobreescritura y recuperación. **La posibilidad de reemplazar todos los archivos existentes depende de esas reglas y del contenido del ZIP**; no se garantiza limpieza de ficheros obsoletos que ya no estén en el archivo nuevo.

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

El recibo indica el resultado de una instalación realizada por PSVitaAlive; no verifica hashes de los archivos de datos ni detecta modificaciones manuales posteriores. Si los datos se instalaron fuera de PSVitaAlive no hay una revisión fiable y se ofrece `UPDATE ALL` al introducir `dr`. La actualización de datos que el usuario confirme sobreescribe lo que el extractor existente admita, sin borrado preventivo.

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
