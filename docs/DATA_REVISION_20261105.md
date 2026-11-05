# PSVitaAlive — optional Game/Data Files revision (`dr`)

**Fecha:** 2026-11-05  
**Ámbito:** Homebrew en `apps/*.json`, catálogo generado, generador web, ficha pública y cliente PS Vita. No cambia los catálogos de juegos oficiales.

## Motivo

Algunos ports publican un VPK nuevo que deja de funcionar con los antiguos **Game Files** o **Data Files**. Antes, el botón **UPDATE** del cliente instalaba únicamente el VPK; esto es correcto para la mayoría de versiones pero peligroso cuando se requiere regenerar o sustituir los datos.

## Contrato del campo

Un único campo **opcional** por app: `"dr": 2` (*data revision*). Es un entero entre 1 y 65535, no una cadena, un booleano ni una versión semántica. Se omite por completo cuando no se usa. Solo se admite en apps con al menos un enlace `Game Files` o `Data Files`. No modifica las propiedades de sus enlaces, `extract_path`, ni los datos de instalación. El campo no significa que el ZIP tenga que descargarse en cada actualización.

Ejemplo parcial de `apps/mi-port.json`:

```json
{
  "title_id": "EXAM00001",
  "version": "1.3.0",
  "dr": 2,
  "links": [
    {"type": "Download", "name": "VPK", "url": "https://example.org/port.vpk", "recommended": true},
    {"type": "Game Files", "name": "Game Files", "url": "https://example.org/data.zip", "extract_path": "ux0:data/"}
  ]
}
```

Los demás campos obligatorios del JSON se mantienen. Nunca editar `catalog.json` manualmente.

Reglas editoriales:

- Versión VPK 1.0, datos A: `"dr": 1`.
- Versión VPK 1.1, mismos datos A: **mantener** `"dr": 1`.
- Versión VPK 1.2, necesita datos B: **incrementar** a `"dr": 2`.
- Versión VPK 1.3, mismos datos B: **mantener** `"dr": 2`.
- Si un proyecto nunca necesita este control, **omitir** `dr`.

El mantenedor debe verificar las instrucciones del autor. `dr` es metadato de compatibilidad declarado, no una detección automática por hash.

## Web

En `web/tools/app-generator/`, sección **3. Release**, el campo *Game/Data Files revision (optional)* aparece junto a la versión. **Create**, **Edit**, importación, previsualización y exportación conservan `dr`, y lo omiten cuando está vacío. Se validan tipo, rango y presencia de enlaces de datos. La ficha pública de Homebrew presenta el número cuando está presente, sin mostrar espacio extra cuando falta.

## Cliente PS Vita

`CatalogItem.dataRevision` añade solo un entero (0 = no indicado); `catalog_parser.cpp` lee `dr` del catálogo.

- **Instalación inicial:** `INSTALL ALL` sigue usando el asistente actual, con una nota visible cuando hay `dr`.
- **Actualización VPK:** al pulsar **UPDATE**, se compara `dr` con un pequeño recibo local por `title_id`. Si coincide, continúa el flujo habitual de **solo VPK**. Si difiere o falta el recibo, se muestra un modal con **VPK + Datos** (preseleccionado), **Solo VPK** y **Cancelar**. No se cambia ninguna fuente de descarga automáticamente ni se alteran los controles del usuario.
- **Actualizar completo:** reutiliza la selección de fuentes, los plugins, el estimador de espacio, la descarga secuencial, recuperación ZIP y el reporte de `INSTALL ALL`. La confirmación del modal sustituye la primera pantalla de confirmación, no la selección de enlaces ni el preflight.
- **Registro local:** `ux0:data/psvitaalive/data_revisions/<TITLEID>.txt`. Guarda un entero pequeño (ej. `2` y salto de línea). **Solo se actualiza si TODOS los pasos seleccionados de Game/Data Files terminaron con éxito.** Fallos, cancelaciones, ZIP incompletos y elegir **Solo VPK** nunca actualizan este recibo. El registro de versión de VPK permanece independiente.
- **Compatibilidad:** las apps sin `dr` conservan su comportamiento previo; no se escanea el disco en cada frame. Recibos desconocidos se muestran como **no verificados**, no como prueba de archivos obsoletos.

### Límites de esta primera implementación

- El recibo local acredita que PSVitaAlive completó satisfactoriamente el último lote de archivos de datos, **no** comprueba la integridad de sus bytes ni detecta borrados/modificaciones externas posteriores.
- Datos instalados manualmente o desde otras tiendas pueden ser válidos, pero no tienen recibo y generan una advertencia. El usuario puede optar por **Solo VPK**.
- Si un juego usa varios conjuntos de datos incompatibles que necesitan revisiones independientes, `dr` por app no permite diferenciarlos: se deberá ampliar el contrato en otra fase tras validación.
- El aviso se activa en el **botón UPDATE** de Homebrew. No cambia el botón **OPEN**, los enlaces individuales, el flujo de juegos comerciales ni las descargas fuera del cliente.
- Ningún paquete de datos de apps existentes cambia de revisión automáticamente en esta entrega. El editor debe introducir `dr` caso por caso siguiendo las notas del autor.
- El proyecto `installed_paths` para inventario y borrado seguro sigue siendo independiente; sus reglas **no** son sustituidas por `dr`.

## QA / regresión

1. App sin `dr`: Create/Edit exporta sin el campo; UPDATE sigue instalando solo VPK.
2. `dr=2` con enlace Game Files: exporta entero; importar y editar conserva 2.
3. `dr=0`, negativo, decimal, string, booleano o >65535: validación rechaza; si no existe Game/Data Files también rechaza.
4. Primera instalación con `dr`: aparece nota en Confirm; solo se registra revisión si los ZIP de datos seleccionados terminan correctamente.
5. Update con revisión igual: UPDATE no solicita ZIP adicionales.
6. Update con revisión anterior o sin recibo: modal, por defecto **VPK + Datos**. Confirmar ejecuta selector(es), preflight y cola actuales; rechazar/solo VPK no modifica recibo.
7. Fallo/cancelación de cualquiera de los ZIP seleccionados: nunca escribir nueva revisión. Probar con y sin VPK exitoso.
8. Probar navegación D-pad, Cross/Circle, selección táctil y las ocho traducciones. Comprobar en Vita física, Vita3K y compilación VitaSDK.
9. El pipeline sigue siendo `apps/ + authors/ + categories/` → Actions → `catalog.json + authors.json + categories.json` → cliente + web.

**Validación incluida:** prueba de lógica pura `tests/test_data_revision_policy.cpp` en el workflow de build de cliente; pruebas del generador en `generator.test.cjs`; validaciones de fuente y final en `validate_catalog.py` y `generate_catalog.py`. La prueba final en PS Vita física requiere ejecutarse manualmente: CI no puede demostrar funcionamiento del almacenamiento ni de los overlays en hardware.
