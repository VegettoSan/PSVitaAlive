#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Unpack a PSP or PSX retail PKG into Adrenaline layout under partition root.
 *
 * @param pkg_path   Full path to .pkg on device
 * @param partition  e.g. "ux0:"
 * @param as_iso     0 = Folder/PBP (default): PSP → GAME/<ID>/EBOOT.PBP
 *                   1 = ISO: PSP → ISO/*.iso (pkg2zip eboot→iso)
 *                   PSX always uses GAME folder.
 * @param out_path   Optional buffer for primary install path
 * @param out_path_sz
 * @return 0 on success, -1 on failure (see pkg2zip_last_error())
 */
int psp_pkg_unpack_to_pspemu(const char* pkg_path, const char* partition, int as_iso,
                             char* out_path, unsigned out_path_sz);

const char* pkg2zip_last_error(void);

#ifdef __cplusplus
}
#endif
