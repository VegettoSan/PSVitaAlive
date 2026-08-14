# PSVitaAlive — Cliente PS Vita de PS Vita Alive Store

Cliente nativo de [PS Vita Alive Store](https://github.com/VegettoSan/PSVitaAlive) para PlayStation Vita (VitaSDK).

## Estado actual

**Fase 0 + Fase 1**

- Estructura CMake
- `StorageManager`
- Creación de `ux0:data/psvitaalive/`
- Escritura / lectura de archivo de prueba

## Requisitos

- VitaSDK instalado (`$VITASDK` definido)
- PS Vita con HENkaku/Ensō y VitaShell

### Paquetes recomendados (vdpm)

Ya deberían estar con `./install-all.sh`. Si falta algo:

```bash
vdpm zlib
vdpm libpng
vdpm libjpeg-turbo
vdpm freetype
vdpm vita2d
# Más adelante:
# vdpm curl
# vdpm libzip
```

## Compilar

```bash
cd ~/PSVitaAlive
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

Salida: `PSVitaAlive.vpk`

## Instalar en la Vita

1. VitaShell → SELECT → FTP ON
2. Subir `PSVitaAlive.vpk` a `ux0:data/`
3. Instalar el VPK desde VitaShell
4. Abrir la app (pulsa ✕ para salir)

## Verificar Fase 1

Tras ejecutar la app, en VitaShell revisa:

```text
ux0:data/psvitaalive/test/phase1.txt
ux0:data/psvitaalive/test/summary.txt
```

Si `summary.txt` contiene `dirs=1 write=1 read=1 size=1`, la Fase 1 está validada.

## Estructura

```text
PSVitaAlive/
├── CMakeLists.txt
├── include/storage/storage_manager.hpp
├── source/main.cpp
├── source/storage/storage_manager.cpp
└── assets/sce_sys/          # icono / LiveArea (opcional por ahora)
```

## Fases siguientes

2. HttpClient  
3. DownloadManager  
4. ZipExtractor  
5. FormatDetector  
6. HomebrewInstaller (VPK)  
7. VitaInstaller (PKG)  
8. UI FULL_CATALOG  
9. SPLIT_DETAIL  
10. Integración UI ↔ install  

Ver documento maestro: `PSVitaAlive_Cliente_VitaSDK_Instrucciones_Completas.md`
