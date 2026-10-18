#include "ui/qr_code.hpp"
#include "qrcodegen.h"
#include <png.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace psvitaalive { namespace ui {
namespace {
constexpr const char* ROOT = "ux0:data/psvitaalive/cache/images/v3";
uint64_t urlHash(const std::string& url) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned char ch : url) { hash ^= ch; hash *= UINT64_C(1099511628211); }
    return hash;
}
bool fileExists(const std::string& path) {
    SceIoStat stat{};
    return sceIoGetstat(path.c_str(), &stat) >= 0 && stat.st_size > 50;
}
}
bool ensureWebQrPng(const std::string& url, std::string& path, std::string& error) {
    error.clear(); path.clear();
    if (url.empty() || url.size() > 512) { error = "invalid URL"; return false; }
    char key[24];
    std::snprintf(key, sizeof(key), "%016llx",
                  static_cast<unsigned long long>(urlHash(url)));
    const std::string bucket = std::string(ROOT) + "/" + std::string(key, 2);
    path = bucket + "/qr_" + key + ".png";
    if (fileExists(path)) return true;
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX] = {};
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX] = {};
    if (!qrcodegen_encodeText(url.c_str(), temp, qr, qrcodegen_Ecc_MEDIUM,
                             qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                             qrcodegen_Mask_AUTO, true)) {
        error = "QR encoding failed"; return false;
    }
    const int size = qrcodegen_getSize(qr);
    const int side = size + 8; // quiet zone: 4 white modules each edge
    std::vector<uint8_t> rgba(static_cast<size_t>(side) * side * 4, 255);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!qrcodegen_getModule(qr, x, y)) continue;
            const size_t k = (static_cast<size_t>(y + 4) * side + (x + 4)) * 4;
            rgba[k] = rgba[k + 1] = rgba[k + 2] = 0;
        }
    }
    // ROOT exists after ImageCache::init; generated PNGs are counted by its bucket scanner.
    sceIoMkdir(bucket.c_str(), 0777);
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    png.width = static_cast<png_uint_32>(side);
    png.height = static_cast<png_uint_32>(side);
    png.format = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&png, path.c_str(), 0, rgba.data(), 0, nullptr)) {
        error = png.message[0] ? png.message : "PNG write failed";
        sceIoRemove(path.c_str());
        path.clear();
        png_image_free(&png);
        return false;
    }
    png_image_free(&png);
    return true;
}
}} // namespace psvitaalive::ui
