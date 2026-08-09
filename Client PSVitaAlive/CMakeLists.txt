cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(PSVitaAlive)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "PSVitaAlive")
set(VITA_TITLEID  "PSVA00001")
set(VITA_VERSION  "01.00")
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1")

set(SOURCES
  source/main.cpp
  source/storage/storage_manager.cpp
  source/network/http_client.cpp
  source/network/download_manager.cpp
  source/archive/zip_extractor.cpp
  source/archive/format_detector.cpp
  source/installer/homebrew_installer.cpp
  source/installer/vita_installer.cpp
  source/installer/install_dispatcher.cpp
  source/installer/install_controller.cpp
  source/catalog/catalog_parser.cpp
  source/ui/ui_types.cpp
  source/ui/full_catalog_screen.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_compile_options(${PROJECT_NAME} PRIVATE
  -fno-rtti
)

target_link_libraries(${PROJECT_NAME}
  vita2d

  SceDisplay_stub
  SceCtrl_stub
  SceTouch_stub
  SceSysmodule_stub
  SceLibJson_stub
  SceAppMgr_stub
  SceCommonDialog_stub
  SceIme_stub
  SceIofilemgr_stub
  SceLibKernel_stub
  # quitar si no existe:
# SceKernelThreadMgr_stub
  ScePower_stub
  SceNet_stub
  SceNetCtl_stub

  ScePromoterUtil_stub
  SceGxm_stub
  ScePgf_stub

  zip
  zstd
  lzma
  bz2
  z
  curl
  ssl
  crypto
  pthread
  m
)

vita_create_self(eboot.bin ${PROJECT_NAME})

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID}
  eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)

message(STATUS "PSVitaAlive configured. TitleID=${VITA_TITLEID}")
