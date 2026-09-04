cmake_minimum_required(VERSION 3.16)

# Kernel helper for the PSM Runtime installer.
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

# Intermediate outputs in a path WITHOUT spaces so vita-elf-create works when the
# source tree lives under e.g. "Client PSVitaAlive".
set(_PSM_OUT_DIR "$ENV{HOME}/.cache/psvitaalive_build/psm_runtime_driver")
file(MAKE_DIRECTORY "${_PSM_OUT_DIR}")
set_target_properties(psm_runtime_driver PROPERTIES
  LINK_FLAGS "-nostdlib"
  RUNTIME_OUTPUT_DIRECTORY "${_PSM_OUT_DIR}"
  LIBRARY_OUTPUT_DIRECTORY "${_PSM_OUT_DIR}"
  ARCHIVE_OUTPUT_DIRECTORY "${_PSM_OUT_DIR}"
)

# vita_create_self writes next to the target file; keep OUTPUT in the binary dir
# via working around spaces by building the ELF outside the spaced tree.
vita_create_self(psm_runtime_driver.skprx psm_runtime_driver
  UNSAFE
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/exports.yml
)
