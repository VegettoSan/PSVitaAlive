#!/usr/bin/env python3
"""UI polish: data-request button, detail focus, loading copy, larger small text, title marquee."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "Client PSVitaAlive/source/ui/full_catalog_screen.cpp"
MAIN = ROOT / "Client PSVitaAlive/source/main.cpp"


def must(path: Path, old: str, new: str, label: str, optional: bool = False) -> str:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        if optional:
            print(f"skip {label} (needle missing)")
            return text
        if new[: min(50, len(new))] in text and old not in text:
            print(f"skip {label} (already applied)")
            return text
        sys.exit(f"FAIL {label}: needle not found")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"ok {label}")
    return path.read_text(encoding="utf-8")


# ---------------------------------------------------------------------------
# 1) Marquee helper next to ellipsize
# ---------------------------------------------------------------------------
must(
    CPP,
    'std::string ellipsize(const std::string&s,size_t n){if(s.size()<=n)return s;if(n<=3)return s.substr(0,n);return s.substr(0,n-3)+"...";}',
    '''std::string ellipsize(const std::string&s,size_t n){if(s.size()<=n)return s;if(n<=3)return s.substr(0,n);return s.substr(0,n-3)+"...";}

/** Soft horizontal marquee for long titles when focused (list card + detail header). */
void drawMarqueeText(vita2d_pgf* font, int x, int y, int maxW, unsigned color, float scale,
                     const std::string& text, bool animate) {
    if (!font || text.empty() || maxW <= 8) return;
    const int tw = vita2d_pgf_text_width(font, scale, text.c_str());
    if (!animate || tw <= maxW) {
        vita2d_pgf_draw_text(font, x, y, color, scale, text.c_str());
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
}
''',
    "marquee helper",
)

# ---------------------------------------------------------------------------
# 2) Open detail → focus DETAIL panel (not list)
# ---------------------------------------------------------------------------
must(
    CPP,
    """state_.mode=state_.mode==UiMode::OPENING_DETAIL?UiMode::SPLIT_DETAIL:UiMode::FULL_CATALOG;
    state_.activePanel=UiPanel::Catalog;
    clampCatalogScroll();""",
    """const bool opening = (state_.mode == UiMode::OPENING_DETAIL);
    state_.mode = opening ? UiMode::SPLIT_DETAIL : UiMode::FULL_CATALOG;
    // After opening detail, put focus on DETAIL so the user can browse metadata first.
    state_.activePanel = opening ? UiPanel::Detail : UiPanel::Catalog;
    clampCatalogScroll();""",
    "detail focus on open",
)

# ---------------------------------------------------------------------------
# 3) Data/Game Files request button — larger + distinctive amber color
# ---------------------------------------------------------------------------
must(
    CPP,
    """        if (itemEligibleForDataRequest(it)) {
            // Below Select links when present; same header column when no links.
            const int rby = !it.linkDetails.empty() ? (by + bh + 6) : by;
            const int rbx = bx, rbw = bw, rbh = 26;
            vita2d_draw_rectangle(rbx, rby, rbw, rbh, SURFACE2);
            vita2d_draw_rectangle(rbx, rby, rbw, 1, ACCENT);
            vita2d_draw_rectangle(rbx, rby, 1, rbh, ACCENT);
            vita2d_draw_rectangle(rbx, rby + rbh - 1, rbw, 1, ACCENT);
            vita2d_draw_rectangle(rbx + rbw - 1, rby, 1, rbh, ACCENT);
            vita2d_pgf_draw_text(font_, rbx + 6, rby + 18, ACCENT, 0.48f, "□ Request data");
        }""",
    """        if (itemEligibleForDataRequest(it)) {
            // Below Select links when present; same header column when no links.
            // Amber identity color so it stands out from green "Select links".
            const unsigned REQ = RGBA8(0xFF, 0xB0, 0x20, 255);
            const unsigned REQ_BG = RGBA8(0x3A, 0x2A, 0x10, 255);
            const int rby = !it.linkDetails.empty() ? (by + bh + 6) : by;
            const int rbx = bx, rbw = bw, rbh = 30;
            vita2d_draw_rectangle(rbx, rby, rbw, rbh, REQ_BG);
            vita2d_draw_rectangle(rbx, rby, rbw, 2, REQ);
            vita2d_draw_rectangle(rbx, rby, 2, rbh, REQ);
            vita2d_draw_rectangle(rbx, rby + rbh - 2, rbw, 2, REQ);
            vita2d_draw_rectangle(rbx + rbw - 2, rby, 2, rbh, REQ);
            vita2d_pgf_draw_text(font_, rbx + 6, rby + 20, REQ, 0.58f, "□ Request data");
        }""",
    "data-request button style",
)

# ---------------------------------------------------------------------------
# 4) Card name marquee when focused + slightly larger meta
# ---------------------------------------------------------------------------
must(
    CPP,
    """    int tx = x + is + 20 + ox;
    vita2d_pgf_draw_text(font_, tx, y + 25 + oy, WHITE, focus ? 0.80f : 0.76f, ellipsize(it.name, h >= 100 ? 24 : 22).c_str());
    vita2d_pgf_draw_text(font_, tx, y + 45 + oy, TEXT, 0.64f, ellipsize(it.author.empty() ? "Unknown author" : it.author, 20).c_str());
    vita2d_pgf_draw_text(font_, tx, y + 64 + oy, colorForStatus(it.status), 0.62f, ellipsize(it.status, 16).c_str());""",
    """    int tx = x + is + 20 + ox;
    {
        const float nameSc = focus ? 0.84f : 0.78f;
        const int nameMaxW = std::max(40, (x + ox + ww) - tx - 10);
        drawMarqueeText(font_, tx, y + 25 + oy, nameMaxW, WHITE, nameSc, it.name, focus);
    }
    vita2d_pgf_draw_text(font_, tx, y + 45 + oy, TEXT, 0.68f, ellipsize(it.author.empty() ? "Unknown author" : it.author, 20).c_str());
    vita2d_pgf_draw_text(font_, tx, y + 64 + oy, colorForStatus(it.status), 0.66f, ellipsize(it.status, 16).c_str());""",
    "card name marquee + slightly larger meta",
)

# ---------------------------------------------------------------------------
# 5) Detail title marquee when detail panel active
# ---------------------------------------------------------------------------
must(
    CPP,
    """    vita2d_pgf_draw_text(font_, titleX, y + 29, WHITE, 0.82f, ellipsize(it.name, active ? 18 : 24).c_str());
    vita2d_pgf_draw_text(font_, titleX, y + 50, TEXT, 0.64f, ellipsize(it.author.empty() ? "Unknown author" : it.author, 20).c_str());""",
    """    {
        // Leave room for Select-links / Request-data buttons on the right.
        const int titleMaxW = std::max(60, (x + w - 150) - titleX);
        drawMarqueeText(font_, titleX, y + 29, titleMaxW, WHITE, 0.86f, it.name, active);
    }
    vita2d_pgf_draw_text(font_, titleX, y + 50, TEXT, 0.68f, ellipsize(it.author, 20).c_str());""",
    "detail title marquee",
)

# ---------------------------------------------------------------------------
# 6) Bump very small text scales (readability on real Vita screen)
# ---------------------------------------------------------------------------
must(
    CPP,
    'vita2d_pgf_draw_text(font_, x + 10, ry + 31, f ? BG : DIM, 0.50f, ellipsize(meta, badgeW ? 28 : 42).c_str());',
    'vita2d_pgf_draw_text(font_, x + 10, ry + 31, f ? BG : DIM, 0.58f, ellipsize(meta, badgeW ? 28 : 42).c_str());',
    "detail link meta scale",
)

must(
    CPP,
    'vita2d_pgf_draw_text(font_, x + 4, y + 14, ACCENT, 0.52f, "INSTALL ALL");',
    'vita2d_pgf_draw_text(font_, x + 4, y + 14, ACCENT, 0.58f, "INSTALL ALL");',
    "install all label scale",
    optional=True,
)

must(
    CPP,
    'vita2d_pgf_draw_text(font_, x + 4, ry + 14, ACCENT, 0.52f, linkSectionTitle(row.section));',
    'vita2d_pgf_draw_text(font_, x + 4, ry + 14, ACCENT, 0.58f, linkSectionTitle(row.section));',
    "link section title scale",
    optional=True,
)

must(
    CPP,
    'vita2d_pgf_draw_text(font_, barX, stripY + 28, ACCENT, 0.78f, ellipsize(phase, 40).c_str());',
    'vita2d_pgf_draw_text(font_, barX, stripY + 28, ACCENT, 0.84f, ellipsize(phase, 40).c_str());',
    "loading phase scale",
)

must(
    CPP,
    'vita2d_pgf_draw_text(font_, barX, stripY + 52, WHITE, 0.58f, ellipsize(detail, 78).c_str());',
    'vita2d_pgf_draw_text(font_, barX, stripY + 52, WHITE, 0.66f, ellipsize(detail, 78).c_str());',
    "loading detail scale",
)

must(
    CPP,
    'vita2d_pgf_draw_text(font_, x + 24, y + h - 14, DIM, 0.46f, "D-Pad: scroll   Circle: close");',
    'vita2d_pgf_draw_text(font_, x + 24, y + h - 14, DIM, 0.54f, "D-Pad: scroll   Circle: close");',
    "news hint scale",
    optional=True,
)

must(
    CPP,
    'vita2d_pgf_draw_text(font_, x + 10 + ox, y + h - 10 + oy, DIM, 0.56f, ellipsize(meta, 22).c_str());',
    'vita2d_pgf_draw_text(font_, x + 10 + ox, y + h - 10 + oy, DIM, 0.60f, ellipsize(meta, 22).c_str());',
    "card bottom meta scale",
)

must(
    CPP,
    'const float sc = 0.56f;\n                const int tw = vita2d_pgf_text_width(font_, sc, label.c_str());',
    'const float sc = 0.60f;\n                const int tw = vita2d_pgf_text_width(font_, sc, label.c_str());',
    "size chip scale",
    optional=True,
)

must(
    CPP,
    'vita2d_pgf_draw_text(font_, bx + 10, by + 19, linkOn ? BG : ACCENT, 0.56f, linkOn ? "△ Exit link mode" : "△ Select links");',
    'vita2d_pgf_draw_text(font_, bx + 10, by + 19, linkOn ? BG : ACCENT, 0.58f, linkOn ? "△ Exit link mode" : "△ Select links");',
    "select links scale",
)

# ---------------------------------------------------------------------------
# 7) Clearer catalog loading messages (main.cpp)
# ---------------------------------------------------------------------------
must(
    MAIN,
    'screen.setCatalogLoading(true,psvitaalive::ui::catalogName(psvitaalive::ui::CatalogType::Homebrew),0,0,"Checking catalog cache...");',
    'screen.setCatalogLoading(true,psvitaalive::ui::catalogName(psvitaalive::ui::CatalogType::Homebrew),0,0,"Loading Homebrew catalog (cache first, then update)...");',
    "startup loading message",
)

must(
    MAIN,
    'csBusy.message.empty()?"Loading catalog...":csBusy.message);',
    'csBusy.message.empty()?"Loading this catalog — please wait (not preloaded at startup)...":csBusy.message);',
    "runtime loading fallback message",
)

must(
    MAIN,
    'screen.setCatalogLoading(true,psvitaalive::ui::catalogName(readyCatalog),0,0,"Loading next catalog...");',
    'screen.setCatalogLoading(true,psvitaalive::ui::catalogName(readyCatalog),0,0,"Loading next catalog — please wait...");',
    "next catalog message",
    optional=True,
)

# Improve catalog manager status messages for clearer UX (optional if strings still match)
CM = ROOT / "Client PSVitaAlive/source/catalog/catalog_manager.cpp"
if CM.exists():
    must(
        CM,
        'status_.message = "Checking catalog cache...";',
        'status_.message = "Loading catalog (local cache, then network)...";',
        "cm checking message",
        optional=True,
    )
    must(
        CM,
        'status_.message = "Switching catalog...";',
        'status_.message = "Switching catalog — please wait...";',
        "cm switching message",
        optional=True,
    )

print("ALL UI POLISH OK")
