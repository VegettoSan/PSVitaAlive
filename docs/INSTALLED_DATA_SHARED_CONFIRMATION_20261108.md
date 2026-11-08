# Administrador de Game/Data Files: carpetas compartidas y detecciones ambiguas

Fecha: 8 de noviembre de 2026

## Alcance

Solo modifica el administrador de datos externos del cliente PS Vita. No cambia el esquema
de apps/authors/categories, los catálogos generados, el instalador, ni la web.

## Casos corregidos

- Las declaraciones de dos Homebrew sobre una misma carpeta no demuestran por sí solas
  que haya dos VPK instalados. Una carpeta válida y encontrada puede borrarse
  voluntariamente incluso si la aplicación correspondiente ya no está instalada.
- Los enlaces alternativos de Game Files que enumeran las mismas carpetas en distinto
  orden se consideran equivalentes. La falta de installed_paths en un espejo no anula
  los datos válidos declarados por otro enlace. El extract_path puede apuntar a la
  carpeta raíz directamente, además de hacerlo a una subcarpeta.
- Si existen variantes de destino diferentes y solo una raíz declarada está presente,
  se admite la operación. Si existen varias raíces alternativas simultáneamente,
  se conserva la protección por ambigüedad.

## Carpeta compartida con otro Homebrew instalado

- Ejemplo real: ux0:data/smworld es declarado por Super Mario World Vita y dos
  versiones de Super Mario All-Stars. La sola coincidencia no bloquea.
- Si otra aplicación realmente instalada utiliza la misma ruta y el Homebrew
  seleccionado también está instalado, el usuario puede eliminarla mediante la
  confirmación explícita del administrador. Se muestra un mensaje destacado
  advirtiendo que otro Homebrew puede perder archivos o partidas.
- Si el Homebrew seleccionado no está instalado pero otro instalado reclama
  esa carpeta, el borrado desde la fila huérfana sigue bloqueado.
- La confirmación no autoriza rutas desconocidas o peligrosas, ni carpetas de
  sistema, ni rutas de PSVitaAlive. La eliminación opera exclusivamente sobre
  raíces declaradas, presentes y validadas bajo ux0:data/.
- La verificación se repite antes de contar archivos y justo antes de borrar.
  En Borrar todo, la APP se desinstala únicamente mediante PromoterUtility y
  la autorización se conserva hasta concluir la misma operación.
- Al eliminar carpetas compartidas se invalidan los tamaños de caché de los
  títulos solapados y se actualiza la lista de presencia mediante Getstat.

## Pruebas y límites

- La prueba de política cubre huérfanos, carpetas solapadas, consentimiento
  ausente/presente, rutas protegidas, alternativas y extract_path.
- La compilación VitaSDK verifica la integración; se requiere aún una prueba
  de borrado real en PS Vita o Vita3K, preferiblemente con datos ficticios.
- Copia antes los datos con VitaShell. No existe borrado por archivo ni
  restauración automática: eliminar una carpeta compartida puede impedir
  el funcionamiento de otros Homebrew hasta reinstalar sus Game Files.
