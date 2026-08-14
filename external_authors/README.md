# `external_authors/` — Autores externos provisionales

Esta capa representa autores descubiertos automáticamente que todavía no forman parte de la colección canónica de perfiles de `authors/` o que necesitan resolución de identidad adicional.

## Flujo

```text
fuente externa
    ↓
nombre/repo externo
    ↓
resolución de identidad
    ↓
external_authors/ (si hace falta información provisional)
    ↓
authors/<id>.json
```

El `author_id` debe ser estable. Cuando el autor sea curado manualmente, crea:

```text
authors/<mismo-id>.json
```

No cambies el ID de las aplicaciones que lo utilizan.

## Identidad

El pipeline puede comparar nombre normalizado, variantes de guion/guion bajo y enlaces/repositorios para evitar perfiles duplicados.

Si una fuente representa a varios desarrolladores en un solo campo, el objetivo es generar perfiles individuales cuando la separación sea inequívoca.

## Fallback

Cuando no exista avatar fiable, el perfil canónico puede utilizar:

`https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors/icon/autoricon.png`

## Importante

`external_authors/` no sustituye `authors/`. Los perfiles que la web y el cliente consumen pertenecen a la capa canónica `authors/` y se reflejan en `authors.json` generado.
