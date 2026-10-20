# Modo de protección de pantalla (OLED/LCD)

## Objetivo

Reducir el tiempo que la interfaz de progreso permanece estática durante descargas, extracciones e instalaciones largas, especialmente en PS Vita 1000 con panel OLED, sin cambiar la política actual de mantener la consola y la pantalla encendidas mientras existe un trabajo activo.

Este sistema es **solo de presentación e input**. No modifica libcurl, el instalador, el extractor, las colas de instalación, los bloqueos del botón PS ni los `sceKernelPowerTick` existentes.

## Comportamiento

- La ventana normal de progreso no se mueve.
- El temporizador se inicia al entrar en una fase activa reconocida como `Downloading`, extracción o instalación.
- Tras **60 segundos** en la misma fase aparece el modo de protección.
- Al cambiar de fase (por ejemplo, Descargando → Extrayendo → Instalando) se restaura inmediatamente la interfaz normal y empieza un nuevo temporizador de 60 segundos.
- Al finalizar, fallar o cancelar el trabajo, el modo de protección se desactiva inmediatamente y no puede volver a activarse hasta que exista otra fase activa compatible.
- Si el usuario sale manualmente del protector y la misma fase continúa, se concede un nuevo periodo normal de 60 segundos antes de poder entrar de nuevo.

## Presentación

- Resolución objetivo de PS Vita: **960 × 544**.
- Fondo del protector: **negro puro** (`#000000`) siempre, independientemente del tema.
- Bloque lógico de contenido: **430 × 150 px**. No se dibuja panel ni borde alrededor del bloque.
- El bloque mantiene un margen de seguridad de 16 px respecto a los bordes de la pantalla.
- El bloque muestra únicamente:
  - `PSVitaAlive`;
  - fase actual localizada: Descargando / Extrayendo / Instalando;
  - porcentaje;
  - ETA;
  - icono y porcentaje de batería de la consola;
  - mensaje localizado para volver a la interfaz normal.
- Los colores del texto usan la paleta activa (`ACCENT`, `TEXT`, `DIM`).
- El render usa `UiFont`, por lo que respeta la fuente/estilo y la escala seleccionados por el usuario.

## Indicador de batería en la cabecera y en el protector (PR #41)

