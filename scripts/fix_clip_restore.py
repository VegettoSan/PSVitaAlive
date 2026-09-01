from pathlib import Path

p = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
text = p.read_text(encoding="utf-8")
changed = 0

def rep(old, new, label):
    global text, changed
    if old not in text:
        print(f"FAIL {label}")
        raise SystemExit(1)
    text = text.replace(old, new, 1)
    changed += 1
    print(f"OK {label}")

old_marq = r'''void drawMarqueeText(vita2d_pgf* font, int x, int y, int maxW, unsigned color, float scale,
                     const std::string& text, bool animate) {
    if (!font || text.empty() || maxW <= 8) return;
    const int tw = vita2d_pgf_text_width(font, scale, text.c_str());
    // Always clip so glyphs never bleed past the card/panel edge.
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x, y - 22, x + maxW, y + 8);
    if (tw <= maxW) {
        vita2d_pgf_draw_text(font, x, y, color, scale, text.c_str());
        vita2d_disable_clipping();
        return;
    }
    if (!animate) {
        // Static: shrink with "..." so long names never overflow the card.
        std::string s = text;
        const char* dots = "...";
        const int dotsW = vita2d_pgf_text_width(font, scale, dots);
        while (s.size() > 1 && vita2d_pgf_text_width(font, scale, s.c_str()) + dotsW > maxW)
            s.pop_back();
        while (!s.empty() && (unsigned char)s.back() < 0x80 && (s.back() == ' ' || s.back() == '.'))
            s.pop_back();
        s += dots;
        vita2d_pgf_draw_text(font, x, y, color, scale, s.c_str());
        vita2d_disable_clipping();
        return;
    }
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
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x, clipTop, x + maxW, clipBottom);
    vita2d_pgf_draw_text(font, x - offset, y, color, scale, text.c_str());
    vita2d_pgf_draw_text(font, x - offset + cycle, y, color, scale, text.c_str());
    vita2d_disable_clipping();
}'''

new_marq = r'''void drawMarqueeText(vita2d_pgf* font, int x, int y, int maxW, unsigned color, float scale,
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
        while (!s.empty() && (unsigned char)s.back() < 0x80 && (s.back() == ' ' || s.back() == '.'))
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
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x, clipTop, x + maxW, clipBottom);
    vita2d_pgf_draw_text(font, x - offset, y, color, scale, text.c_str());
    vita2d_pgf_draw_text(font, x - offset + cycle, y, color, scale, text.c_str());
    vita2d_disable_clipping();
}'''

rep(old_marq, new_marq, "drawMarqueeText")

old_full = r'''                drawCatalogCard(catalogView()[i], i, x + GRID_PAD + c * (cw + CARD_GAP), static_cast<int>(fy), cw, FULL_CARD_H, i == state_.focusIndex);
            }
        }
        vita2d_disable_clipping();'''

new_full = r'''                drawCatalogCard(catalogView()[i], i, x + GRID_PAD + c * (cw + CARD_GAP), static_cast<int>(fy), cw, FULL_CARD_H, i == state_.focusIndex);
                // Re-assert panel clip: focused card marquee disables global scissor.
                vita2d_enable_clipping();
                vita2d_set_clip_rectangle(x + 1, y + 1, x + w - 1, y + h - 1);
            }
        }
        vita2d_disable_clipping();'''

rep(old_full, new_full, "catalog full grid re-clip")

old_split = r'''            drawCatalogCard(catalogView()[i], i, x + GRID_PAD, static_cast<int>(fy), w - GRID_PAD * 2 - 4, SPLIT_CARD_H, i == state_.focusIndex);
        }
        vita2d_disable_clipping();'''

new_split = r'''            drawCatalogCard(catalogView()[i], i, x + GRID_PAD, static_cast<int>(fy), w - GRID_PAD * 2 - 4, SPLIT_CARD_H, i == state_.focusIndex);
            // Re-assert panel clip: focused card marquee disables global scissor.
            vita2d_enable_clipping();
            vita2d_set_clip_rectangle(x + 1, y + 1, x + w - 1, y + h - 1);
        }
        vita2d_disable_clipping();'''

rep(old_split, new_split, "catalog split list re-clip")

old_links = r'''    drawDetailLinks(it, cx, cursor, cw, linksH);
    cursor += linksH;'''

new_links = r'''    drawDetailLinks(it, cx, cursor, cw, linksH);
    // Link-row marquee can clear scissor; restore detail body clip.
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x + 2, top, x + w - 18, bottom);
    cursor += linksH;'''

rep(old_links, new_links, "detail links re-clip")

p.write_text(text, encoding="utf-8")
print("changed", changed, "size", p.stat().st_size)
