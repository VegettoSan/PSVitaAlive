#pragma once
// Markdown-lite helpers for the News modal only (does not affect other UI).
#include <vita2d.h>
#include <string>
#include <cctype>

namespace psvitaalive {
namespace ui {
namespace news_md {

enum class Kind { Body, H1, H2, H3, List, Hr, Blank };

struct ParsedLine {
    Kind kind = Kind::Body;
    std::string text;
};

inline std::string trimLeft(std::string s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

inline ParsedLine classifyRawLine(std::string raw) {
    ParsedLine pl;
    std::string t = raw;
    while (!t.empty() && (t.back() == '\r' || t.back() == ' ' || t.back() == '\t')) t.pop_back();
    if (t.empty()) { pl.kind = Kind::Blank; return pl; }
    std::string s = trimLeft(t);
    auto isHr = [](const std::string& x) {
        if (x.size() < 3) return false;
        char c = x[0];
        if (c != '-' && c != '*' && c != '_') return false;
        for (char ch : x) {
            if (ch != c && ch != ' ' && ch != '\t') return false;
        }
        int n = 0;
        for (char ch : x) if (ch == c) ++n;
        return n >= 3;
    };
    if (isHr(s)) { pl.kind = Kind::Hr; return pl; }
    if (s.size() >= 2 && s[0] == '#') {
        int level = 0;
        while (level < (int)s.size() && s[level] == '#') ++level;
        if (level >= 1 && level <= 3 && level < (int)s.size() && s[level] == ' ') {
            pl.text = trimLeft(s.substr(level + 1));
            if (level == 1) pl.kind = Kind::H1;
            else if (level == 2) pl.kind = Kind::H2;
            else pl.kind = Kind::H3;
            return pl;
        }
    }
    if (s.size() >= 2 && (s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') {
        pl.kind = Kind::List;
        pl.text = s.substr(2);
        return pl;
    }
    pl.kind = Kind::Body;
    pl.text = s;
    return pl;
}

inline std::string plainForWidth(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        if (i + 1 < in.size() && in[i] == '*' && in[i + 1] == '*') {
            size_t j = in.find("**", i + 2);
            if (j != std::string::npos) {
                out.append(in, i + 2, j - (i + 2));
                i = j + 2;
                continue;
            }
        }
        if (in[i] == '*' || in[i] == '_') {
            char m = in[i];
            size_t j = in.find(m, i + 1);
            if (j != std::string::npos) {
                out.append(in, i + 1, j - (i + 1));
                i = j + 1;
                continue;
            }
        }
        if (in[i] == '`') {
            size_t j = in.find('`', i + 1);
            if (j != std::string::npos) {
                out.append(in, i + 1, j - (i + 1));
                i = j + 1;
                continue;
            }
        }
        out.push_back(in[i]);
        ++i;
    }
    return out;
}

inline void drawInlineMarkdown(vita2d_pgf* font, int x, int baselineY, float scale,
                               unsigned baseCol, unsigned boldCol, unsigned codeCol,
                               const std::string& text) {
    if (!font) return;
    int cx = x;
    size_t i = 0;
    auto drawSeg = [&](const std::string& seg, unsigned col, float sc) {
        if (seg.empty()) return;
        vita2d_pgf_draw_text(font, cx, baselineY, col, sc, seg.c_str());
        cx += vita2d_pgf_text_width(font, sc, seg.c_str());
    };
    while (i < text.size()) {
        if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*') {
            size_t j = text.find("**", i + 2);
            if (j != std::string::npos) {
                drawSeg(text.substr(i + 2, j - (i + 2)), boldCol, scale);
                i = j + 2;
                continue;
            }
        }
        if (text[i] == '`') {
            size_t j = text.find('`', i + 1);
            if (j != std::string::npos) {
                drawSeg(text.substr(i + 1, j - (i + 1)), codeCol, scale * 0.96f);
                i = j + 1;
                continue;
            }
        }
        if (text[i] == '*' || text[i] == '_') {
            char m = text[i];
            size_t j = text.find(m, i + 1);
            if (j != std::string::npos) {
                drawSeg(text.substr(i + 1, j - (i + 1)), boldCol, scale);
                i = j + 1;
                continue;
            }
        }
        size_t next = text.size();
        for (size_t k = i; k < text.size(); ++k) {
            if (text[k] == '`' || text[k] == '*' || text[k] == '_') { next = k; break; }
        }
        drawSeg(text.substr(i, next - i), baseCol, scale);
        i = next;
    }
}

} // namespace news_md
} // namespace ui
} // namespace psvitaalive
