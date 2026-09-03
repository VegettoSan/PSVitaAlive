#include "pkg2zip_sys.h"
#include <stdint.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>

static char g_prefix[16] = "ux0:";
static char g_last_error[512];
static jmp_buf g_jmp;
static int g_jmp_ready = 0;
static uint64_t g_progress_size;
static uint32_t g_progress_next;

void pkg2zip_set_root_prefix(const char* prefix)
{
    if (!prefix || !prefix[0]) {
        sceClibStrncpy(g_prefix, "ux0:", sizeof(g_prefix) - 1);
        g_prefix[sizeof(g_prefix) - 1] = 0;
        return;
    }
    sceClibStrncpy(g_prefix, prefix, sizeof(g_prefix) - 1);
    g_prefix[sizeof(g_prefix) - 1] = 0;
}

int pkg2zip_setjmp_begin(void)
{
    g_jmp_ready = 1;
    g_last_error[0] = 0;
    return setjmp(g_jmp);
}

void pkg2zip_setjmp_end(void)
{
    g_jmp_ready = 0;
}

const char* pkg2zip_last_error(void)
{
    return g_last_error;
}

static void make_abs(const char* path, char* out, size_t out_sz)
{
    if (strchr(path, ':')) {
        sceClibStrncpy(out, path, out_sz - 1);
        out[out_sz - 1] = 0;
        return;
    }
    snprintf(out, out_sz, "%s%s", g_prefix, path);
}

void sys_output_init(void) {}
void sys_output_done(void) {}

void sys_output(const char* msg, ...)
{
    char buffer[512];
    va_list arg;
    va_start(arg, msg);
    vsnprintf(buffer, sizeof(buffer), msg, arg);
    va_end(arg);
    sceClibPrintf("[pkg2zip] %s", buffer);
}

void sys_error(const char* msg, ...)
{
    va_list arg;
    va_start(arg, msg);
    vsnprintf(g_last_error, sizeof(g_last_error), msg, arg);
    va_end(arg);
    sceClibPrintf("[pkg2zip] ERROR: %s", g_last_error);
    if (g_jmp_ready) {
        longjmp(g_jmp, 1);
    }
    abort();
}

static void sys_mkdir_real(const char* path)
{
    char abs[512];
    make_abs(path, abs, sizeof(abs));
    int r = sceIoMkdir(abs, 0777);
    if (r < 0 && r != 0x80010011 /* EEXIST-ish */) {
        /* ignore already exists; other errors may be ok if parent exists */
    }
}

void sys_mkdir(const char* path)
{
    char tmp[512];
    sceClibStrncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    char* last = strrchr(tmp, '/');
    if (last && last != tmp) {
        *last = 0;
        sys_mkdir(tmp);
        *last = '/';
    }
    sys_mkdir_real(tmp);
}

sys_file sys_open(const char* fname, uint64_t* size)
{
    char abs[512];
    make_abs(fname, abs, sizeof(abs));
    SceUID fd = sceIoOpen(abs, SCE_O_RDONLY, 0);
    if (fd < 0) {
        sys_error("ERROR: cannot open '%s'\n", abs);
    }
    SceIoStat st;
    if (sceIoGetstat(abs, &st) < 0) {
        sceIoClose(fd);
        sys_error("ERROR: cannot stat '%s'\n", abs);
    }
    *size = (uint64_t)st.st_size;
    return (void*)(intptr_t)fd;
}

sys_file sys_create(const char* fname)
{
    char abs[512];
    make_abs(fname, abs, sizeof(abs));
    /* ensure parent dirs */
    char parent[512];
    sceClibStrncpy(parent, abs, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = 0;
    char* slash = strrchr(parent, '/');
    if (slash) {
        *slash = 0;
        /* recursive mkdir without prefix double */
        char* p = parent;
        if (strchr(p, ':')) {
            char* colon = strchr(p, ':');
            p = colon + 1;
            if (*p == '/') p++;
        }
        char build[512];
        sceClibStrncpy(build, parent, sizeof(build) - 1);
        /* use path up to each component */
        for (char* s = parent; *s; ++s) {
            if (*s == '/') {
                *s = 0;
                sceIoMkdir(parent, 0777);
                *s = '/';
            }
        }
        sceIoMkdir(parent, 0777);
    }
    SceUID fd = sceIoOpen(abs, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) {
        sys_error("ERROR: cannot create '%s'\n", abs);
    }
    return (void*)(intptr_t)fd;
}

void sys_close(sys_file file)
{
    sceIoClose((SceUID)(intptr_t)file);
}

void sys_read(sys_file file, uint64_t offset, void* buffer, uint32_t size)
{
    SceUID fd = (SceUID)(intptr_t)file;
    sceIoLseek(fd, (SceOff)offset, SCE_SEEK_SET);
    int n = sceIoRead(fd, buffer, size);
    if (n < 0 || (uint32_t)n != size) {
        sys_error("ERROR: failed to read %u bytes\n", size);
    }
}

void sys_write(sys_file file, uint64_t offset, const void* buffer, uint32_t size)
{
    SceUID fd = (SceUID)(intptr_t)file;
    sceIoLseek(fd, (SceOff)offset, SCE_SEEK_SET);
    int n = sceIoWrite(fd, buffer, size);
    if (n < 0 || (uint32_t)n != size) {
        sys_error("ERROR: failed to write %u bytes\n", size);
    }
}

void* sys_realloc(void* ptr, size_t size)
{
    void* result = NULL;
    if (!ptr && size) result = malloc(size);
    else if (ptr && !size) { free(ptr); return NULL; }
    else if (ptr && size) result = realloc(ptr, size);
    else sys_error("ERROR: internal error, wrong sys_realloc usage\n");
    if (!result) sys_error("ERROR: out of memory\n");
    return result;
}

void sys_vstrncat(char* dst, size_t n, const char* format, ...)
{
    char temp[512];
    va_list args;
    va_start(args, format);
    vsnprintf(temp, sizeof(temp), format, args);
    va_end(args);
    strncat(dst, temp, n - strlen(dst) - 1);
}

void sys_output_progress_init(uint64_t size)
{
    g_progress_size = size ? size : 1;
    g_progress_next = 0;
}

void sys_output_progress(uint64_t progress)
{
    uint32_t now = (uint32_t)(progress * 100 / g_progress_size);
    if (now >= g_progress_next) {
        sceClibPrintf("[pkg2zip] unpacking... %u%%\n", now);
        g_progress_next = now + 5;
    }
}
