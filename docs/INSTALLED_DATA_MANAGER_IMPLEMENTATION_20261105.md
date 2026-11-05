# PSVitaAlive — Administrador de Game/Data Files instalados

**Fecha:** 5 de noviembre de 2026  
**Implementación:** cliente PS Vita + generador JSON + validador.  
**Validación:** compilación VitaSDK y pruebas automatizadas; los borrados/desinstalaciones **deben probarse en una consola física** antes de distribuir una versión estable.

## Objetivo y arquitectura

Identificar las carpetas/archivos instalados por Game Files y Data Files, mostrar uso real de almacenamiento y ofrecer eliminación explícita desde una lista por aplicación y desde la ficha individual, respetando el diseño de [INSTALLED_GAME_DATA_MANAGEMENT_PLAN_20261105.md](INSTALLED_GAME_DATA_MANAGEMENT_PLAN_20261105.md).

Se preserva el contrato: `apps/`, `authors/`, `categories/` → GitHub Actions → `catalog.json`, `authors.json`, `categories.json` → cliente PS Vita y web. **Jamás editar catálogos generados.**

### Metadatos de enlaces

Los enlaces `Game Files` y `Data Files` aceptan `installed_paths` como array de rutas relativas a `ux0:data/`. El destino real de extracción continúa siendo `extract_path`. El cliente **no** utiliza `installed_paths` para cambiar el extractor.

Ejemplo:

```json
{
  "type": "Game Files",
  "name": "Game Files",
  "extract_path": "ux0:data/",
  "installed_paths": ["kotor"]
}
```

Se detecta `ux0:data/kotor/` sin incluir un supuesto tamaño descomprimido del VPK. Un destino como `ux0:data/AnimalCrossing/rom/` se asocia a `installed_paths: ["AnimalCrossing"]`, **no** al duplicado `AnimalCrossing/rom/AnimalCrossing`.

Para `ux0:app/` y `ux0:app/<title_id>/`, los Game Files forman parte de la instalación del paquete y no se requiere `installed_paths`. Para Data Files, la omisión del campo o `[]` significa herencia **solo si** existe Game Files del mismo Homebrew inequívoco y compatible; no demuestra que se posean archivos exclusivos. Si falta información, se muestra estado protegido y no se permite borrar automáticamente.

La auditoría previa cubrió **158 aplicaciones Game Files (150 bajo ux0:data y 8 bajo ux0:app), 172 enlaces**; véase [GAME_FILES_ROUTES_20261105.md](GAME_FILES_ROUTES_20261105.md). Se bloquearán rutas compartidas, por ejemplo `ux0:data/ctr/` (CTR High Octane / Cut the Rope Classic) y `ux0:data/smworld/` (varios ports de Mario). Algunos ZIP contienen carpetas adicionales no descritas por `installed_paths`; **no se presupone propiedad ni se borran** dichas carpetas.

## Cliente: funcionamiento

1. El parser de catálogo lee `links[].installed_paths` solo como **metadatos**. No consulta disco durante carga, filtros, animación ni renderizado.
2. Desde **Administrador UX0**, existe una quinta sección independiente: **Game/Data Files de Homebrew instalados**. Al entrar, un trabajador de baja prioridad recorre las rutas conocidas, identifica archivos/directorios con `sceIoGetstat`, suma sus bytes y genera una lista por aplicación. El total refleja solamente **datos externos medidos**, no el VPK comprimido ni la instalación `ux0:app/`. Si una ruta es compartida o padre/hijo de otra, se contabiliza una vez en el total. Ante lectura parcial, se muestra advertencia; no se afirma integridad.
3. Al seleccionar una aplicación se presenta su nombre, Title ID, si el paquete existe, el tamaño externo medido y las rutas encontradas. Las alternativas de enlaces (idioma o espejo) se agrupan por la **misma ubicación**; no se suman múltiples enlaces como si fueran instalaciones simultáneas.
4. En la **ficha del Homebrew instalado** aparece **Desinstalar**. La ventana independiente ofrece **Desinstalar VPK**, **Borrar datos externos**, **Borrar ambos** y **Volver**. El botón físico Triángulo desde la ficha también abre este menú (navegación PS TV).
5. Antes de una operación destructiva hay una segunda confirmación que advierte de **pérdida permanente de datos y partidas guardadas** e indica **hacer copia en VitaShell**. No existe limpieza automática ni borrado durante el arranque. Solo actúa tras consentimiento.
6. La desinstalación del paquete llama a **PromoterUtility** y verifica su resultado; **jamás** elimina recursivamente `ux0:app/<title_id>`. El cliente mismo (`PSVAS1178`) está excluido. La eliminación externa usa una política distinta y no modifica `StorageMaintenance::cleanupTransient()`, `removeSelected()` ni **Limpiar todo**.
7. Si se eliminan datos externos de un Homebrew, su registro local `dr` vuelve a 0 cuando existe un recibo de instalación válido. La lógica anterior de UPDATE/UPDATE ALL sigue intacta: **`dr` no crea actualizaciones sin nueva versión del VPK**.

