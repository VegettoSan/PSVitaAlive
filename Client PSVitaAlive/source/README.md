# `Client PSVitaAlive/source/` — Código del cliente PS Vita

Este directorio contiene la implementación del cliente Homebrew de PSVitaAlive.

## Módulos actuales

- `catalog/` — lectura, parseo, búsqueda y representación del catálogo.
- `network/` — comunicación HTTP/HTTPS y descarga de datos.
- `storage/` — persistencia local y archivos del cliente.
- `ui/` — interfaz y pantallas.
- `installer/` — fundamentos de instalación/gestión de VPK.
- `archive/` — código histórico/archivado que no debe confundirse con la implementación activa.

## Contrato con el catálogo

El cliente no debe conocer VitaDB, NeoVitaDB ni VitaDBtoo directamente.

Su fuente pública es:

```text
catalog.json
authors.json
categories.json
```

La responsabilidad de combinar las fuentes externas pertenece a GitHub Actions.

```text
Fuentes externas
      ↓
GitHub Actions
      ↓
catalog.json / authors.json / categories.json
      ↓
Cliente Vita
```

## Compatibilidad

El parser debe tolerar campos opcionales del esquema y conservar compatibilidad con aplicaciones antiguas. No asumir que todos los campos opcionales estarán presentes.

## Descargas

Una aplicación puede tener varios enlaces. El cliente debe presentar el enlace recomendado cuando exista, pero permitir acceder a otras fuentes de descarga y a enlaces de repositorio/documentación cuando la UI lo soporte.

## Instalación

La lógica de instalación VPK pertenece al cliente y no al pipeline web. Una modificación del instalador no debe cambiar el contrato del catálogo salvo que se documente y actualice toda la cadena.

## Desarrollo

Para cambios de UI o catálogo:

1. Revisar primero el JSON publicado.
2. No introducir llamadas directas a fuentes externas desde el cliente.
3. Probar con catálogo local y remoto.
4. Mantener compatibilidad con VitaSDK y Vita3K.
5. Evitar SQLite mientras no sea necesario.
