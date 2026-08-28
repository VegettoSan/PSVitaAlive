#!/usr/bin/env python3
"""Apply Markdown-lite support to the News modal (safe, idempotent)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HPP = ROOT / "Client PSVitaAlive/include/ui/full_catalog_screen.hpp"
CPP = ROOT / "Client PSVitaAlive/source/ui/full_catalog_screen.cpp"
MD_HPP = ROOT / "Client PSVitaAlive/include/ui/news_markdown.hpp"


def patch_header() -> None:
    text = HPP.read_text(encoding="utf-8")
    if "struct NewsDrawLine" in text:
        print("header: already has NewsDrawLine")
        return
    old = """    // News modal state.
    bool newsVisible_ = false;
    bool newsCheckedOnce_ = false;
    int newsFetchAttempts_ = 0;
    bool newsMarkSeenOnClose_ = false;
    std::string newsId_;
    std::string newsTitle_;
    std::string newsBody_;
    std::vector<std::string> newsLines_;
    int newsScrollLine_ = 0;
    float visualNewsScroll_ = 0.f;"""
    new = """    /** One rendered line of the News modal (Markdown-lite). */
    struct NewsDrawLine {
        std::string text;
        float scale = 0.55f;
        unsigned color = 0;
        int indentPx = 0;
        int heightPx = 22;
        bool isHr = false;
        bool isBlank = false;
        bool emphasize = false;
    };

    // News modal state.
    bool newsVisible_ = false;
    bool newsCheckedOnce_ = false;
    int newsFetchAttempts_ = 0;
    bool newsMarkSeenOnClose_ = false;
    std::string newsId_;
    std::string newsTitle_;
    std::string newsBody_;
    std::vector<NewsDrawLine> newsLines_;
    int newsScrollLine_ = 0;
    float visualNewsScroll_ = 0.f;"""
    if old not in text:
        raise SystemExit("header pattern not found — unexpected file layout")
    HPP.write_text(text.replace(old, new), encoding="utf-8")
    print("header: NewsDrawLine added")


def patch_cpp() -> None:
    cpp = CPP.read_text(encoding="utf-8")
    if "news_md::drawInlineMarkdown" in cpp and "news_markdown.hpp" in cpp:
        print("cpp: already markdown-enabled")
        return

    if "news_markdown.hpp" not in cpp:
        cpp = cpp.replace(
            '#include "ui/full_catalog_screen.hpp"',
            '#include "ui/full_catalog_screen.hpp"\n#include "ui/news_markdown.hpp"',
            1,
        )
        print("cpp: include added")

    new_build = """newsCheckedOnce_ = true;
    newsId_ = item.id;
    newsTitle_ = item.title.empty() ? "News" : item.title;
    newsBody_ = item.body;
    newsLines_.clear();
    {
        // Markdown-lite: headings, lists, HR, **bold**, *italic*, `code`.
        const int maxTextW = 600;
        size_t pos = 0;
        const std::string& b = newsBody_;
        while (pos <= b.size()) {
            size_t endLine = b.find('\\n', pos);
            std::string raw;
            if (endLine == std::string::npos) {
                raw = b.substr(pos);
                pos = b.size() + 1;
            } else {
                raw = b.substr(pos, endLine - pos);
                pos = endLine + 1;
            }

            news_md::ParsedLine pl = news_md::classifyRawLine(raw);

            NewsDrawLine base;
            base.color = 0;
            if (pl.kind == news_md::Kind::Blank) {
                base.isBlank = true;
                base.heightPx = 12;
                base.text.clear();
                newsLines_.push_back(base);
                if (endLine == std::string::npos) break;
                continue;
            }
            if (pl.kind == news_md::Kind::Hr) {
                base.isHr = true;
                base.heightPx = 16;
                base.text.clear();
                newsLines_.push_back(base);
                if (endLine == std::string::npos) break;
                continue;
            }

            float scale = 0.55f;
            int height = 22;
            int indent = 0;
            bool emphasize = false;
            if (pl.kind == news_md::Kind::H1) {
                scale = 0.88f; height = 32; emphasize = true;
            } else if (pl.kind == news_md::Kind::H2) {
                scale = 0.74f; height = 28; emphasize = true;
            } else if (pl.kind == news_md::Kind::H3) {
                scale = 0.64f; height = 24; emphasize = true;
            } else if (pl.kind == news_md::Kind::List) {
                scale = 0.55f; height = 22; indent = 12; emphasize = true;
            }

            std::string content = pl.text;
            if (pl.kind == news_md::Kind::List)
                content = std::string("• ") + content;

            if (font_) {
                const std::string measure = news_md::plainForWidth(content);
                auto wrapped = wrapTextToWidth(font_, scale, measure, maxTextW - indent);
                if (wrapped.size() <= 1) {
                    NewsDrawLine dl = base;
                    dl.text = content;
                    dl.scale = scale;
                    dl.heightPx = height;
                    dl.indentPx = indent;
                    dl.emphasize = emphasize;
                    newsLines_.push_back(std::move(dl));
                } else {
                    for (size_t wi = 0; wi < wrapped.size(); ++wi) {
                        NewsDrawLine dl = base;
                        dl.text = wrapped[wi];
                        dl.scale = scale;
                        dl.heightPx = (wi == 0 ? height : 22);
                        dl.indentPx = indent + (wi > 0 && pl.kind == news_md::Kind::List ? 14 : 0);
                        dl.emphasize = emphasize;
                        newsLines_.push_back(std::move(dl));
                    }
                }
            } else {
                NewsDrawLine dl = base;
                dl.text = content;
                dl.scale = scale;
                dl.heightPx = height;
                dl.indentPx = indent;
                dl.emphasize = emphasize;
                newsLines_.push_back(std::move(dl));
            }
            if (endLine == std::string::npos) break;
        }
        if (newsLines_.empty()) {
            NewsDrawLine dl;
            dl.isBlank = true;
            dl.heightPx = 12;
            newsLines_.push_back(dl);
        }
    }
    newsScrollLine_ = 0;
    visualNewsScroll_ = 0.f;"""

    cpp2, n = re.subn(
        r"newsCheckedOnce_ = true;\n    newsId_ = item\.id;.*?visualNewsScroll_ = 0\.f;",
        lambda _m: new_build,
        cpp,
        count=1,
        flags=re.S,
    )
    if n != 1:
        raise SystemExit(f"cpp build-block replace failed (n={n})")
    cpp = cpp2
    print("cpp: runNewsCheck body builder replaced")

    new_draw = """void FullCatalogScreen::drawNewsOverlay() {
    if (!newsVisible_ || !font_) return;
    const unsigned BLACK = RGBA8(0, 0, 0, 255);
    const unsigned CODE_COL = RGBA8(0x7E, 0xC8, 0xFF, 255);
    const int w = 700, h = 420;
    const int x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 140));
    vita2d_draw_rectangle(x, y, w, h, PANEL);
    vita2d_draw_rectangle(x, y, w, 3, ACCENT);
    vita2d_draw_rectangle(x, y + 3, 3, h - 6, ACCENT);
    vita2d_draw_rectangle(x + w - 3, y + 3, 3, h - 6, BORDER);
    vita2d_draw_rectangle(x, y + h - 3, w, 3, BORDER);

    vita2d_pgf_draw_text(font_, x + 24, y + 34, ACCENT, 0.62f, "NEWS");
    vita2d_pgf_draw_text(font_, x + 24, y + 64, WHITE, 0.78f,
                         ellipsize(newsTitle_, 64).c_str());

    const int textTop = y + 88;
    const int textBottom = y + h - 56;
    const int lineH = 22;
    const int maxVisible = std::max(1, (textBottom - textTop) / lineH);
    int total = (int)newsLines_.size();
    const int maxScroll = std::max(0, total - maxVisible);
    if (newsScrollLine_ < 0) newsScrollLine_ = 0;
    if (newsScrollLine_ > maxScroll) newsScrollLine_ = maxScroll;
    if (visualNewsScroll_ < 0.f) visualNewsScroll_ = 0.f;
    if (visualNewsScroll_ > (float)maxScroll) visualNewsScroll_ = (float)maxScroll;

    const float vs = visualNewsScroll_;
    const int start = (int)std::floor(vs);
    const float frac = vs - (float)start;

    auto heightAt = [&](int idx) -> int {
        if (idx < 0 || idx >= total) return lineH;
        int hp = newsLines_[(size_t)idx].heightPx;
        return hp > 0 ? hp : lineH;
    };

    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x + 20, textTop, x + w - 22, textBottom);
    int cursorY = textTop - (int)(frac * (float)heightAt(start));
    if (start > 0) cursorY -= heightAt(start - 1);
    for (int i = std::max(0, start - 1); i < total; ++i) {
        const NewsDrawLine& nl = newsLines_[(size_t)i];
        const int hp = heightAt(i);
        if (cursorY > textBottom + hp) break;
        if (cursorY + hp >= textTop - hp) {
            if (nl.isHr) {
                const int hy = cursorY + hp / 2;
                vita2d_draw_rectangle(x + 28, hy, w - 56, 2, BORDER);
            } else if (!nl.isBlank && !nl.text.empty()) {
                float scale = nl.scale > 0.01f ? nl.scale : 0.55f;
                unsigned baseCol = TEXT;
                unsigned boldCol = WHITE;
                if (nl.emphasize) baseCol = WHITE;
                if (scale >= 0.80f) baseCol = ACCENT;
                else if (scale >= 0.68f) baseCol = WHITE;
                const int tx = x + 28 + nl.indentPx;
                const int baseY = cursorY + std::min(hp - 4, (int)(scale * 18.f) + 4);
                news_md::drawInlineMarkdown(font_, tx, baseY, scale, baseCol, boldCol, CODE_COL, nl.text);
            }
        }
        cursorY += hp;
        if (i >= start + maxVisible + 3) break;
    }
    vita2d_disable_clipping();

    if (total > maxVisible) {
        const int trackX = x + w - 16;
        const int trackY = textTop;
        const int trackH = textBottom - textTop;
        vita2d_draw_rectangle(trackX, trackY, 4, trackH, BORDER);
        const float thumbH = std::max(22.f, (float)trackH * ((float)maxVisible / (float)total));
        const float thumbT = (maxScroll > 0) ? (vs / (float)maxScroll) : 0.f;
        const int thumbY = trackY + (int)(thumbT * ((float)trackH - thumbH));
        vita2d_draw_rectangle(trackX, thumbY, 4, (int)thumbH, ACCENT);
        char scr[32];
        sceClibSnprintf(scr, sizeof(scr), "%d/%d", std::min(total, start + 1), total);
        vita2d_pgf_draw_text(font_, x + w - 90, y + 34, DIM, 0.48f, scr);
    }

    const int by = y + h - 48, bw = 220, bh = 36;
    vita2d_draw_rectangle(x + (w - bw) / 2, by, bw, bh, ACCENT);
    {
        const char* clab = "O  Close";
        const float sc = 0.68f;
        const int tw = vita2d_pgf_text_width(font_, sc, clab);
        vita2d_pgf_draw_text(font_, x + (w - bw) / 2 + (bw - tw) / 2, by + 25, BLACK, sc, clab);
    }
    vita2d_pgf_draw_text(font_, x + 24, y + h - 14, DIM, 0.46f, "D-Pad: scroll   Circle: close");
}

void FullCatalogScreen::drawReportChip()"""

    cpp3, n2 = re.subn(
        r"void FullCatalogScreen::drawNewsOverlay\(\) \{.*?void FullCatalogScreen::drawReportChip\(\)",
        lambda _m: new_draw,
        cpp,
        count=1,
        flags=re.S,
    )
    if n2 != 1:
        raise SystemExit(f"cpp drawNewsOverlay replace failed (n={n2})")
    cpp = cpp3
    print("cpp: drawNewsOverlay replaced")

    if cpp.count("{") != cpp.count("}"):
        raise SystemExit(f"brace mismatch {{ {cpp.count('{')} }} {cpp.count('}')}")

    CPP.write_text(cpp, encoding="utf-8")
    print(f"cpp: written ({len(cpp)} bytes)")


def main() -> int:
    if not MD_HPP.is_file():
        raise SystemExit(f"missing {MD_HPP}")
    if not HPP.is_file() or not CPP.is_file():
        raise SystemExit("missing client UI sources")
    patch_header()
    patch_cpp()
    print("OK: News Markdown-lite applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
