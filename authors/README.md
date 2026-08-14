# `authors/` — Perfiles individuales de autores

Esta carpeta contiene un JSON por autor. PS Vita Alive Store trata a cada desarrollador como una entidad independiente para que la web y el cliente puedan abrir su perfil y mostrar sus aplicaciones.

## Estructura conceptual

```text
authors/
├── autor-a.json
├── autor-b.json
└── icon/
    └── autoricon.png
```

## Campos

Un perfil utiliza como base:

```json
{
  "id": "autor-a",
  "name": "Autor A",
  "avatar": "https://...",
  "bio": "...",
  "links": [
    {
      "type": "GitHub",
      "name": "GitHub",
      "url": "https://github.com/autor-a",
      "recommended": true
    }
  ],
  "icon": "https://..."
}
```

## Autores múltiples

Si una fuente externa entrega un campo como `Autor A & Autor B`, el pipeline intenta separarlo y resolver dos perfiles independientes. La aplicación resultante debe referenciar ambos mediante `author_ids`.

La separación debe ser conservadora para no romper nombres legítimos.

## Autores nuevos

Cuando una aplicación importada referencia un autor que todavía no existe, el pipeline puede crear un perfil mínimo automáticamente. Posteriormente puede enriquecerse con repositorio, enlaces y avatar encontrados en las fuentes.

## Avatar e icono de fallback

El fallback oficial es:

`https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors/icon/autoricon.png`

Si no hay avatar válido, el perfil persistido debe apuntar a una URL absoluta usable por GitHub Pages y el cliente Vita.

## Enlaces

Los enlaces del autor deben ser útiles y válidos. El pipeline normaliza duplicados y permite como máximo un enlace marcado como `recommended`.

Cuando no se conoce un repositorio, se puede conservar un enlace de búsqueda/identificación para que el perfil siga siendo válido.

## Autores huérfanos

Un JSON físico puede conservarse aunque no esté actualmente referenciado por una aplicación para fines de preservación. Sin embargo, `authors.json` se genera a partir de los autores que realmente participan en el catálogo final.

## No editar

`authors.json` es un registro generado. Las modificaciones permanentes deben realizarse en `authors/*.json` o mediante el sistema de agregación/Overrides correspondiente.
