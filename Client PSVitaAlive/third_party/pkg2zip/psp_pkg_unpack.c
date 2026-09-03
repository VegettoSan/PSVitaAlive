
/* PSP/PSX PKG → ux0:pspemu (pkg2zip-based, public domain). Vita-only glue. */
#include "psp_pkg_unpack.h"
#include "pkg2zip_aes.h"
#include "pkg2zip_out.h"
#include "pkg2zip_psp.h"
#include "pkg2zip_sys.h"
#include "pkg2zip_utils.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define PKG_HEADER_SIZE 192
#define PKG_HEADER_EXT_SIZE 64
#define ZIP_MAX_FILENAME 1024

static const uint8_t pkg_ps3_key[] = { 0x2e, 0x7b, 0x71, 0xd7, 0xc9, 0xc9, 0xa1, 0x4e, 0xa3, 0x22, 0x1f, 0x18, 0x88, 0x28, 0xb8, 0xf8 };
static const uint8_t pkg_psp_key[] = { 0x07, 0xf2, 0xc6, 0x82, 0x90, 0xb5, 0x0d, 0x2c, 0x33, 0x81, 0x8d, 0x70, 0x9b, 0x60, 0xe6, 0x2b };

typedef enum {
    PKG_TYPE_PSP,
    PKG_TYPE_PSX,
} pkg_type;

static void parse_sfo_content(const uint8_t* sfo, uint32_t sfo_size, char* category, char* title)
{
    if (get32le(sfo) != 0x46535000)
        sys_error("ERROR: incorrect sfo signature\n");
    uint32_t keys = get32le(sfo + 8);
    uint32_t values = get32le(sfo + 12);
    uint32_t count = get32le(sfo + 16);
    int title_index = -1;
    int category_index = -1;
    for (uint32_t i = 0; i < count; i++) {
        if (i * 16 + 20 + 2 > sfo_size) sys_error("ERROR: sfo information is too small\n");
        char* key = (char*)sfo + keys + get16le(sfo + i * 16 + 20);
        if (strcmp(key, "TITLE") == 0) {
            if (title_index < 0) title_index = (int)i;
        } else if (strcmp(key, "STITLE") == 0) {
            title_index = (int)i;
        } else if (strcmp(key, "CATEGORY") == 0) {
            category_index = (int)i;
        }
    }
    if (title_index < 0) sys_error("ERROR: cannot find title from sfo\n");
    char* value = (char*)sfo + values + get32le(sfo + title_index * 16 + 20 + 12);
    size_t i, max = 255;
    for (i = 0; i < max && *value; i++, value++) {
        if ((*value >= 32 && *value < 127 && strchr("<>\"/\\|?*", *value) == NULL) || (uint8_t)*value >= 128) {
            if (*value == ':') { *title++ = ' '; *title++ = '-'; max--; }
            else *title++ = *value;
        } else if (*value == 10) *title++ = ' ';
    }
    *title = 0;
    if (category_index >= 0) {
        value = (char*)sfo + values + get32le(sfo + category_index * 16 + 20 + 12);
        while (*value) *category++ = *value++;
    }
    *category = 0;
}

static void find_psp_sfo(const aes128_key* key, const aes128_key* ps3_key, const uint8_t* iv, sys_file pkg, uint64_t pkg_size, uint64_t enc_offset, uint64_t items_offset, uint32_t item_count, char* category, char* title)
{
    for (uint32_t item_index = 0; item_index < item_count; item_index++) {
        uint8_t item[32];
        uint64_t item_offset = items_offset + item_index * 32;
        sys_read(pkg, enc_offset + item_offset, item, sizeof(item));
        aes128_ctr_xor(key, iv, item_offset / 16, item, sizeof(item));
        uint32_t name_offset = get32be(item + 0);
        uint32_t name_size = get32be(item + 4);
        uint64_t data_offset = get64be(item + 8);
        uint64_t data_size = get64be(item + 16);
        uint8_t psp_type = item[24];
        if (pkg_size < enc_offset + name_offset + name_size || pkg_size < enc_offset + data_offset + data_size)
            sys_error("ERROR: pkg file is too short\n");
        const aes128_key* item_key = psp_type == 0x90 ? key : ps3_key;
        char name[ZIP_MAX_FILENAME];
        sys_read(pkg, enc_offset + name_offset, name, name_size);
        aes128_ctr_xor(item_key, iv, name_offset / 16, (uint8_t*)name, name_size);
        name[name_size] = 0;
        if (strcmp(name, "PARAM.SFO") == 0) {
            uint8_t sfo[16 * 1024];
            if (data_size < 16 || data_size > sizeof(sfo)) sys_error("ERROR: bad sfo size\n");
            sys_read(pkg, enc_offset + data_offset, sfo, (uint32_t)data_size);
            aes128_ctr_xor(item_key, iv, data_offset / 16, sfo, (uint32_t)data_size);
            parse_sfo_content(sfo, (uint32_t)data_size, category, title);
            return;
        }
    }
}

