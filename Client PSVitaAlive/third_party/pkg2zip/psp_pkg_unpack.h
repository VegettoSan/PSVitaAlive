#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Unpack a PSP or PSX retail PKG into Adrenaline layout under partition root
 * (default paths are relative to prefix, e.g. ux0:pspemu/...).
 *
 * @param pkg_path  Full path to .pkg on device (e.g. ux0:data/.../payload.pkg)
 * @param partition Partition prefix with colon, e.g. "ux0:" (must not be NULL)
 * @param out_path  Optional buffer for primary install path (ISO or GAME folder)
 * @param out_path_sz Size of out_path
 * @return 0 on success, -1 on failure (see pkg2zip_last_error())
 */
int psp_pkg_unpack_to_pspemu(const char* pkg_path, const char* partition,
                             char* out_path, unsigned out_path_sz);

#ifdef __cplusplus
}
#endif
