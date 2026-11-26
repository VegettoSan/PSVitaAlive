# Entrada del mando: acciones únicas y navegación continua (2026-11-26)

## Decisión vigente

Una pulsación física nueva ejecuta una acción. Mantener X, círculo, cuadrado, triángulo, L/R, START o SELECT no debe repetirla ni volver a ejecutarla al cambiar de pantalla. La cruceta y el joystick izquierdo **conservan** la navegación continua ya aprobada; no aplicar la regla de acciones a la repetición direccional. Los atajos combinados existentes y el manejo táctil se conservan.

## Causa y solución

El catálogo, almacenamiento UX0 y datos instalados tenían historiales de botones independientes. Un botón todavía presionado podía convertirse en un flanco nuevo al cambiar de contexto (abrir/cerrar varias ventanas o saltar una confirmación). No resolver mediante retardos por ventana.

- `include/ui/controller_button_edges.hpp`: historial de 32 bits, 4 bytes, sin asignaciones; calcula `pressed = buttons & ~previous` y conserva el estado si falla la lectura.
- `include/ui/controller_input.hpp`: instancia compartida del historial en el hilo UI; `readClientControllerActions` obtiene los flancos de los botones físicos. `discardClientControllerActions` consume muestras en transiciones donde no se deben ejecutar acciones. La protección de START durante el arranque se mantiene.
- `source/ui/full_catalog_screen.cpp`: catálogo, ajustes y diálogos, almacenamiento, datos instalados y cola comparten los flancos. La cola recibe las acciones ya calculadas, sin historial local. Se conservan las barreras de liberación y propiedad modal.
- `source/ui/search_keyboard.cpp` y `source/main.cpp`: teclado QWERTY y selector de destino ZIP comparten el mismo historial. Square o X sobre DEL borran un carácter por pulsación; mantener DEL táctil conserva su repetición original.

## Navegación preservada

| Contexto | Retardo inicial | Intervalo de repetición |
| --- | --- | --- |
| Catálogo y navegación general | 320 ms | 420 ms |
| Almacenamiento y datos instalados | 330 ms | 130 ms |
| Cola, historial y recuperación | 330 ms | 130 ms |
| Teclado | 290 ms | 115 ms |

Se conservan las zonas de foco, requisitos de pulsación nueva entre zonas, desplazamientos por página y zona muerta analógica (64/192). Los diálogos que antes no repetían direcciones conservan esa política. La repetición se calcula exclusivamente con máscaras direccionales; nunca debe convertirse en una segunda confirmación. Los retardos de navegación son los preexistentes, no un debounce nuevo para acciones.

## Validación

`tests/test_controller_button_edges.cpp` cubre los 32 bits, pulsación sostenida, liberación y nueva pulsación, lectura fallida, botones combinados y tamaño de estado. Se añade al workflow `client-build.yml`. El 26 de noviembre se compiló y ejecutó localmente una reproducción equivalente de esta prueba con C++17, -Wall -Wextra -Werror: correcta. Esto verifica el algoritmo, no sustituye la compilación VitaSDK ni las pruebas de integración en consola.

Pruebas de hardware pendientes de confirmación explícita para esta revisión:

- Mantener X al abrir el administrador UX0, entrar en datos y abrir detalle; nunca confirmar una eliminación involuntariamente.
- Mantener círculo al volver de confirmación, detalle, listado y administrador: un nivel por pulsación.
- Repetir pulsaciones rápidas y combinar navegación con acciones; verificar los atajos existentes.
- Mantener D-pad y stick en catálogo, cola, almacenamiento y teclado: misma navegación que la versión anterior.
- Probar teclado, selector ZIP, diálogos de plugins, recuperación de ZIP y retorno desde la cola.
- Comprobar arranque/START, salida del protector OLED, carga del catálogo y navegación táctil.

No cambia la lógica de instalación/descarga, esquema de catálogo, versión ni publicación de release. Las notas históricas que describan repetición de Square físico quedan sustituidas por esta decisión.
