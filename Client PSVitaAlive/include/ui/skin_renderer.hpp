#pragma once
#include "ui/skin_atlas_coords.hpp"
#include <algorithm>
#include <cmath>
#include <vita2d.h>

namespace psvitaalive::skin {

// The UI thread owns these resources. Load once after vita2d_init and free
// before vita2d_fini. No catalog cache entries or network activity.
enum class Atlas { Ui, Icons, Controls };
inline vita2d_texture* textures[3] = {};
inline unsigned calls = 0;
inline unsigned vertices = 0;
inline unsigned poolFallbacks = 0;
inline unsigned invalidGeometry = 0;
// A/B diagnostic only: never stall normal frame submission.
inline bool gpuSyncTest = false;
inline void beforeFrame() { if (gpuSyncTest) vita2d_wait_rendering_done(); }
inline bool ready() { return textures[0] && textures[1] && textures[2]; }
inline void resetFrameCounters() { calls = vertices = poolFallbacks = invalidGeometry = 0; }
inline void release() {
    if (textures[0] || textures[1] || textures[2]) vita2d_wait_rendering_done();
    for (auto& t : textures) {
        if (t) vita2d_free_texture(t);
        t = nullptr;
    }
}
inline bool load() {
    if (ready()) return true;
    static constexpr const char* paths[] = {
        "app0:ui/skin/ui_atlas.png", "app0:ui/skin/icons_atlas.png",
        "app0:ui/skin/controls_atlas.png"
    };
    for (int i = 0; i < 3; ++i) {
        textures[i] = vita2d_load_PNG_file(paths[i]);
        const unsigned size = i == 0 ? 512u : 256u;
        if (!textures[i] || vita2d_texture_get_width(textures[i]) != size ||
                vita2d_texture_get_height(textures[i]) != size) {
            release();
            return false;
        }
    }
    return true;
}

struct Axis {
    float dst[4], src[4];
};
inline Axis axis(float position, float length, int start, int size,
                 int first, int last) {
    const float sum = static_cast<float>(first + last);
    const float scale = sum > 0.f ? std::min(1.f, length / sum) : 1.f;
    return {{position, position + first * scale,
             position + length - last * scale, position + length},
            {static_cast<float>(start), static_cast<float>(start + first),
             static_cast<float>(start + size - last), static_cast<float>(start + size)}};
}

// Each nine-slice uses ONE GXM draw, not nine. Vertices live in vita2d's
// per-frame GPU-visible pool, never on the stack or heap. Reuse the caller's
// scissor; don't modify clip state, blend mode, or frame/swap ownership.
inline bool draw(Atlas atlas, const Sprite& s, float x, float y,
                 float w, float h, unsigned tint = 0xffffffffu) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h)) {
        ++invalidGeometry; return true;
    }
    if (w <= 0.f || h <= 0.f) return true;
    auto* t = textures[static_cast<int>(atlas)];
    if (!t) return false;
    if (!(s.left || s.top || s.right || s.bottom)) {
        if (vita2d_pool_free_space() < 128) { ++poolFallbacks; return false; }
        vita2d_draw_texture_tint_part_scale(t, x, y, s.x, s.y, s.w, s.h,
            w / s.w, h / s.h, tint);
        ++calls; vertices += 4;
        return true;
    }
    const auto ax = axis(x, w, s.x, s.w, s.left, s.right);
    const auto ay = axis(y, h, s.y, s.h, s.top, s.bottom);
    unsigned count = 0;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            if (ax.dst[i+1] > ax.dst[i] && ay.dst[j+1] > ay.dst[j]) count += 6;
    const unsigned bytes = count * sizeof(vita2d_texture_vertex);
    if (!count || vita2d_pool_free_space() < bytes + 64) { ++poolFallbacks; return false; }
    // libvita2d uses bitmask alignment: pass a power of two, not the 20-byte vertex stride.
    auto* v = static_cast<vita2d_texture_vertex*>(
        vita2d_pool_memalign(bytes, 16u));
    if (!v) { ++poolFallbacks; return false; }
    const float invW = 1.f / vita2d_texture_get_width(t);
    const float invH = 1.f / vita2d_texture_get_height(t);
    unsigned n = 0;
    auto push = [&](int i, int j) {
        v[n++] = {ax.dst[i], ay.dst[j], .5f, ax.src[i] * invW, ay.src[j] * invH};
    };
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            if (ax.dst[i+1] <= ax.dst[i] || ay.dst[j+1] <= ay.dst[j]) continue;
            push(i,j); push(i+1,j); push(i,j+1);
            push(i,j+1); push(i+1,j); push(i+1,j+1);
        }
    }
    vita2d_draw_array_textured(t, SCE_GXM_PRIMITIVE_TRIANGLES, v, n, tint);
    ++calls; vertices += n;
    return true;
}

inline void surface(const Sprite& s, float x, float y, float w, float h, unsigned tint) {
    if (!draw(Atlas::Ui, s, x, y, w, h, tint) && w > 0 && h > 0)
        vita2d_draw_rectangle(x, y, w, h, tint);
}
inline bool icon(const Sprite& s, float x, float y, float size, unsigned tint,
                 Atlas atlas = Atlas::Icons) {
    return draw(atlas, s, x, y, size, size, tint);
}
inline bool control(const Sprite& s, float x, float y, float size) {
    return draw(Atlas::Controls, s, x, y, size, size);
}
inline void focus(float x, float y, float w, float h, unsigned tint) {
    if (draw(Atlas::Ui, ui_atlas::ui_focus_ring, x, y, w, h, tint)) return;
    if (w <= 0 || h <= 0) return;
    vita2d_draw_rectangle(x,y,w,2,tint);
    vita2d_draw_rectangle(x,y+h-2,w,2,tint);
    vita2d_draw_rectangle(x,y,2,h,tint);
    vita2d_draw_rectangle(x+w-2,y,2,h,tint);
}
} // namespace psvitaalive::skin
