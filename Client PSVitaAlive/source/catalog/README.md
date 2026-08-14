# `Client PSVitaAlive/source/catalog/` — Consumo del catálogo

Este módulo es la frontera entre los catálogos generados y el cliente PS Vita.

## Fuente de datos

Solo debe consumir:

- `catalog.json`
- `authors.json`
- `categories.json`

No realizar scraping ni consultas directas a VitaDB, NeoVitaDB o VitaDBtoo desde el cliente.

## Campos importantes

El cliente puede utilizar:

- `id`
- `title_id`
- `name`
- `description`
- `author_ids`
- `category_id`
- `subcategory_ids`
- `version`
- `version_date`
- `icon`
- `screenshots`
- `links`
- `status`

Los demás campos pueden ser opcionales y no deben romper el parser.

## Autores

`author_ids` permite navegar a perfiles individuales mediante `authors.json`.

Una aplicación con varios autores no debe mostrarse como un único nombre compuesto.

## Versiones y actualización

El `title_id` es la identidad de instalación/actualización. La versión y fecha permiten informar al usuario de una actualización disponible.

## Recursos remotos

Iconos y screenshots pueden ser URLs HTTPS/HTTP absolutas. El cliente debe manejar fallos de red y recursos faltantes sin bloquear la navegación del catálogo.
