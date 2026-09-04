cmake_minimum_required(VERSION 3.16)

# Kernel helper for the PSM Runtime installer. This target is built with the
# VitaSDK kernel toolchain and packed into the main PSVitaAlive VPK.
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

vita_create_self(psm_runtime_driver.skprx psm_runtime_driver
  UNSAFE
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/exports.yml
)
