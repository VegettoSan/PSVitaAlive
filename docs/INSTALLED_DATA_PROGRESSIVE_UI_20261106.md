# PSVitaAlive — Exploración gradual y resultados de desinstalación

**Revisión:** 6 de noviembre de 2026. Primer test de VitaSDK antes de comprobar con consola real.

## Reporte del usuario

- El cálculo de tamaños individuales ya es razonablemente rápido en consola. Se pide hacerlo automáticamente **una aplicación a la vez**, sin bloquear la navegación.
- Varias aplicaciones aparecían con tamaño de Game Files `0 B` sin tener datos externos.
- Los botones del detalle eran excesivos y visualmente confusos.
- Al borrar datos el indicador no decía qué se estaba borrando ni su avance.
- La desinstalación de VPK volvía silenciosamente a la ficha, dando la impresión de no haberse ejecutado.

## Arquitectura y respuesta

Se conserva `apps/ + authors/ + categories/ -> GitHub Actions -> catalog.json + authors.json + categories.json -> cliente/web`. `links[].installed_paths` es el origen de las rutas. La caché persistente local **nunca autoriza borrados**.

### Navegación y medición

1. Al abrir el administrador se descubren las raíces con `sceIoGetstat`, **sin escaneo recursivo automático en el primer paso**.
2. La interfaz muestra inmediatamente las aplicaciones instaladas. También incluye Homebrew sin enlaces Game/Data Files, etiquetados **Solo APP** en vez de mostrar un supuesto tamaño externo cero.
3. Durante la navegación, un trabajador procesa **una aplicación por vez**. Prioriza la aplicación seleccionada y luego recorre las restantes; omite las que no tienen carpetas externas o ya cuentan con una medición íntegra de la misma sesión/caché.
4. Cada intento de medición tiene un presupuesto cooperativo de 8,5 segundos. No se repite automáticamente un intento incompleto en la misma sesión. Un archivo grande o una operación de I/O bloqueante puede superar el presupuesto.
5. El trabajador no modifica el `InstalledDataSnapshot` que dibuja la interfaz. Publica el resultado individual terminado y la UI lo integra al recibir la señal atómica. Si se abre la ficha durante la medición, se puede navegar, pero **no se permite borrar concurrentemente** hasta que la medición finalice.
6. El usuario puede cerrar el administrador mientras se mide: se solicita cancelación cooperativa, se espera a que el trabajador termine y se cierra sin dejar un hilo accediendo a memoria liberada.
7. Las mediciones íntegras se guardan en `ux0:data/psvitaalive/cache/installed_directory_index.tsv`. Una medición histórica permanece etiquetada como tal; `st_mtime` de una carpeta raíz **no** detecta obligatoriamente cambios en subcarpetas. **Cuadrado** o el control táctil secundario permiten recalcular el tamaño manualmente.
8. Un escaneo parcial no se guarda como un resultado completo.

### Acciones y seguridad

El detalle presenta **exactamente tres botones destructivos**:

- **Borrar APP:** desinstala mediante `scePromoterUtilityDeletePkg`, no borra `ux0:data`.
- **Borrar Game Files:** borra solo los datos externos declarados y validados en `installed_paths`, no toca `ux0:app`.
- **Borrar todo:** ejecuta ambas operaciones por etapas, conservando la protección de rutas compartidas, títulos propios y confirmación informada.

Los controles de medición y de retorno no compiten visualmente con los tres botones.

### Progreso legítimo

- **Borrado de datos externos:** se muestran **raíces terminadas / raíces totales** y el porcentaje derivado exclusivamente de esas raíces. También se actualizan contadores de archivos y directorios realmente eliminados. En una única raíz grande el porcentaje puede permanecer en cero mientras avanzan los contadores: no representa porcentaje de bytes.
- **Desinstalación del VPK:** PromoterUtility no ofrece porcentaje fiable de archivos; se muestra la etapa **Desinstalando APP**, barra animada indeterminada y el estado de espera del gestor de paquetes. No se inventa un porcentaje.
- **Borrar todo:** cambia el mensaje entre desinstalación y eliminación de Game Files; se ven los avances individuales.
- Las operaciones destructivas no se pueden cancelar a mitad de ejecución.

### Resultado persistente

Tras esperar al trabajador, la interfaz muestra un resultado que **permanece hasta pulsar X o Círculo**:

- APP desinstalada correctamente o fallo.
- Game Files eliminados correctamente, fallo, o no procesados por un fallo previo.
- Resultado completo, parcial o fallido.

Después de cerrar el resultado se reconstruye la lista visible; una APP sin Game Files desaparece del listado de instaladas, una APP eliminada que conserva datos pasa a **Solo Game Files**, y una APP con datos eliminados pero VPK intacto pasa a **Solo APP**. Si se abrió desde una ficha de catálogo, se vuelve a ella y se invalida el estado de instalación, sin dejar una ventana vieja de desinstalación delante.

### Pruebas

- Host: validación de rutas y aritmética real de progreso por raíces; compilación VitaSDK con las ocho traducciones.
- Consola: comprobar navegación durante medición, prioridades, cierre solicitado durante un trabajo, regeneración del índice, casos Solo APP/Solo Game Files, controles táctiles y de PS TV.
- Operaciones destructivas: probar primero con Homebrew temporal sin partidas; respaldar `ux0:data` y saves mediante VitaShell; comprobar resultado de PromoterUtility y confirmaciones, incluyendo fallo parcial y carpetas compartidas.

**Limitación:** el progreso de la desinstalación del paquete no es un porcentaje de bytes porque la API de PS Vita no ofrece ese dato. Los porcentajes de carpetas para archivos externos también reflejan únicamente raíces finalizadas, no una estimación de segundos.
