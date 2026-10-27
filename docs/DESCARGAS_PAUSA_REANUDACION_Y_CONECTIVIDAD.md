# Descargas de PSVitaAlive: pausa, reanudación y recuperación de conectividad

**Estado:** integrado en el cliente después de la validación del mantenedor en una PS Vita real (26 de octubre de 2026).  
**Alcance:** descargas HTTP dentro del cliente; no sustituye BGDL del sistema para PKG oficiales.

## Para usuarios

- Durante una descarga gestionada por PSVitaAlive, pulsa **CUADRADO** o el botón **Pausar** de la pantalla táctil. El porcentaje queda guardado junto con los datos válidos.
- Con el trabajo **Pausado**, pulsa **CUADRADO** o **Reanudar** para continuar desde las partes guardadas. El servidor y la versión del archivo se comprueban antes de reutilizar esos datos.
- **CÍRCULO** cancela y descarta los archivos temporales del trabajo. Pausar **no** equivale a cancelar.
- Si se pierde la conexión Wi-Fi mientras descargas, el cliente intenta detener el trabajo con seguridad y **mantener el progreso validado**. Vuelve a conectarte y pulsa **Reanudar**.
- Si intentas iniciar una descarga con la consola desconectada, PSVitaAlive muestra un aviso **antes de abrir el selector de destino o el asistente**. Cuando la consola se está conectando, pide que esperes a que la red esté preparada.
- Si hay conexión a una red pero no llega respuesta HTTP, se advierte que **Internet o la fuente de descarga podrían no estar disponibles**; no se atribuye sin prueba el problema a Archive.org, MediaFire ni a ningún proveedor.
- Los archivos incompletos se conservan **únicamente durante la sesión abierta de PSVitaAlive**. Cerrar la aplicación o reiniciar la consola permite que la limpieza del siguiente inicio los elimine. No se promete reanudación entre sesiones.
- La protección OLED conserva el primer toque para despertar el panel; para pausar, pulsa de nuevo el botón visible en la ventana de descarga.

### Mensajes generales

| Estado | Qué significa | Qué hacer |
| --- | --- | --- |
| Wi-Fi apagado o desconectado | La consola no tiene un enlace de red utilizable | Activar Wi-Fi y conectarse |
| Conectando a la red | La conexión está en curso | Esperar a la asociación y volver a intentar |
| Descarga guardada; Wi-Fi desconectado | Se detectó pérdida de enlace y se conserva el trabajo | Reconectar y pulsar Reanudar |
| Descarga guardada; fuente inaccesible | La red puede estar activa, pero una petición falló | Comprobar Internet, probar Reanudar |
| Verificando archivo y partes | El cliente está comprobando identidad y rangos | No cerrar el cliente |

La API VitaSDK `sceNetCtlInetGetState` expone el estado de conexión, **no prueba si el interruptor Wi-Fi está físicamente apagado** ni certifica acceso a Internet. Por eso estos mensajes explican también la posibilidad de estar desconectado, sin afirmar una causa no verificable.

## Arquitectura de la implementación

```text
Interfaz (CUADRADO o toque) / Monitor NetCtl
   → InstallController
   → DownloadManager: trabajo + pauseRequested
   → HttpClient + libcurl
       ├── serial: payload.part + Range de 64 bits + If-Range
       └── 2/4 rangos: partes + payload.part.range.resume
               ↓
        nueva petición 206: Content-Range + tamaño +
        validador (ETag / Last-Modified) + offsets
               ↓
        continuar solo bytes pendientes / abortar reutilización insegura
```