**Estado:** implementado en [PR #41](https://github.com/VegettoSan/PSVitaAlive/pull/41) y validado visual y funcionalmente por el responsable del proyecto en una **PS Vita real el 2026-10-20**, incluyendo el centrado de la cabecera y su presencia en el protector.

### Ubicación y distribución

- La cabecera muestra icono y porcentaje **entre □ Filtros y el reloj**. `FILTER_CHIP_W = 192` conserva el ancho del botón y `HEADER_STATUS_RESERVE = 188` reserva espacio para batería/reloj, reduciendo el buscador.
- Con filtros visibles, el espacio disponible empieza en `gdX + gdW`; con filtros ocultos, en `barX + barW` (borde del buscador expandido). Termina en `clockX`, cuya posición **no cambia**.
- `batteryCenterX = statusLeft + (clockX - statusLeft) / 2`. `drawBatteryIndicator(..., centered=true)` mide con `uiTextWidth()` todo el bloque icono + porcentaje; sigue centrado cuando cambia la cantidad de cifras.
- Dibujo y **hitboxes táctiles** comparten las constantes geométricas. El indicador no captura entradas.
- En protección OLED se dibuja el **mismo indicador**, centrado en el bloque móvil de **430 × 150 px**, debajo del porcentaje/ETA de la tarea y por encima del mensaje para despertar. El porcentaje de batería es distinto del porcentaje de descarga.

### Datos, estados y coste

- `batteryDisplayState()` usa `scePowerGetBatteryLifePercent()` y `scePowerIsBatteryCharging()` de VitaSDK (`<psp2/power.h>`).
- Ambos lugares **comparten una lectura cacheada durante 15 segundos** (sin llamadas por fotograma ni hilos adicionales).
- Lecturas válidas: **0–100 %**; si la API devuelve un valor fuera de rango, se presenta `--%` en vez de interpretar una batería vacía.
- El icono es geométrico, dibujado con `vita2d_draw_rectangle`, sin nuevas texturas, glifos ni traducciones. Usa `ACCENT` normalmente, **rojo con 15 % o menos** y una cruz blanca cuando el sistema indica carga.
- Se trata solo de información. **No cambia** el instalador, libcurl, las descargas, los bloqueos del botón PS ni `sceKernelPowerTick`/suspensión.

Código: `Client PSVitaAlive/source/ui/full_catalog_screen.cpp` → `batteryDisplayState()`, `drawBatteryIndicator()`, `FullCatalogScreen::drawHeader()` y la vista de protección en `drawInstallOverlay()`.

**Validación:** GitHub Actions **Build PS Vita Client** [#160](https://github.com/VegettoSan/PSVitaAlive/actions/runs/39464007392) y **Validate and Generate Local Catalog** [#2345](https://github.com/VegettoSan/PSVitaAlive/actions/runs/39464007535) completados con éxito en la versión probada, más prueba manual positiva en consola real. Una nueva compilación tras documentar o fusionar debe comprobarse por separado.

### Pruebas de regresión

1. En catálogo completo, filtros visibles: porcentaje centrado, botón de **192 px** y búsqueda táctil intactos.
2. En detalle, ajustes, modales y otras pantallas donde se oculta □ Filtros: el indicador se recentra entre buscador y reloj, sin solaparse.
3. Con porcentajes de una, dos y tres cifras, carga conectada y batería baja: revisar centrado, color y símbolo.
4. Tras 60 s de descarga/extracción/instalación, revisar el indicador dentro del protector OLED y después del movimiento del bloque a los 120 s.
5. Repetir con otros temas, fuentes e idiomas. Ante lecturas no válidas (por ejemplo, en algunos emuladores), comprobar `--%`.
6. Confirmar que no cambian los bloqueos del sistema, el flujo de instalación, el toque ni la salida del protector.

## Movimiento anti-retención

Cada **120 segundos** mientras el modo de protección sigue activo, el bloque se reposiciona de forma pseudoaleatoria dentro del área segura. La selección intenta evitar posiciones demasiado próximas a la anterior (140 px en X o 80 px en Y como distancia mínima útil), manteniendo siempre los 430 × 150 px completamente visibles.

## Input

Cualquier pulsación nueva de un botón que llegue al cliente cierra el modo de protección y **consume esa pulsación**. Esto es importante para que Círculo no cancele accidentalmente una descarga al usarse para despertar la interfaz.

El panel táctil frontal también puede despertar la interfaz. El toque se consume hasta que el dedo se levanta para impedir que llegue a los controles inferiores.

## Multidioma

Los nombres de fase reutilizan los `TextId` existentes (`StageDownloading`, `StageExtracting`, `StageInstalling`). El mensaje del protector utiliza la clave:

```text
PROTECTION_PRESS_ANY_BUTTON
```

La clave se incluye en los paquetes `en`, `es`, `fr`, `de`, `it`, `pt-BR`, `pt-PT` y `ru`.

## Implementación

La lógica vive en `Client PSVitaAlive/source/ui/full_catalog_screen.cpp` para reutilizar el estado de progreso, tema, fuente, localización e input ya existentes sin introducir un segundo sistema de descarga o instalación.

Los estados principales son:

```text
Fase activa
   ↓
60 s en la misma fase
   ↓
Modo protección
   ├─ botón/toque → UI normal + nuevo margen de 60 s
   ├─ 120 s → nueva posición segura
   ├─ cambio de fase → UI normal + nuevo temporizador
   └─ fin/error/cancelación → UI/resultados normal, protector desactivado
```

## Pruebas recomendadas en Vita real

1. Descargar un archivo durante menos de 60 s: el protector no debe aparecer.
2. Mantener una descarga más de 60 s: debe aparecer con fondo negro, porcentaje y ETA.
3. Mantener el protector más de 120 s: el bloque debe cambiar de posición sin recortarse.
4. Pulsar Círculo mientras el protector está activo: debe volver a la UI y **no** cancelar con esa primera pulsación.
5. Repetir con X, Triángulo, D-Pad y touch.
6. Verificar una extracción larga y una instalación larga.
7. Verificar una secuencia Install All: cada cambio de fase debe volver a la UI normal y reiniciar el minuto.
8. Completar, cancelar y forzar un error: el protector no debe reaparecer después del resultado.
9. Probar varios temas: el fondo debe seguir negro y solo deben cambiar los colores de texto.
10. Probar varias fuentes, escalas e idiomas, comprobando que las líneas siguen dentro del bloque de 430 × 150.
11. Confirmar que la consola continúa con las protecciones actuales de suspensión/apagado durante todo el trabajo.

## Nota

El modo reduce contenido estático y áreas iluminadas, pero no pretende garantizar la eliminación absoluta de retención o desgaste del panel. Su objetivo es minimizar de forma práctica el riesgo durante operaciones largas sin alterar el funcionamiento del instalador.


## Mascotas del protector

El modo de protección admite ahora mascotas animadas mediante `MascotManager`. Esta integración es exclusivamente visual y no modifica libcurl, el instalador, extractores, colas, bloqueos del botón PS ni `sceKernelPowerTick`.

Fuentes combinadas:

```text
app0:mascots/
ux0:data/psvitaalive/mascots/
```

El valor predeterminado de Settings es `Random`: cada entrada real al protector selecciona una mascota válida de cualquiera de las dos fuentes. El usuario también puede elegir `Off` o una mascota concreta.

Las texturas se cargan únicamente para la sesión activa del protector y se liberan al salir. La carga/liberación se atiende en el hilo principal de UI antes del dibujo, nunca desde workers de red/instalación.

El área lógica de movimiento continúa siendo 100 × 100 con margen seguro de 16 px. Los PNG físicos pueden tener otras dimensiones y se ajustan dentro de esa caja conservando la relación de aspecto.

La especificación completa, instalación de mascotas de usuario, límites y pruebas está en [`MASCOTS.md`](MASCOTS.md).
