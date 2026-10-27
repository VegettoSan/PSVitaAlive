# PSVitaAlive — Avisos de compatibilidad de plugins del catálogo (2026-10-27)

**Estado:** implementación propuesta en [PR #59](https://github.com/VegettoSan/PSVitaAlive/pull/59), solicitada para integración en `main`.  
**Alcance:** exclusivamente enlaces de tipo `Plugin` incluidos en fichas de aplicaciones Homebrew (`apps/*.json`).  
**Fuera de alcance:** ventana de plugins esenciales (kubridge, FdFix, libshacccg), detección de sus versiones y descargas automáticas, actualización de `catalog.json`, web, instalación de VPK/ZIP/PKG.

## 1. Comportamiento de usuario

Al seleccionar un enlace del catálogo con `type: "Plugin"`, el cliente conserva **una sola ventana** de consentimiento. La misma ventana sirve para la instalación individual y para la fase `PickPlugin` de **Install All**.

Orden del contenido visible:
1. **Posible conflicto entre plugins**, solo si hay coincidencias en las reglas documentadas; enumera los nombres detectados.
2. Clasificación de la ficha **Requerido / Opcional / No especificado**.
3. Explicación habitual de instalación o reinstalación.
4. Botones existentes **Instalar/Reinstalar** y **Omitir**, siempre fijos.

Cuando sobran líneas, el contenido admite scroll con **Arriba/Abajo**, stick izquierdo o gesto de arrastre vertical sobre el cuerpo del diálogo. Una indicación localizada `PLUGIN_CONFLICT_SCROLL` aparece solo si el contenido supera el número de filas visibles. Desplazar el texto no activa los botones ni el catálogo situado detrás.

La advertencia es **informativa**, no impide expresamente la instalación, no edita la configuración y no deshabilita otros plugins.

## 2. Tabla implementada

| Plugin solicitado desde el catálogo | Buscar si está configurado | Motivo | Interpretación |
| --- | --- | --- | --- |
| `ioplus.skprx` | `kuio.skprx` | Reportes de problemas en ciertas configuraciones con ioPlus/kuio | Riesgo reportado; **no** incompatibilidad universal |
| `ioplus.skprx` | `kaio.skprx` / `v-kaio.skprx` | v-kaio sustituye/incluye funciones de ioPlus | Solapamiento, no fallo demostrado |
| `CapUnlocker.skprx` | `CoreUnlocker80000H.skprx` | Antecesor de CapUnlocker | Posible duplicación de funciones |

Si existen dos coincidencias, se enumeran ambas y el contenido se desplaza según sea necesario. La comprobación del nombre **no diferencia versiones** del binario; no afirmar que existe incompatibilidad absoluta.

Los otros enlaces de plugin presentes en las fichas examinadas, `azaharnative.skprx` y `ttbr_remap.skprx`, **no tienen reglas de conflicto confirmadas**. No se introducen avisos especulativos.

El número de fichas o enlaces del catálogo puede variar a medida que se publican proyectos; la tabla depende del **nombre del plugin enlazado**, no de un `app_id` ni de una cifra fija de aplicaciones.

## 3. Evidencia y límites

- ioPlus: [TeamFAPS/PSVita-RE-tools #26](https://github.com/TeamFAPS/PSVita-RE-tools/issues/26) documenta reportes de problemas con VitaHBB/EasyVPK; [#7](https://github.com/TeamFAPS/PSVita-RE-tools/issues/7) muestra que un fallo de Ys VIII se corrigió tras probar otro binario, así que **no** es base para declarar conflictos universales.
- v-kaio: el [README de SKGleba/v-kaio](https://github.com/SKGleba/v-kaio#notes) indica explícitamente que sustituye funcionalidades de ioPlus. Es redundancia, no incompatibilidad comprobada.
- CapUnlocker: [GrapheneCt/CapUnlocker](https://github.com/GrapheneCt/CapUnlocker), evolución de CoreUnlocker80000H. El fallo de CapUnlocker 1.3 al volver de Adrenaline fue [corregido en 1.4](https://github.com/GrapheneCt/CapUnlocker/issues/3); **no** se muestra aviso contra Adrenaline por ese problema antiguo.
- Azahar requiere `azaharnative` + CapUnlocker: [Grarak/azahar](https://github.com/Grarak/azahar). Estos son **compatibles por diseño**.
- La documentación de COD WAW exige TTBR Remap para su port; no se halló una pareja conflictiva con suficiente respaldo para advertir indiscriminadamente.

Este catálogo de reglas no diagnostica fallos de kernel, ni prueba el estado de carga real del plugin. Los mensajes deben usar el término «posible».

## 4. Detección técnica y rendimiento

- `FullCatalogScreen::refreshPluginChoiceConflicts(const CatalogLink&)` resuelve el nombre del plugin a instalar y consulta reglas estrictamente limitadas a los enlaces de catálogo.
- `PluginDetector::activeKernelPluginBasenames()` lee `ur0:tai/config.txt` o `ur0:/tai/config.txt`, con política **ur0** idéntica al detector vigente. Reutiliza `parseConfigText()` y comprueba para cada `*KERNEL` que la línea esté activa y el archivo referenciado **exista físicamente**.
- Se ignoran comentarios, plugins listados bajo otras secciones, entradas `.suprx` y nombres duplicados. Se devuelven los **basenames normalizados a minúsculas**.
- La lectura se ejecuta al abrir la confirmación individual y cada vez que Install All pasa a otro candidato, **no por fotograma**. La pantalla representa la lista ya detectada.
- No se modifican `config.txt`, `*.skprx`, ni las decisiones de instalar, omitir, reinstalar o reiniciar.
- La ventana actual usa `drawPluginChoiceOverlay()`; el cuerpo admite un máximo de siete filas a la vez con desplazamiento por línea. Cabecera, pie y botones mantienen sus coordenadas actuales.

**Precaución:** «configurado» no prueba que el plugin esté ya cargado en RAM si aún no se reinició tras editar taiHEN. Un archivo suelto sin entrada activa no debe activar la advertencia.

## 5. Código modificado

- `Client PSVitaAlive/include/installer/plugin_detector.hpp` y `source/installer/plugin_detector.cpp`: nueva lectura puntual de nombres de plugins kernel activos.
- `Client PSVitaAlive/include/ui/full_catalog_screen.hpp`: estado del bloque de conflictos y desplazamiento.
- `Client PSVitaAlive/source/ui/full_catalog_screen.cpp`: búsqueda de coincidencias, presentación primero de la advertencia y desplazamiento del texto mediante botones y táctil; funcionamiento compartido con Install All.
- `Client PSVitaAlive/assets/lang/{en,es,fr,de,it,ru,pt-BR,pt-PT}.lang`: claves `PLUGIN_CONFLICT_HEADING`, `PLUGIN_CONFLICT_DETECTED`, `PLUGIN_CONFLICT_WARNING`, `PLUGIN_CONFLICT_SCROLL`.

No cambia el esquema de `apps/`, `authors/` o `categories/`. Los JSON generados por GitHub Actions siguen siendo la única API pública para cliente/web. **Nunca se editan manualmente.**

## 6. Mantenimiento de reglas

Para añadir **en el futuro** una incompatibilidad, primero registrar un enlace a evidencia primaria o un problema reproducible. Después modificar `refreshPluginChoiceConflicts()` con la combinación concreta. No advertir solo porque dos plugins tengan propósitos parecidos. Conservar localización y criterio no bloqueante; comprobar que el nombre realmente coincide con el basename utilizado en taiHEN.

Si aparece un catálogo declarativo futuro, deberá validarse y entregarse por el flujo oficial existente; **esta versión no crea un cuarto catálogo JSON ni realiza solicitudes nuevas a servidores**.

## 7. Validación y pendientes

- [x] PR limitado al cliente y los ocho `.lang`; sin archivos de datos `apps/` ni catálogos generados.
- [x] Revisión estática: aviso se dibuja antes de Requerido/Opcional; botones fijos; controles Arriba/Abajo y táctil; refresco para cada plugin en Install All.
- [x] GitHub Actions **Validate and Generate Local Catalog**, [ejecución 40523917409](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40523917409): **success**.
- [ ] GitHub Actions **Build PS Vita Client**, [ejecución 40523917855](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40523917855): **failure en enlazado de librerías libcurl/OpenSSL** (`undefined reference` a símbolos OpenSSL). Los archivos de esta función compilaron; la fase de enlace del proyecto completo no concluyó. `CMakeLists.txt` y el workflow no se modificaron en este PR. Revisar la compatibilidad de las bibliotecas VitaSDK por separado; no marcar este paso como exitoso.
- [ ] Probar en PS Vita real: solo `kuio`; ambos `kuio`/`v-kaio`; CoreUnlocker antiguo; ningún plugin; línea comentada; fichero ausente; nombre en distinta capitalización; instalador individual; Install All; reinstalación; modalidades táctil y cruceta; pantalla a 960 × 544; ocho idiomas.
- [ ] Confirmar que no hay superposición en fuentes alternativas de PS Vita y que el contenido no ejecuta acciones al arrastrar.

**Investigación posterior (27-10-2026):** se identificó que el workflow de CI sustituía OpenSSL 1.1.1 de la imagen VitaSDK por OpenSSL 1.0.2, provocando un fallo de enlace en libcurl independiente de estos cambios de UI. Se corrigió el workflow en `main` con [commit `d30c9a1`](https://github.com/VegettoSan/PSVitaAlive/commit/d30c9a1d7bdb5357db57a61aaa68ab9e0510afae). Ver [diagnóstico y validación](VITASDK_OPENSSL_LINKER_FIX_20261027.md). **Seguimiento resuelto (27-10-2026):** se corrigió además el orden de enlace de pthread/libstdc++ en [PR #60](https://github.com/VegettoSan/PSVitaAlive/pull/60); la [compilación oficial #508](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40532684582) terminó en **success** y publicó el VPK. Las pruebas de interfaz en hardware real siguen pendientes.

El resultado del workflow no demuestra por sí mismo que haya fallo en esta funcionalidad; tampoco permite declarar completada una compilación ni una prueba de hardware.

## 7.b Advertencia de recuperación durante reinicio (27 octubre 2026)

Es una modificación **de la interfaz de reinicio, no del detector de conflictos**:

- `drawPluginRebootOverlay()`: panel más grande, texto de recuperación con **L** en un recuadro destacado antes de `Reiniciar PS Vita`. Sigue siendo la misma ventana y se utiliza tras instalación individual o cola de esenciales. Touch coincide con geometría actualizada.
- `drawInstallAllSummary()`: conserva el resumen, sus botones y acciones de ZIP, pero utiliza `ALL_REPORT_REBOOT_2` para explicar el arranque sin plugins en caso de bootloop. Solo se muestra si realmente se instaló un plugin.
- La recomendación coincide con [AutoPlugin II](https://github.com/ONElua/AutoPlugin2/blob/master/AUTOPLUG2/lang/SPANISH.lua) y [Vita Hacks Guide](https://vita.hacks.guide/troubleshooting). Con Enso mantener **L** en el arranque; con h-encore mantenerla al ejecutar el exploit para omitir plugins. La acción es temporal, no borra la configuración.
- Texto localizado en ocho idiomas y ajustado al ancho del panel. No se modifica el catálogo, los plugins, ni la lógica de reinicio.

**Pendientes de hardware:** comprobar la visibilidad completa del texto, el área táctil del botón individual, el texto de Install All y distintas fuentes. Las pruebas en hardware y la compilación son validaciones independientes.

## 8. Reversión

El cambio puede revertirse como PR: no realiza migración de datos, no toca ni los binarios de plugins, ni `config.txt`, ni el instalador. Volver al diálogo previo solo elimina la advertencia visual y su lectura puntual.

**Relación:** [detección de plugins taiHEN](PLUGIN_DETECTION_CONFIG_20261023.md) · [sistema futuro de versiones](PLUGIN_UPDATES.md) · [UI](../Client%20PSVitaAlive/source/ui/README.md).
