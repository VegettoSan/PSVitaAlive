# PSVitaAlive: recuperación segura de descargas al perder conectividad

Estado: **aprobado para `main` por el mantenedor el 26 de octubre de 2026**, tras pruebas satisfactorias en PS Vita real. Este documento permanece como referencia técnica de implementación y casos de regresión.

## Problema reproducido

Durante un archivo de ~243.93 MiB, al apagar Wi-Fi a ~40%, cuatro rangos HTTP 206 recibieron errores de transporte/DNS. Los reintentos terminaron eliminando los segmentos y el cliente mostró un aviso que atribuía el fallo a Archive.org. Esta clasificación no estaba demostrada: una caída local puede tener el mismo síntoma.

## Estados observables sin inventar certezas

| Observación | Interfaz | Acción |
| --- | --- | --- |
| NetCtl desconectado | Wi-Fi apagado **o** red desconectada | Avisar antes de abrir instalador; durante transferencia detener sin eliminar datos |
| NetCtl conectando/finalizando | Conectando a la red | Avisar antes de iniciar o reanudar |
| NetCtl conectado, HTTP sin respuesta | Hay red, pero no fue posible acceder a Internet **o** a la fuente | No afirmar que todo Internet o un proveedor esté caído |
| Respuesta 5xx real | Problema temporal de la fuente | Mensaje genérico del servidor de origen |
| HTTP 404/410 | Archivo no disponible | Mantener clasificación específica del recurso |

`sceNetCtlInetGetState` no distingue la preferencia de encendido de la radio Wi-Fi del resto de causas de desconexión ni confirma conectividad completa a Internet. Se evita hacer consultas externas adicionales en cada fotograma o usar una URL de proveedor como prueba universal de Internet. Los nombres concretos quedan en `session.log`.

## Protocolo de descarga

1. Preflight de red en interfaz antes de cualquier selector ZIP, enlace descargable o asistente Install All. Un archivo ZIP local existente conserva su flujo offline.
2. `DownloadManager` revisa NetCtl con intervalo de 500 ms en el *callback* de cancelación de libcurl (sin I/O de red ni archivos en el callback).
3. Si desaparece el enlace, solicita la misma pausa cooperativa de descarga ya probada. Un rango con validador y prefijo íntegro se guarda en `.range.resume`.
4. Si los reintentos de rangos se agotan por un fallo de transporte pero el checkpoint sigue siendo seguro, `NetworkInterrupted` regresa **sin borrar** los rangos. `DownloadManager` lo muestra como pausa por conexión.
5. Si no se puede validar por HTTP 206 una transferencia parcial en una reanudación, se conserva el checkpoint y no se abre un GET serial que pueda sobreescribirlo.
6. El botón Reanudar comprueba NetCtl; al volver la red, el HTTP 206 vuelve a validar tamaño, ETag/Last-Modified, identidad de contenido y posiciones de cada rango.
7. Los archivos parcialmente descargados se mantienen **solo durante esta sesión**; el saneamiento al iniciar la aplicación sigue eliminando trabajos incompletos abandonados. No se ha implementado persistencia a través de reinicio.

## Casos de regresión y validación de futuras versiones

- Iniciar descarga sin Wi-Fi: notificación antes de mostrar el instalador.
- Wi-Fi conectando: aviso temporal sin iniciar trabajo nuevo.
- Cortar Wi-Fi al 30–40%, volver a conectarlo y pulsar Reanudar: no borrar ni duplicar bytes; progreso y extracción correctos.
- Mantener Wi-Fi encendido, pero quitar Internet al router: distinguir red enlazada de acceso al servidor no verificado; conservar checkpoint válido.
- Fuente devuelve 404, 416 incompatible, ETag cambiado o partes corruptas: aplicar las reglas de seguridad antiguas, sin ensamblar contenido ambiguo.
- Cancelar descarga: eliminar las partes tal como hacía el comportamiento anterior.
- Verificar espacio disponible, pausa táctil, protección OLED, ZIP/VPK, PKG y fallback serial.
- Ejecutar CI VitaSDK y pruebas de traducción; confirmar los casos de red con la consola física antes de `main`.
