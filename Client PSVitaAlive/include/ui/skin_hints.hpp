#pragma once
#include "ui/skin_renderer.hpp"
#include "ui/ui_font.hpp"
#include <cstring>
#include <algorithm>

namespace psvitaalive::skin {

// Only call this for known action/hint labels, never arbitrary catalog data.
// Existing locale strings retain their translated action; physical control
// tokens are recognized at word boundaries, not replaced inside words.
struct ControlToken { const char* word; const Sprite* sprite; };
inline const ControlToken* tokenAt(const char* p, const char* begin) {
    using namespace controls_atlas;
    static const ControlToken tokens[] = {
        {"Croix directionnelle", &ctrl_dpad_all}, {"Steuerkreuz", &ctrl_dpad_all},
        {"Крестовина", &ctrl_dpad_all}, {"Direzionali", &ctrl_dpad_all},
        {"Direcional", &ctrl_dpad_all}, {"Cruceta", &ctrl_dpad_all},
        {"Arriba/Abajo", &ctrl_dpad_all}, {"Up/Down", &ctrl_dpad_all},
        {"Haut/Bas", &ctrl_dpad_all}, {"Hoch/Runter", &ctrl_dpad_all},
        {"Su/Giù", &ctrl_dpad_all}, {"Cima/Baixo", &ctrl_dpad_all},
        {"Вверх/Вниз", &ctrl_dpad_all}, {"D-Pad", &ctrl_dpad_all}, {"D-pad", &ctrl_dpad_all},
        {"TRIANGLE", &ctrl_triangle_classic}, {"Triangle", &ctrl_triangle_classic},
        {"Triángulo", &ctrl_triangle_classic}, {"Triângulo", &ctrl_triangle_classic},
        {"Triangolo", &ctrl_triangle_classic}, {"Dreieck", &ctrl_triangle_classic},
        {"Треугольник", &ctrl_triangle_classic},
        {"SQUARE", &ctrl_square_classic}, {"Square", &ctrl_square_classic},
        {"CUADRADO", &ctrl_square_classic}, {"Cuadrado", &ctrl_square_classic},
        {"Quadrado", &ctrl_square_classic}, {"Quadrato", &ctrl_square_classic},
        {"Quadrat", &ctrl_square_classic}, {"Carré", &ctrl_square_classic},
        {"Квадрат", &ctrl_square_classic},
        {"CIRCLE", &ctrl_circle_classic}, {"Circle", &ctrl_circle_classic},
        {"CÍRCULO", &ctrl_circle_classic}, {"Círculo", &ctrl_circle_classic},
        {"CIRCLE", &ctrl_circle_classic}, {"CROSS", &ctrl_cross_classic},
        {"SELECT", &ctrl_select}, {"Select", &ctrl_select},
        {"START", &ctrl_start}, {"Start", &ctrl_start},
        {"△", &ctrl_triangle_classic}, {"□", &ctrl_square_classic},
        {"○", &ctrl_circle_classic}, {"×", &ctrl_cross_classic},
        {"✕", &ctrl_cross_classic}, {"→", &ctrl_dpad_right}, {"←", &ctrl_dpad_left},
        {"X", &ctrl_cross_classic}, {"O", &ctrl_circle_classic}
    };
    auto boundary = [](char c) {
        return !c || c==' ' || c=='\t' || c==':' || c=='/' || c=='|' ||
               c=='+' || c=='(' || c==')' || c==',' || c==';' || c=='.';
    };
    if (p != begin && !boundary(p[-1])) return nullptr;
    for (const auto& t : tokens) {
        const auto n = std::strlen(t.word);
        if (std::strncmp(p,t.word,n)==0 && boundary(p[n])) return &t;
    }
    return nullptr;
}
inline int hintRun(const ui::UiFont* font, const char* text, int x, int base,
                   float scale, unsigned color, bool paint) {
    if (!text || !*text) return 0;
    const int start = x;
    const int size = std::max(16, static_cast<int>(scale * 31.f));
    const char* p=text;
    // Hints are short, but keep a bounded UTF-8-safe buffer for each text run.
    char buf[512];
    while (*p) {
        if (ready()) {
            // L/R is a pair, not a word glyph.
            if ((p==text || p[-1]==' ' || p[-1]=='|') &&
                std::strncmp(p,"L/R",3)==0 && (p[3]==':' || p[3]==' ' || !p[3])) {
                if (paint) { control(controls_atlas::ctrl_l,x,base-size+4,size);
                             control(controls_atlas::ctrl_r,x+size,base-size+4,size); }
                x += size*2+3; p+=3;
                if (*p==':') ++p;
                continue;
            }
            if (const auto* t=tokenAt(p,text)) {
                if (paint) control(*t->sprite,x,base-size+4,size);
                x += size+3; p += std::strlen(t->word);
                if (*p==':' || (*p==' ' && p[1]==':')) { if (*p==' ') ++p; ++p; }
                continue;
            }
        }
        const char* q=p;
        while (*q && q-p<static_cast<int>(sizeof(buf)-5)) {
            if (q!=p && ready() && (tokenAt(q,text) ||
                 ((q==text || q[-1]==' ' || q[-1]=='|') && std::strncmp(q,"L/R",3)==0))) break;
            ++q;
        }
        while (*q && (static_cast<unsigned char>(*q)&0xc0)==0x80) ++q;
        const auto n=static_cast<size_t>(q-p);
        if (!n) break;
        std::memcpy(buf,p,n); buf[n]=0;
        if (paint) ui::uiDrawText(font,x,base,color,scale,buf);
        x += ui::uiTextWidth(font,scale,buf);
        p=q;
    }
    return x-start;
}
inline void hint(const ui::UiFont* font, int x, int base, int width,
                 unsigned color, float scale, const char* text, bool centered=false) {
    if (!font || !text || width<=0) return;
    int w=hintRun(font,text,0,base,scale,color,false);
    while (scale>.40f && w>width) {
        scale-=.04f;
        w=hintRun(font,text,0,base,scale,color,false);
    }
    // Don't let translations or 150% custom fonts spill outside the row.
    const bool clipped=vita2d_get_clipping_enabled()!=0;
    int l=0,t=0,r=960,b=544;
    if (clipped) vita2d_get_clip_rectangle(&l,&t,&r,&b);
    const int left=std::max(l,x), top=std::max(t,base-34);
    const int right=std::min(r,x+width), bottom=std::min(b,base+8);
    if (left>=right || top>=bottom) return;
    if (!clipped) vita2d_enable_clipping();
    vita2d_set_clip_rectangle(left,top,right,bottom);
    hintRun(font,text,x+(centered?std::max(0,(width-w)/2):0),base,scale,color,true);
    if (clipped) vita2d_set_clip_rectangle(l,t,r,b); else vita2d_disable_clipping();
}
} // namespace psvitaalive::skin