### Seguridad del sistema de archivos

- Solo se admiten raíces declaradas **bajo `ux0:data/`**, con componentes relativos validables. Se rechazan `..`, rutas absolutas, caracteres de control, `ux0:app`, `ur0:`, `tai`, carpetas del cliente, cachés, `psva_vpk` y otros directorios reservados.
- Las colisiones entre Homebrew se detectan por componente completo (p.ej. `juego` y `juego/saves` coinciden; `juego` y `juego2`, no). Si existe posible propiedad compartida, **bloquear el borrado**, aun si la carpeta se puede medir.
- Si una app posee alternativas Game Files con diferentes raíces externas, se marca ambigua. Data Files sin Game Files inequívoco también se marcan ambiguos. No inventar ubicaciones a partir del nombre, Title ID ni del `size` del ZIP.
- Inspección y eliminación con trabajadores; máximo 24 niveles y 200.000 entradas por recorrido. Sin volcar cada archivo a RAM ni almacenar una base SQLite.
- Antes de eliminar se vuelve a verificar la ruta de destino. La eliminación de un padre implica datos y partidas guardadas dentro de él, por lo que nunca debe presentarse como «solo el ZIP».
- Si un proceso de instalación está activo, se impide iniciar la administración destructiva.

### Compatibilidad

- Apps sin `installed_paths` conservan instalación y descarga normales; la eliminación de rutas externas no identificables queda **bloqueada**.
- La web permite Create/Edit/Import/Export del campo, una ruta por línea. Game Files externos nuevos requieren una ruta; Game Files dentro de `ux0:app` no. Data Files la admiten opcionalmente. La validación Python comprueba los campos que existen y permite registros históricos durante migración.
- `size` se conserva como dato de descarga del enlace; la nueva pantalla calcula bytes reales externos mediante inspección del sistema de archivos.
- Los catálogos de juegos comerciales Vita, PSP y PS1 no se tocan. Los 4 apartados previos de almacenamiento permanecen sin cambios y los datos de Homebrew nunca entran en la acción «Limpiar todo».

## Validación y aspectos pendientes en hardware

**Automatizable:** tests puros de rutas, compilación con VitaSDK, tests de generador web/validador de JSON, comprobación de locales, presencia de los componentes de interfaz y VPK generado en GitHub Actions.

**Pruebas en Vita real indispensables antes de publicar como estable:**

- Desinstalar un Homebrew de ejemplo con datos en `ux0:data/`; comprobar menú, confirmación, resultado del PromoterUtility, burbuja y registros.
- Repetir con uno cuyos Game Files están bajo `ux0:app/` (The Drifter, Pocket Mortys), sin intentar borrar paquetes recursivamente.
- Confirmar que rutas compartidas de `ctr` y `smworld` no permiten borrado externo.
- Verificar un paquete con Data Files compartidos y otro con Data Files propios, así como fuentes alternativas por idioma.
- Medir RAM/latencia con listas de muchas apps, rutas profundas, ZIP parciales y almacenamiento de baja velocidad.
- Confirmar navegación táctil, PS TV, botón Triángulo, todas las traducciones y el retorno de la ventana.
- Verificar caso de fallo del PromoterUtility o eliminación parcial, sin mensajes de éxito incorrectos.

**Limitación deliberada:** el catálogo describe raíces, pero aún no contiene un inventario verificado de cada archivo que escribió cada ZIP. Por lo tanto no se ofrece eliminación selectiva de «solo la parte Data Files» dentro de una carpeta compartida ni comprobación criptográfica de integridad; ambas serían fases posteriores con recibos por archivo. No se promete recuperar archivos eliminados.

## Código

- `include/storage/installed_data_policy.hpp`: validador puro y solapamientos.
- `include/storage/installed_data_manager.hpp`, `source/storage/installed_data_manager.cpp`: medición y operaciones protegidas.
- `include/ui/ui_types.hpp`, `source/catalog/catalog_parser.cpp`: nueva propiedad por enlace.
- `source/ui/installed_data_screen.inc`, `source/ui/full_catalog_screen.cpp`: UI de ficha, ventana y trabajadores.
- `web/tools/app-generator/`, `scripts/validate_catalog.py`: esquema web y validación.
- `tests/test_installed_data_policy.cpp`, `web/tools/app-generator/generator.test.cjs`: regresión.
