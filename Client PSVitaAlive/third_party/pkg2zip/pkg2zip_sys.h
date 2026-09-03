#pragma once

#include "pkg2zip_utils.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void sys_output_init(void);
void sys_output_done(void);
void sys_output(const char* msg, ...);
void NORETURN sys_error(const char* msg, ...);

void sys_output_progress_init(uint64_t size);
void sys_output_progress(uint64_t progress);

typedef void* sys_file;

void sys_mkdir(const char* path);
sys_file sys_open(const char* fname, uint64_t* size);
sys_file sys_create(const char* fname);
void sys_close(sys_file file);
void sys_read(sys_file file, uint64_t offset, void* buffer, uint32_t size);
void sys_write(sys_file file, uint64_t offset, const void* buffer, uint32_t size);
void* sys_realloc(void* ptr, size_t size);
void sys_vstrncat(char* dst, size_t n, const char* format, ...);

/* Vita: set partition prefix, e.g. "ux0:" — paths like pspemu/... become ux0:pspemu/... */
void pkg2zip_set_root_prefix(const char* prefix);

/* Returns 0 on success, -1 on error. last error in pkg2zip_last_error(). */
int pkg2zip_setjmp_begin(void);
void pkg2zip_setjmp_end(void);
const char* pkg2zip_last_error(void);

#ifdef __cplusplus
}
#endif
