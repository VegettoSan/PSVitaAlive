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
set(VITA_TITLEID  "PSVAS1178")
set(VITA_VERSION "01.00")
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1")

set(SOURCES
  source/main.cpp
  source/diagnostic_logger.cpp
  source/storage/storage_manager.cpp
  source/network/http_client.cpp
  source/network/download_manager.cpp
  source/network/mediafire_resolver.cpp
  source/archive/zip_extractor.cpp
  source/archive/format_detector.cpp
  source/installer/homebrew_installer.cpp
  source/installer/fake_package_builder.cpp
  source/installer/vita_installer.cpp
  source/installer/psp_installer.cpp
  source/installer/license_helper.cpp
  source/installer/plugin_detector.cpp
  source/installer/refresh_manager.cpp
  source/installer/bgdl_client.cpp
  source/installer/app_settings.cpp
  source/installer/install_dispatcher.cpp
  source/installer/install_controller.cpp
  source/catalog/catalog_parser.cpp
  source/catalog/catalog_manager.cpp
  source/ui/ui_types.cpp
  source/ui/image_cache.cpp
  source/ui/full_catalog_screen.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/source
)

target_compile_options(${PROJECT_NAME} PRIVATE
  -fno-rtti
)

target_link_libraries(${PROJECT_NAME}
  vita2d
  vita2d
  png
  jpeg
  zip
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
  ScePower_stub
  SceNet_stub
  SceNetCtl_stub
  ScePromoterUtil_stub
  taihen_stub
  SceGxm_stub
  ScePgf_stub
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

# Build the VPK explicitly instead of using vita_create_vpk().
# VitaSDK's vita_create_vpk() converts FILE paths to absolute paths and its
# vita-pack-vpk custom command does not use VERBATIM. That is unsafe when the
# project path contains spaces (for example: "Client PSVitaAlive").
# We therefore invoke vita-mksfoex and vita-pack-vpk with relative arguments
# from the build directory, so the source path never contains a space.
set(VPK_PARAM_SFO "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.vpk_param.sfo")
set(VPK_OUT "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.vpk.out")
set(VPK_FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.vpk")

add_custom_command(
  OUTPUT "${VPK_OUT}"
  COMMAND ${VITA_MKSFOEX}
    -d PARENTAL_LEVEL=1
    -s APP_VER=${VITA_VERSION}
    -s TITLE_ID=${VITA_TITLEID}
    "${VITA_APP_NAME}"
    "${VPK_PARAM_SFO}"
  COMMAND ${VITA_PACK_VPK}
    -a "../assets/sce_sys=sce_sys"
    -a "../assets/ui=ui"
    -s "${PROJECT_NAME}.vpk_param.sfo"
    -b "eboot.bin"
    "${PROJECT_NAME}.vpk.out"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  DEPENDS
    eboot.bin-self
    "${CMAKE_SOURCE_DIR}/assets/sce_sys"
    "${CMAKE_SOURCE_DIR}/assets/ui"
  COMMENT "Building vpk ${PROJECT_NAME}"
  VERBATIM
)

add_custom_target(${PROJECT_NAME}-vpk
  ALL
  DEPENDS "${VPK_OUT}"
  COMMAND ${CMAKE_COMMAND} -E copy "${VPK_OUT}" "${VPK_FILE}"
  BYPRODUCTS "${VPK_FILE}"
)

if(TARGET eboot.bin-self)
  add_dependencies(${PROJECT_NAME}-vpk eboot.bin-self)
endif()

message(STATUS "PSVitaAlive configured. TitleID=${VITA_TITLEID}")
