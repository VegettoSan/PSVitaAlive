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
set(VITA_VERSION "01.18")
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1")

set(SOURCES
  source/main.cpp
  source/diagnostic_logger.cpp
  source/storage/storage_manager.cpp
  source/network/http_client.cpp
  source/network/curl_lifecycle.cpp
  source/network/download_manager.cpp
  source/network/mediafire_resolver.cpp
  source/network/error_reporter.cpp
  source/network/news_manager.cpp
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
  source/installer/pkg_bgdl_installer.cpp
  source/installer/app_settings.cpp
  source/installer/install_dispatcher.cpp
  source/installer/install_controller.cpp
  source/catalog/catalog_parser.cpp
  source/catalog/catalog_manager.cpp
  source/update/update_checker.cpp
  source/update/startup_update_manager.cpp
  source/ui/ui_types.cpp
  source/ui/image_cache.cpp
  source/ui/full_catalog_screen.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

# Redirect only HttpClient's curl_global_init() call to the process-wide,
# one-shot lifecycle proxy. There is deliberately no cleanup proxy: libcurl
# global state remains alive for the complete Vita process lifetime.
set_source_files_properties(source/network/http_client.cpp PROPERTIES
  COMPILE_DEFINITIONS "PSVITAALIVE_CURL_LIFECYCLE_PROXY"
)

set_source_files_properties(source/network/curl_lifecycle.cpp PROPERTIES
  COMPILE_DEFINITIONS "PSVITAALIVE_CURL_LIFECYCLE_IMPL"
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/source
)

target_compile_options(${PROJECT_NAME} PRIVATE
  -fno-rtti
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  PSVITAALIVE_VERSION="${VITA_VERSION}"
)

# Optional TLS backend switch (default OFF = stock VitaSDK OpenSSL 1.0.2 + curl).
# Enable only after: vdpm mbedtls && vdpm curl-mbedtls (or equivalent), then rebuild clean.
#   cmake .. -DPSVITAALIVE_USE_MBEDTLS_CURL=ON
option(PSVITAALIVE_USE_MBEDTLS_CURL
  "Link curl against mbedTLS instead of OpenSSL 1.0.2 (requires curl-mbedtls package)"
  OFF)

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
  SceAppUtil_stub
  SceCommonDialog_stub
  SceIme_stub
  SceIofilemgr_stub
  SceLibKernel_stub
  ScePower_stub
  SceNet_stub
  SceNetCtl_stub
  ScePromoterUtil_stub
  SceShellSvc_stub
  SceVshBridge_stub
  taihen_stub
  SceGxm_stub
  ScePgf_stub
  zstd
  lzma
  bz2
  z
  curl
  pthread
  m
)

if(PSVITAALIVE_USE_MBEDTLS_CURL)
  message(STATUS "PSVitaAlive TLS: mbedTLS-backed curl (PSVITAALIVE_USE_MBEDTLS_CURL=ON)")
  target_link_libraries(${PROJECT_NAME} mbedtls mbedx509 mbedcrypto)
  target_compile_definitions(${PROJECT_NAME} PRIVATE PSVITAALIVE_CURL_MBEDTLS=1)
else()
  message(STATUS "PSVitaAlive TLS: OpenSSL (stock VitaSDK)")
  target_link_libraries(${PROJECT_NAME} ssl crypto)
endif()

# ---------------------------------------------------------------------------
# PSVitaAlive Updater (TITLE_ID PSVAUPDT1)
# Separate process used for self-update (VitaShell pattern). Built first and
# packed into the main VPK under updater/ so the client can promote it.
# ---------------------------------------------------------------------------
set(UPDATER_TITLEID "PSVAUPDT1")
set(UPDATER_APP_NAME "PSVitaAlive Updater")
set(UPDATER_SOURCES updater/main.c)

add_executable(PSVitaAliveUpdater ${UPDATER_SOURCES})
target_link_libraries(PSVitaAliveUpdater
  vita2d
  png
  jpeg
  z
  SceDisplay_stub
  SceGxm_stub
  ScePgf_stub
  SceCommonDialog_stub
  SceSysmodule_stub
  SceAppMgr_stub
  ScePromoterUtil_stub
  SceIofilemgr_stub
  SceLibKernel_stub
  m
)

vita_create_self(updater_eboot.bin PSVitaAliveUpdater UNSAFE)

set(UPDATER_PARAM_SFO "${CMAKE_CURRENT_BINARY_DIR}/updater_param.sfo")
add_custom_command(
  OUTPUT "${UPDATER_PARAM_SFO}"
  COMMAND ${VITA_MKSFOEX}
    -d PARENTAL_LEVEL=1
    -s APP_VER=01.00
    -s TITLE_ID=${UPDATER_TITLEID}
    "${UPDATER_APP_NAME}"
    "${UPDATER_PARAM_SFO}"
  COMMENT "Building updater param.sfo"
  VERBATIM
)
add_custom_target(PSVitaAliveUpdaterSfo ALL DEPENDS "${UPDATER_PARAM_SFO}")

# The self-update uses the system Promoter APIs to replace the running app.
# These private Vita APIs require an UNSAFE SELF.
vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)

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
    -a "updater_eboot.bin=updater/eboot.bin"
    -a "updater_param.sfo=updater/param.sfo"
    -s "${PROJECT_NAME}.vpk_param.sfo"
    -b "eboot.bin"
    "${PROJECT_NAME}.vpk.out"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  DEPENDS
    eboot.bin-self
    updater_eboot.bin-self
    PSVitaAliveUpdaterSfo
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
