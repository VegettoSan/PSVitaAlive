cmake_minimum_required(VERSION 3.16)

# Kernel helper for the PSM Runtime installer (skprx packed into the main VPK).
add_executable(psm_runtime_driver
  psm_runtime_driver.c
)

target_link_libraries(psm_runtime_driver
  k
  SceSysmemForDriver_stub
  SceIofilemgrForDriver_stub
  SceDebugForDriver_stub
  SceFios2KernelForDriver_stub
  SceProcEventForDriver_stub
  SceAppMgrForDriver_stub
  taihenForKernel_stub
  gcc
)

set_target_properties(psm_runtime_driver PROPERTIES
  LINK_FLAGS "-nostdlib"
)

# vita-elf-create / vita-make-fself break when paths contain spaces
# (e.g. ".../Client PSVitaAlive/..."). Stage everything under a space-free dir.
set(_PSM_STAGE "$ENV{HOME}/.cache/psvitaalive_build/psm_runtime_driver")
if(NOT _PSM_STAGE OR _PSM_STAGE STREQUAL "/.cache/psvitaalive_build/psm_runtime_driver")
  set(_PSM_STAGE "/tmp/psvitaalive_build/psm_runtime_driver")
endif()
file(MAKE_DIRECTORY "${_PSM_STAGE}")

find_program(VITA_ELF_CREATE_TOOL vita-elf-create HINTS "$ENV{VITASDK}/bin" REQUIRED)
find_program(VITA_MAKE_FSELF_TOOL vita-make-fself HINTS "$ENV{VITASDK}/bin" REQUIRED)

# Parent VPK rule expects this file in the top-level binary dir:
#   -a "psm_runtime_driver.skprx=psm_runtime_driver.skprx"
set(_PSM_SKPRX "${CMAKE_BINARY_DIR}/psm_runtime_driver.skprx")
set(_PSM_ELF_STAGE "${_PSM_STAGE}/psm_runtime_driver.elf")
set(_PSM_VELF_STAGE "${_PSM_STAGE}/psm_runtime_driver.velf")

add_custom_command(
  OUTPUT "${_PSM_SKPRX}"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:psm_runtime_driver>" "${_PSM_ELF_STAGE}"
  COMMAND "${VITA_ELF_CREATE_TOOL}" -e "${CMAKE_CURRENT_SOURCE_DIR}/exports.yml"
          "${_PSM_ELF_STAGE}" "${_PSM_VELF_STAGE}"
  COMMAND "${VITA_MAKE_FSELF_TOOL}" -c -u "${_PSM_VELF_STAGE}" "${_PSM_SKPRX}"
  DEPENDS psm_runtime_driver "${CMAKE_CURRENT_SOURCE_DIR}/exports.yml"
  COMMENT "Creating psm_runtime_driver.skprx (space-safe staging in ${_PSM_STAGE})"
  VERBATIM
)

# Target name expected by parent CMakeLists.txt (VPK depends on this)
add_custom_target(psm_runtime_driver.skprx-self ALL DEPENDS "${_PSM_SKPRX}")
