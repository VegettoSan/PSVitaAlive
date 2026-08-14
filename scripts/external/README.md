# `scripts/external/` — Motor de catálogos externos

Este directorio contiene la lógica que lee fuentes externas, convierte sus registros al modelo interno de PS Vita Alive Store, agrupa aplicaciones equivalentes, resuelve autores y aplica la lógica de merge.

## Modelo interno

Los lectores externos convierten sus datos a `Candidate`. El objetivo es que el resto del pipeline no tenga que conocer detalles específicos de cada catálogo.

```text
VitaDB
NeoVitaDB
VitaDBtoo
   ↓
adaptador de fuente
   ↓
Candidate
   ↓
group_candidates()
   ↓
merge_group()
   ↓
app canónica PS Vita Alive Store
```

## Componentes principales

- `sources.py`: adquisición y normalización de fuentes compatibles, incluyendo VitaDB.
- `aggregate.py`: carga local, obtiene candidatos externos, deduplica, fusiona, resuelve autores, categorías y persistencia.
- `identity.py`: identidad/canonicalización de autores y comparación de identidades.
- `merge.py`: combinación de registros equivalentes.
- `overrides.py`: aplicación de Overrides.
- `normalizer.py`: normalización de texto, repositorios y versiones.
- `neovitadb.py`: lector específico de NeoVitaDB.

## VitaDB

El lector específico de VitaDB hace `POST` con cuerpo vacío al endpoint oficial. Usa un User-Agent de navegador y varios endpoints HTTPS/HTTP de respaldo porque el servidor puede devolver `200 + []` para agentes no compatibles.

Esto es una peculiaridad del origen y no debe trasladarse a otros catálogos sin comprobar su comportamiento real.

## Identidad de aplicaciones

Las aplicaciones se agrupan por identidad, con `title_id` como identificador principal del ecosistema Vita. No depender de los IDs numéricos de las fuentes externas como ID canónico de PS Vita Alive Store.

## Identidad de autores

Los nombres externos pueden venir con varios desarrolladores en un solo campo. El pipeline intenta separar nombres compuestos y usar repositorios/GitHub para resolver si un autor ya existe.

Cuando un autor no existe, se puede crear un perfil mínimo.

## Versión y descarga recomendada

La fuente con mayor prioridad no gana automáticamente una descarga. La lógica actual compara primero la versión y la fecha; la prioridad se usa como desempate. Esto permite que un VPK de una fuente menos prioritaria sea recomendado si realmente corresponde a una versión más reciente.

## Recursos

Los adaptadores deben devolver URLs multimedia absolutas cuando existan. Nunca fabricar una URL de screenshot simplemente concatenando carpetas si la fuente no publica ese recurso.

## Añadir un adaptador nuevo

1. Estudiar el formato real de la fuente.
2. Implementar `fetch`/lectura específica si no coincide con un JSON simple.
3. Devolver registros `dict` o `Candidate` según la interfaz existente.
4. Normalizar Title ID, autor, versión, categoría y recursos.
5. Añadir logs de conteo y errores.
6. Añadir una prueba/smoke test cuando sea útil.
7. Actualizar `sources/README.md` y la documentación de la fuente.
8. Validar sin modificar los catálogos generados a mano.
