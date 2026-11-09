# PSVitaAlive: legibilidad del Administrador de espacio y borrado compartido con advertencia

Fecha: 9 de noviembre de 2026.

## Cambio solicitado

- El Administrador de espacio de PSVitaAlive y el inspector de Game/Data Files presentan
  textos considerablemente mayores. Listas, detalle, progreso, resultados, botones
  y confirmaciones usan fuentes mayores que antes (antes había textos a escala 0.58
  a 0.74; ahora la mayoría es 0.81 a 1.14). Los nombres/rutas largos se acotan
  horizontalmente para no pisar los tamaños ni los botones.
- La lista del Administrador principal activa el recorte gráfico de vita2d
  antes de fijar su viewport; antes se fijaba el rectángulo sin habilitarlo.
- Que varias entradas del **catálogo** declaren la misma carpeta ya no impide
  borrarla, independientemente del estado del VPK de la entrada seleccionada.
- El borrado externo siempre requiere la confirmación existente. Cuando hay
  coincidencia de carpetas, se muestra una advertencia adicional con **todas**
  las aplicaciones que declaran la raíz, indicando nombre, Title ID y si el
  VPK está presente. La lista de tres entradas visibles puede desplazarse con
  cruceta o táctil hasta mostrar todas.
- Ejemplo: ux0:data/smworld es declarada por Super Mario World Vita y
  múltiples versiones de Super Mario All-Stars. Un VPK ausente no es motivo
  para bloquear el borrado de datos. Un VPK de otra entrada presente tampoco
  bloquea; tras la confirmación, el usuario acepta el posible impacto sobre
  otras instalaciones.
- La coincidencia se calcula a partir de rutas **declaradas** en el catálogo.
  No prueba qué aplicación creó cada archivo. Se comunica como referencia
  de catálogo, no como certeza de propiedad de datos.

## Salvaguardas inalteradas

Solo se borran rutas existentes declaradas y verificadas bajo ux0:data/.
Se siguen rechazando rutas no resueltas, metadatos ambiguos, directorios de
PSVitaAlive, rutas protegidas del sistema y rutas inválidas. Se mantiene
el conteo previo completo y se revalida antes de borrar. Desinstalar el VPK
usa PromoterUtility y nunca borra recursivamente ux0:app. Se invalida la
caché de todas las referencias solapadas tras borrar datos.

Nunca se modifican manualmente los catálogos generados. La arquitectura
apps/ + authors/ + categories/ -> GitHub Actions -> catálogos permanece intacta.

## Verificación

- Pruebas puras: coincidencias exactas y padre/hijo, coincidencias por prefijo
  falso, apps instaladas/no instaladas, aprobación de confirmación, rutas
  protegidas y resultados con múltiples apps relacionadas.
- Prueba de UI: fuentes mínimas, presencia de lista y scroll, coherencia
  entre hitboxes táctiles y botones, activación del recorte.
- Compilación VitaSDK y VPK de prueba por GitHub Actions.
- **Pendiente en PS Vita física:** lectura de todos los textos con los estilos
  de fuente configurables; prueba de ux0:data/smworld con datos ficticios
  y backup previo mediante VitaShell, tanto desde app instalada como huérfana.

No es posible revertir automáticamente el borrado de datos compartidos.
