#!/usr/bin/env python3
"""Marquee name text must stay inside the catalog panel scissor."""
from pathlib import Path

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
cpp = CPP.read_text(encoding="utf-8")

old_fn = '''/** Soft horizontal marquee for long titles when focused (list card + detail header). */
void drawMarqueeText(vita2d_pgf* font, int x, int y, int maxW, unsigned color, float scale,
                     const std::string& text, bool animate) {
    if (!font || text.empty() || maxW <= 8) return;
    const int tw = vita2d_pgf_text_width(font, scale, text.c_str());
    // IMPORTANT: do not disable global clipping on the static path — parent panels
    // (catalog grid / detail body) rely on their scissor staying active.
    if (tw <= maxW) {
        vita2d_pgf_draw_text(font, x, y, color, scale, text.c_str());
        return;
    }
    if (!animate) {
        // Static: pixel-accurate ellipsis so long names never spill past maxW.
        std::string s = text;
        const char* dots = "...";
        const int dotsW = vita2d_pgf_text_width(font, scale, dots);
        while (s.size() > 1 && vita2d_pgf_text_width(font, scale, s.c_str()) + dotsW > maxW)
            s.pop_back();
        while (!s.empty() && (unsigned char)s.back() < 0x80 && (s.back() == \' \' || s.back() == \'.\'))
            s.pop_back();
        s += dots;
        vita2d_pgf_draw_text(font, x, y, color, scale, s.c_str());
        return;
    }
    // Animated marquee needs a tight scissor for the scrolling glyphs.
    // Callers that own a wider panel clip must re-assert it after this returns.
    const int gap = 48;
    const int cycle = tw + gap;
    const uint64_t ms = sceKernelGetProcessTimeWide() / 1000ULL;
    // Pause ~1.2s at the start, then scroll ~35 px/s, loop seamlessly.
    const uint64_t period = static_cast<uint64_t>(cycle) * 28ULL + 1200ULL;
    const uint64_t t = ms % period;
    int offset = 0;
    if (t > 1200ULL) offset = static_cast<int>((t - 1200ULL) / 28ULL);
    if (offset > cycle) offset = cycle;
    const int clipTop = y - 20;
    const int clipBottom = y + 6;
    // Keep the marquee scissor tight to the actual text strip so glyphs can
    // never spill into author/status rows while scrolling.
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x, clipTop, x + maxW, clipBottom);
    vita2d_pgf_draw_text(font, x - offset, y, color, scale, text.c_str());
    vita2d_pgf_draw_text(font, x - offset + cycle, y, color, scale, text.c_str());
    // Do not leave scissor disabled: rest of the card (badges, chips) would spill
    // outside the panel while scrolling. Expand to full screen; caller re-asserts
    // the panel clip right after drawCatalogCard returns.
    vita2d_set_clip_rectangle(0, 0, SCREEN_W, SCREEN_H);
}
'''

new_fn = '''/** Soft horizontal marquee for long titles when focused (list card + detail header).
 *  parentClip* = outer panel/card scissor; animated path intersects name strip with it
 *  so glyphs never paint outside the catalog panel while scrolling. */
void drawMarqueeText(vita2d_pgf* font, int x, int y, int maxW, unsigned color, float scale,
                     const std::string& text, bool animate,
                     int parentClipL = 0, int parentClipT = 0,
                     int parentClipR = SCREEN_W, int parentClipB = SCREEN_H) {
    if (!font || text.empty() || maxW <= 8) return;
    const int tw = vita2d_pgf_text_width(font, scale, text.c_str());
    // IMPORTANT: do not disable global clipping on the static path — parent panels
    // (catalog grid / detail body) rely on their scissor staying active.
    if (tw <= maxW) {
        vita2d_pgf_draw_text(font, x, y, color, scale, text.c_str());
        return;
    }
    if (!animate) {
        // Static: pixel-accurate ellipsis so long names never spill past maxW.
        std::string s = text;
        const char* dots = "...";
        const int dotsW = vita2d_pgf_text_width(font, scale, dots);
        while (s.size() > 1 && vita2d_pgf_text_width(font, scale, s.c_str()) + dotsW > maxW)
            s.pop_back();
        while (!s.empty() && (unsigned char)s.back() < 0x80 && (s.back() == \' \' || s.back() == \'.\'))
            s.pop_back();
        s += dots;
        vita2d_pgf_draw_text(font, x, y, color, scale, s.c_str());
        return;
    }
    // Animated marquee: scissor = name strip ∩ parent panel (never leave panel).
    const int gap = 48;
    const int cycle = tw + gap;
    const uint64_t ms = sceKernelGetProcessTimeWide() / 1000ULL;
    const uint64_t period = static_cast<uint64_t>(cycle) * 28ULL + 1200ULL;
    const uint64_t t = ms % period;
    int offset = 0;
    if (t > 1200ULL) offset = static_cast<int>((t - 1200ULL) / 28ULL);
    if (offset > cycle) offset = cycle;
    int x0 = x;
    int y0 = y - 20;
    int x1 = x + maxW;
    int y1 = y + 6;
    if (x0 < parentClipL) x0 = parentClipL;
    if (y0 < parentClipT) y0 = parentClipT;
    if (x1 > parentClipR) x1 = parentClipR;
    if (y1 > parentClipB) y1 = parentClipB;
    if (x1 <= x0 || y1 <= y0) {
        // Fully outside parent panel — restore parent scissor and skip draw.
        vita2d_enable_clipping();
        vita2d_set_clip_rectangle(parentClipL, parentClipT, parentClipR, parentClipB);
        return;
    }
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x0, y0, x1, y1);
    vita2d_pgf_draw_text(font, x - offset, y, color, scale, text.c_str());
    vita2d_pgf_draw_text(font, x - offset + cycle, y, color, scale, text.c_str());
    // Restore parent panel/card scissor (never full-screen).
    vita2d_set_clip_rectangle(parentClipL, parentClipT, parentClipR, parentClipB);
}
'''

# The source uses normal quotes for space/dot checks - fix escaping in old_fn match
old_fn = old_fn.replace("\\' \\'", "' '")
old_fn = old_fn.replace("\\'.\\'", "'.'")
new_fn = new_fn.replace("\\' \\'", "' '")
new_fn = new_fn.replace("\\'.\\'", "'.'")

if old_fn not in cpp:
    # try reading exact block from file for diagnosis
    start = cpp.find("/** Soft horizontal marquee")
    if start < 0:
        raise SystemExit("marquee function not found at all")
    end = cpp.find("/** Word-wrap text", start)
    raise SystemExit("marquee block mismatch, len=" + str(end - start))

cpp = cpp.replace(old_fn, new_fn, 1)

# Pass panel clip into card name marquee
old_call = """        drawMarqueeText(font_, tx, y + (compact ? 24 : 28) + oy, nameMaxW, WHITE, nameSc, it.name, focus);
"""
new_call = """        drawMarqueeText(font_, tx, y + (compact ? 24 : 28) + oy, nameMaxW, WHITE, nameSc, it.name, focus,
                         clipL, clipT, clipR, clipB);
"""
if old_call not in cpp:
    raise SystemExit("card marquee call not found")
cpp = cpp.replace(old_call, new_call, 1)

CPP.write_text(cpp, encoding="utf-8")
print("OK marquee panel clip applied")