static int do_unpack(const char* pkg_arg, int as_iso, char* out_path, unsigned out_path_sz)
{
    uint64_t pkg_size;
    sys_file pkg = sys_open(pkg_arg, &pkg_size);
    uint8_t pkg_header[PKG_HEADER_SIZE + PKG_HEADER_EXT_SIZE];
    sys_read(pkg, 0, pkg_header, sizeof(pkg_header));
    if (get32be(pkg_header) != 0x7f504b47 || get32be(pkg_header + PKG_HEADER_SIZE) != 0x7F657874)
        sys_error("ERROR: not a pkg file\n");

    uint64_t meta_offset = get32be(pkg_header + 8);
    uint32_t meta_count = get32be(pkg_header + 12);
    uint32_t item_count = get32be(pkg_header + 20);
    uint64_t total_size = get64be(pkg_header + 24);
    uint64_t enc_offset = get64be(pkg_header + 32);
    const uint8_t* iv = pkg_header + 0x70;
    int key_type = pkg_header[0xe7] & 7;

    if (pkg_size < total_size) sys_error("ERROR: pkg file is too small\n");

    uint32_t content_type = 0;
    uint32_t items_offset = 0;
    for (uint32_t i = 0; i < meta_count; i++) {
        uint8_t block[16];
        sys_read(pkg, meta_offset, block, sizeof(block));
        uint32_t type = get32be(block + 0);
        uint32_t size = get32be(block + 4);
        if (type == 2) content_type = get32be(block + 8);
        else if (type == 13) items_offset = get32be(block + 8);
        meta_offset += 2 * sizeof(uint32_t) + size;
    }

    pkg_type type;
    if (content_type == 6) type = PKG_TYPE_PSX;
    else if (content_type == 7 || content_type == 0xe || content_type == 0xf || content_type == 0x10)
        type = PKG_TYPE_PSP;
    else
        sys_error("ERROR: unsupported content type 0x%x (not PSP/PSX)\n", content_type);

    aes128_key ps3_key;
    uint8_t main_key[16];
    if (key_type == 1) {
        memcpy(main_key, pkg_psp_key, sizeof(main_key));
        aes128_init(&ps3_key, pkg_ps3_key);
    } else {
        sys_error("ERROR: unsupported PKG key type %d for PSP/PSX\n", key_type);
    }
    aes128_key key;
    aes128_init(&key, main_key);

    char title[256] = {0};
    char category[256] = {0};
    const char* id = (char*)pkg_header + 0x37;
    find_psp_sfo(&key, &ps3_key, iv, pkg, pkg_size, enc_offset, items_offset, item_count, category, title);

    sys_output("[*] %s [%.9s] type=%s\n", title, id, type == PKG_TYPE_PSX ? "PSX" : "PSP");
    out_begin(NULL, 0);
    sys_output_progress_init(pkg_size);

    char primary[512] = {0};

    for (uint32_t item_index = 0; item_index < item_count; item_index++) {
        uint8_t item[32];
        uint64_t item_offset = items_offset + item_index * 32;
        sys_read(pkg, enc_offset + item_offset, item, sizeof(item));
        aes128_ctr_xor(&key, iv, item_offset / 16, item, sizeof(item));

        uint32_t name_offset = get32be(item + 0);
        uint32_t name_size = get32be(item + 4);
        uint64_t data_offset = get64be(item + 8);
        uint64_t data_size = get64be(item + 16);
        uint8_t psp_type = item[24];
        uint8_t flags = item[27];

        if (pkg_size < enc_offset + name_offset + name_size ||
            pkg_size < enc_offset + data_offset + data_size)
            sys_error("ERROR: pkg file is too short\n");

        const aes128_key* item_key;
        if (type == PKG_TYPE_PSP || type == PKG_TYPE_PSX)
            item_key = psp_type == 0x90 ? &key : &ps3_key;
        else
            item_key = &key;

        char name[ZIP_MAX_FILENAME];
        sys_read(pkg, enc_offset + name_offset, name, name_size);
        aes128_ctr_xor(item_key, iv, name_offset / 16, (uint8_t*)name, name_size);
        name[name_size] = 0;

        int decrypt = 1;
        (void)flags;

        char path[512];
        if (type == PKG_TYPE_PSX) {
            if (strcmp("USRDIR/CONTENT/EBOOT.PBP", name) == 0) {
                snprintf(path, sizeof(path), "pspemu/PSP/GAME/%.9s/EBOOT.PBP", id);
                if (!primary[0]) snprintf(primary, sizeof(primary), "pspemu/PSP/GAME/%.9s", id);
            } else if (strcmp("USRDIR/CONTENT/DOCUMENT.DAT", name) == 0) {
                snprintf(path, sizeof(path), "pspemu/PSP/GAME/%.9s/DOCUMENT.DAT", id);
            } else if (strcmp("USRDIR/CONTENT/KEYS.BIN", name) == 0) {
                snprintf(path, sizeof(path), "pspemu/PSP/GAME/%.9s/KEYS.BIN", id);
            } else {
                continue;
            }
        } else { /* PSP */
            if (strcmp("USRDIR/CONTENT/EBOOT.PBP", name) == 0) {
                if (as_iso) {
                    /* ISO under pspemu/ISO (pkg2zip eboot→iso) */
                    snprintf(path, sizeof(path), "pspemu/ISO/%s [%.9s].iso", title, id);
                    if (!primary[0]) snprintf(primary, sizeof(primary), "%s", path);
                    unpack_psp_eboot(path, item_key, iv, pkg, enc_offset, data_offset, data_size, 0);
                    continue;
                }
                /* Default: folder / EBOOT.PBP (PKGj install_psp_as_pbp) */
                snprintf(path, sizeof(path), "pspemu/PSP/GAME/%.9s/EBOOT.PBP", id);
                if (!primary[0]) snprintf(primary, sizeof(primary), "pspemu/PSP/GAME/%.9s", id);
            } else if (strcmp("USRDIR/CONTENT/PSP-KEY.EDAT", name) == 0) {
                snprintf(path, sizeof(path), "pspemu/PSP/GAME/%.9s/PSP-KEY.EDAT", id);
                unpack_psp_key(path, item_key, iv, pkg, enc_offset, data_offset, data_size);
                continue;
            } else if (strcmp("USRDIR/CONTENT/CONTENT.DAT", name) == 0) {
                snprintf(path, sizeof(path), "pspemu/PSP/GAME/%.9s/CONTENT.DAT", id);
            } else if (strcmp("USRDIR/CONTENT/DOCUMENT.DAT", name) == 0) {
                snprintf(path, sizeof(path), "pspemu/PSP/GAME/%.9s/DOCUMENT.DAT", id);
            } else {
                continue;
            }
        }

        uint64_t offset = data_offset;
        out_begin_file(path, 0);
        while (data_size != 0) {
            uint8_t PKG_ALIGN(16) buffer[1 << 16];
            uint32_t size = (uint32_t)min64(data_size, sizeof(buffer));
            sys_output_progress(enc_offset + offset);
            sys_read(pkg, enc_offset + offset, buffer, size);
            if (decrypt) aes128_ctr_xor(item_key, iv, offset / 16, buffer, size);
            out_write(buffer, size);
            offset += size;
            data_size -= size;
        }
        out_end_file();
    }

    out_end();
    sys_close(pkg);
    if (out_path && out_path_sz) {
        if (primary[0]) {
            snprintf(out_path, out_path_sz, "%s%s", "ux0:", primary); /* prefix shown for UI; real prefix applied in sys */
        } else {
            out_path[0] = 0;
        }
    }
    return 0;
}



