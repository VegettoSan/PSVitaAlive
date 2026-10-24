# PSVitaAlive — Corrección de detección de plugins taiHEN (2026-10-23)

**Estado:** implementada en `main`, compilación de VitaSDK correcta y **caso original validado por el usuario en una PS Vita real**.  
**Commit de implementación:** [`bd131a99fd6b6503946d5d44e4046c4bc29960e7`](https://github.com/VegettoSan/PSVitaAlive/commit/bd131a99fd6b6503946d5d44e4046c4bc29960e7)  
**Compilación:** [Build PS Vita Client — success](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40056539974)  
**Alcance:** cliente nativo, enlaces del catálogo con `type: Plugin` y sus indicadores de instalación. No afecta al formato del catálogo ni a la web.

## 1. Problema reproducido

1. Instalar un plugin desde un enlace de PSVitaAlive.
2. Desinstalarlo o desactivarlo desde AutoPlugin, de modo que **se elimina su entrada de `config.txt` pero permanece el binario `.skprx` en disco**.
3. Volver al detalle de la aplicación en PSVitaAlive.
4. **Antes de la corrección:** aparecía como `Installed`; pulsar el enlace mostraba `Already installed` y `Install All` podía omitirlo.

**Causa:** la función `isPluginAlreadyInstalled(const CatalogLink&)` de `full_catalog_screen.cpp` solo comprobaba que existiera el archivo y que tuviera tamaño positivo. No comprobaba si la entrada seguía registrada en el archivo de configuración taiHEN. Esto afectaba a los plugins ofrecidos mediante enlaces del catálogo, aunque la detección de ciertos plugins esenciales ya tenía otras comprobaciones.

## 2. Regla implementada

Para los enlaces `Plugin`, el cliente obtiene primero la ruta esperada del binario mediante `pluginInstallFilePath()`.

| Condición | Estado mostrado |
|---|---|
| Archivo existente y de tamaño positivo **más** línea exacta en el `config.txt` consultado, para un plugin que requiere sección | Instalado |
| Archivo existente, pero línea eliminada | No instalado |
| Archivo existente, pero línea comentada | No instalado |
| Línea en `config.txt` pero archivo ausente o vacío | No instalado |
| `section: "none"` o sección omitida/vacía y archivo existente | Instalado (plugin solo de archivo) |
| Se requiere `config.txt` y `line` está vacío | No instalado |

La comprobación de `config.txt` se realiza mediante **`TaiConfigEditor::configContainsLine(l.line)`**. Busca una línea de texto exacta tras quitar espacios iniciales/finales; no cuenta una línea que tenga `#` al principio. Una línea eliminada o comentada no satisface la comprobación.

Ejemplo de enlace que **requiere** `config.txt`:

```json
{
  "type": "Plugin",
  "name": "Plugin de ejemplo",
  "url": "https://example.org/example.skprx",
  "extract_path": "ur0:tai/",
  "section": "*KERNEL",
  "line": "ur0:tai/example.skprx"
}
```

Si existe `ur0:tai/example.skprx` y la configuración contiene:

```text
*KERNEL
ur0:tai/example.skprx
```

el cliente indica `Installed`. Si AutoPlugin elimina esa línea, o pasa a `# ur0:tai/example.skprx`, deja de considerarse instalado aunque permanezca el archivo.

En cambio, para un enlace con `section: "none"` no se exige línea en `config.txt`, por diseño.

## 3. Lugares donde se aplica

- **Detalle / enlaces:** la insignia `Installed` refleja el nuevo criterio.
- **Instalación individual:** el enlace ya no se bloquea con `Already installed` cuando el binario permanece pero su entrada ha sido retirada.
- **`Install All`:** no omite un plugin desactivado únicamente porque exista el binario.
- **Después de instalar:** se actualiza el estado guardado de los enlaces cuando se completa un trabajo de plugin que solicita reinicio.
- **Al abrir o navegar por el detalle:** se renueva la comprobación para la aplicación seleccionada.

## 4. Rendimiento y separación de responsabilidades

El renderizado **no** debe abrir archivos ni releer `config.txt` en cada fotograma. `FullCatalogScreen::refreshDetailPluginStatus()` calcula una instantánea `detailPluginInstalled_` en memoria, indexada según `linkDetails`. Los elementos visibles consultan ese vector; la comprobación de instalación se realiza cuando corresponde a una acción o al actualizar la instantánea.

La detección para enlaces de catálogo permanece separada de `PluginDetector::scan()`, que calcula los estados de plugins del sistema, y de `essentialPluginFullyInstalled()`, que aplica reglas adicionales a la recomendación de plugins esenciales (por ejemplo, huella de tamaño de kubridge y consideración de RePatch para FdFix).

No se modificaron los flujos de descarga, copia, edición de `config.txt`, VPK, ZIP, PKG, PSP/PS1, ni los JSON oficiales.

## 5. Archivos de implementación

- `Client PSVitaAlive/source/ui/full_catalog_screen.cpp`
  - `isPluginAlreadyInstalled()`: archivo no vacío + entrada de configuración, salvo plugins de solo archivo.
  - `refreshDetailPluginStatus()`: instantánea de los enlaces.
  - Puntos de actualización: apertura/navegación de detalle y finalización de instalaciones de plugin.
  - Renderizado de enlaces: usa la instantánea sin consultas al sistema de archivos por fotograma.
- `Client PSVitaAlive/include/ui/full_catalog_screen.hpp`
  - Declaración de `refreshDetailPluginStatus()` y vector `detailPluginInstalled_`.
- **Componentes reutilizados, no modificados en el commit correctivo:**
  - `source/installer/tai_config_editor.cpp`: `configContainsLine()` y resolución de la configuración.
  - `source/installer/install_controller.cpp`: descarga/copias y configuración del plugin.
  - `source/installer/plugin_detector.cpp`: detección de plugins del sistema.

## 6. Validación

- **GitHub Actions, Build PS Vita Client:** `success` para el commit `bd131a9`.
- **PS Vita real:** el usuario confirmó el 23 de octubre de 2026 que la corrección funcionó tras el problema reportado con AutoPlugin.
- **Importante:** esta confirmación valida el **caso original**; no significa que todos los escenarios de regresión siguientes se hayan ejecutado en hardware.

### Lista de regresión para futuras modificaciones

- [ ] Archivo presente + línea activa → `Installed`; instalación individual bloqueada, `Install All` lo omite.
- [ ] Archivo presente + línea eliminada por AutoPlugin → no `Installed`; instalación individual disponible, `Install All` lo incluye.
- [ ] Archivo presente + línea comentada → no `Installed`.
- [ ] Archivo ausente + línea activa → no `Installed`.
- [ ] Enlace `section: "none"` → archivo suficiente; no exige `config.txt`.
- [ ] Instalar plugin → indicador actualizado y aviso de reinicio conservado.
- [ ] Al abrir/cambiar detalle → indicadores actualizados sin comprobaciones de disco en cada fotograma.
- [ ] Verificar que recomendaciones de kubridge/FdFix/libshacccg, VPK/ZIP/PKG y PSP/PS1 siguen funcionando.

## 7. Límites conocidos: no confundir con detección de carga real

1. **Entrada configurada no equivale a plugin ya cargado en el kernel.** Después de instalar puede requerirse reiniciar la consola; la insignia refleja archivo/configuración, no inspección de módulos activos.
2. **Coincidencia de línea, no análisis semántico de sección.** `configContainsLine()` no valida que la entrada esté bajo la sección `*KERNEL`, `*main`, etc.; comprueba la línea exacta en el archivo. Además la coincidencia es sensible a mayúsculas/minúsculas.
3. **Se consulta `ur0:tai/config.txt` (o `ur0:/tai/config.txt`).** El resolvedor actual ignora `ux0:tai/config.txt`. Es una decisión preexistente en el cliente, no introducida por esta corrección.
4. **Las insignias se actualizan por eventos, no continuamente.** Si AutoPlugin modifica la configuración mientras el mismo detalle ya está abierto, volver a abrir/navegar al detalle permite refrescar la instantánea.
5. **No se añade verificación de versiones o hashes.** La detección de binarios obsoletos sigue siendo un trabajo separado descrito en [`PLUGIN_UPDATES.md`](PLUGIN_UPDATES.md).

## 8. Regla de mantenimiento

**Para un plugin taiHEN registrado en un enlace del catálogo, nunca marcar `Installed` solo por encontrar el `.skprx`: comprobar también su entrada en la configuración, salvo que el enlace declare explícitamente que es de solo archivo.**

Mantener cualquier lectura de archivos fuera del bucle de renderizado. Ante cambios futuros, preservar los recorridos de instalación individual, `Install All`, reinicio, plugins esenciales y los enlaces con `section: "none"`.