- `Client PSVitaAlive/include/network/connectivity_state.hpp` clasifica **Ready**, **Connecting**, **Disconnected** y **Unknown**; un estado desconocido no se trata automáticamente como Wi-Fi apagado (compatibilidad Vita3K/NetCtl).
- `source/main.cpp` y la UI bloquean nuevas descargas o asistentes cuando la red está desconectada o conectándose, antes de mostrar los pasos de instalación. Las acciones sobre ZIP ya descargados pueden seguir sin internet.
- `DownloadManager` consulta el enlace periódicamente (hasta dos veces por segundo) desde el callback cooperativo de transferencia; **no introduce un hilo de vigilancia** ni prueba continuamente proveedores externos.
- Al desaparecer el enlace se solicita pausa, se vacían buffers y se preservan únicamente las partes verificables. Si se agotan los intentos paralelos pero el checkpoint es válido, se devuelve `NetworkInterrupted` sin eliminarlo.
- `HttpClient` no inicia una descarga serial que sobreescriba partes previas cuando no puede completar la verificación 206. La reanudación valida de nuevo cada parte; nunca concatena versiones diferentes del mismo archivo.
- Archive.org puede reutilizar temporalmente el nodo de almacenamiento anterior **solo dentro del trabajo actual**, con una nueva prueba de 206. Ante fallo usa las rutas de respaldo existentes; este atajo no se habilita genéricamente para URL HTTP de terceros.
- El cálculo de velocidad por sesión excluye bytes conservados antes de reanudar y suaviza el resultado; no hay un supuesto “aumento de velocidad” por reanudar.
- El sistema de idiomas usa archivos `assets/lang/*.lang`; los mensajes visibles son independientes del proveedor. Los diagnósticos detallados permanecen en `session.log`.

## Garantías y restricciones

- La verificación HTTP 206, `Content-Range`, los límites de tamaño, el hash de identidad del checkpoint, los validadores y las comprobaciones de espacio siguen siendo obligatorios.
- Un validador ausente, cambiado o inconsistente puede obligar a **descartar los segmentos inseguros**, en lugar de reutilizarlos. Conservar **datos verificados** tiene prioridad sobre fingir progreso.
- El sistema puede mostrar una pausa que requiere **reanudación manual**. No se promete reconexión automática a Internet ni que todas las fuentes admitan HTTP Range.
- La pausa del descargador HTTP no desbloquea el botón PS, el menú de energía, las instalaciones/extracciones en marcha, BGDL ni la actualización del propio cliente. Se mantiene la seguridad y política OLED.
- Los archivos `apps/`, `authors/`, `categories/`, catálogos generados y la web no forman parte de esta modificación. La arquitectura oficial de GitHub Actions continúa intacta.

## Pruebas y mantenimiento

El mantenedor informó de pruebas satisfactorias en PS Vita real para el botón táctil y CUADRADO, reanudaciones múltiples, extracción de un archivo grande y pérdida/restablecimiento de Wi-Fi. GitHub Actions validó compilación VitaSDK, HTTP 206, rangos 2/4, cálculo de velocidad, traducciones y catálogo. Esto **no** equivale a afirmar que se probaron todas las redes, servidores, paquetes o escenarios de fallo.

Para regresión: iniciar sin Wi-Fi; probar Wi-Fi conectándose; cortar el enlace cerca del 30–40%, restablecer y reanudar; aislar la salida a Internet manteniendo el router Wi-Fi; cancelar; verificar que no hay bytes duplicados; probar cambios de ETag y HTTP 404/416; extraer ZIP/VPK; probar MediaFire, Archive.org y HTTPS genérico; verificar protección OLED, botones táctiles y controles físicos. Ante anomalías conservar el `session.log`.

Referencias:
- [Detalles de HTTP 206, pausas y pruebas](HTTP206_PAUSE_RESUME_TEST_20261026.md)
- [Diagnóstico y estados de red](HTTP206_WIFI_INTERRUPTION_RECOVERY_20261026.md)
- [Guía de mensajes independientes de la fuente](UI_PROVIDER_NEUTRAL_MESSAGING.md)
- [PR de validación #57](https://github.com/VegettoSan/PSVitaAlive/pull/57)