int psp_pkg_probe_is_psp_psx(const char* pkg_path)
{
    if (!pkg_path || !pkg_path[0]) return -1;
    /* Full paths (ux0:...) — no prefix rewrite needed for probe. */
    uint64_t pkg_size = 0;
    sys_file pkg = sys_open(pkg_path, &pkg_size);
    if (!pkg) return -1;
    uint8_t pkg_header[PKG_HEADER_SIZE + PKG_HEADER_EXT_SIZE];
    sys_read(pkg, 0, pkg_header, sizeof(pkg_header));
    if (get32be(pkg_header) != 0x7f504b47) {
        sys_close(pkg);
        return -1;
    }
    uint64_t meta_offset = get32be(pkg_header + 8);
    uint32_t meta_count = get32be(pkg_header + 12);
    uint32_t content_type = 0;
    for (uint32_t i = 0; i < meta_count; i++) {
        uint8_t block[16];
        sys_read(pkg, meta_offset, block, sizeof(block));
        uint32_t type = get32be(block + 0);
        uint32_t size = get32be(block + 4);
        if (type == 2) content_type = get32be(block + 8);
        meta_offset += 2 * sizeof(uint32_t) + size;
    }
    sys_close(pkg);
    if (content_type == 6) return 1; /* PSX */
    if (content_type == 7 || content_type == 0xe || content_type == 0xf || content_type == 0x10)
        return 1; /* PSP */
    return 0; /* Vita / other — use LiveArea/BGDL path */
}

int psp_pkg_unpack_to_pspemu(const char* pkg_path, const char* partition, int as_iso,
                             char* out_path, unsigned out_path_sz)
{
    if (!pkg_path || !pkg_path[0]) return -1;
    pkg2zip_set_root_prefix(partition ? partition : "ux0:");
    sys_output_init();
    if (pkg2zip_setjmp_begin() != 0) {
        pkg2zip_setjmp_end();
        sys_output_done();
        return -1;
    }
    do_unpack(pkg_path, as_iso ? 1 : 0, out_path, out_path_sz);
    pkg2zip_setjmp_end();
    sys_output_done();
    return 0;
}
