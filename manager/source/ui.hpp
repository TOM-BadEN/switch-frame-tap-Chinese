/*
 * The manager's look (v0.7.14): the PC viewer's theme (tools/raw-recv/ui.h) -
 * dark surfaces, a sky-blue accent, rounded cards whose corners come from one
 * anti-aliased disc texture, pill switches. SDL2 and SDL2_ttf only, so the PC
 * preview (tools/manager-preview) draws the same screens.
 */
#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include "../../common/utf8.h"

namespace ui {

    constexpr int W = 1280, H = 720;

    constexpr SDL_Color Bg    = {  12,  14,  18, 255 };
    constexpr SDL_Color Surf  = {  21,  24,  30, 255 };
    constexpr SDL_Color Surf2 = {  31,  35,  43, 255 };
    constexpr SDL_Color Surf3 = {  44,  49,  59, 255 };
    constexpr SDL_Color Line  = { 255, 255, 255,  20 };
    constexpr SDL_Color Ink   = { 236, 238, 243, 255 };
    constexpr SDL_Color Dim   = { 152, 160, 175, 255 };
    constexpr SDL_Color Faint = { 100, 108, 122, 255 };
    constexpr SDL_Color Acc   = {  56, 189, 248, 255 };
    constexpr SDL_Color Good  = {  52, 211, 153, 255 };
    constexpr SDL_Color Warn  = { 251, 191,  36, 255 };
    constexpr SDL_Color Bad   = { 248, 113, 113, 255 };
    /* the brand mark's Joy-Con colours (docs/hbas-banner.png) */
    constexpr SDL_Color JoyBlue = {   0, 178, 230, 255 };
    constexpr SDL_Color JoyRed  = { 255,  76,  62, 255 };

    inline SDL_Color Alpha(SDL_Color c, Uint8 a) { c.a = a; return c; }
    inline bool Same(SDL_Color a, SDL_Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

    inline SDL_Renderer *g_ren = nullptr;

    /* ---- text ------------------------------------------------------------ */

    enum Size { Small, Body, Label, Title, Big, SizeCount };
    inline constexpr int FontPx[SizeCount] = { 17, 19, 22, 30, 40 };
    inline TTF_Font *g_font[SizeCount] = {};
    inline TTF_Font *g_glyphs = nullptr;          /* the console's button glyphs (NintendoExt), if there */

    struct CachedText { SDL_Texture *tex; int w, h; };
    inline std::unordered_map<std::string, CachedText> g_cache;

    inline void LoadFonts(const void *std_data, size_t std_size, const void *ext_data, size_t ext_size) {
        for (int i = 0; i < SizeCount; ++i) { g_font[i] = TTF_OpenFontRW(SDL_RWFromConstMem(std_data, static_cast<int>(std_size)), 1, FontPx[i]); }
        if (ext_data != nullptr) { g_glyphs = TTF_OpenFontRW(SDL_RWFromConstMem(ext_data, static_cast<int>(ext_size)), 1, 24); }
    }

    inline void FreeAll() {
        for (auto &e : g_cache) { SDL_DestroyTexture(e.second.tex); }
        g_cache.clear();
        for (auto *&f : g_font) { if (f) { TTF_CloseFont(f); f = nullptr; } }
        if (g_glyphs) { TTF_CloseFont(g_glyphs); g_glyphs = nullptr; }
    }

    /* Wrap UTF-8 text using font metrics, including Chinese without spaces.
     * text is the source, font is the active system font, maxWidth is pixels.
     * Returns text with explicit line breaks; never splits encoded characters. */
    inline std::string wrapText(const std::string &text, TTF_Font *font, int maxWidth) {
        std::string result; /* Output consumed by SDL_ttf's wrapped renderer. */
        const char *start = text.c_str(); /* First byte of the current line. */
        while (*start) {
            const char *cursor = start; /* Boundary of the next candidate scalar. */
            size_t length = 0, lastBreak = 0; /* Accepted line length and last word/CJK boundary, in bytes. */
            while (*cursor && *cursor != '\n') {
                const char *next = cursor; /* First byte after this character. */
                const uint32_t code = sftUtf8Next(&next); /* Character determining break opportunities. */
                const size_t candidate = static_cast<size_t>(next - start); /* Candidate line length in bytes. */
                const std::string line(start, candidate); /* NUL-terminated candidate for font measurement. */
                int width = 0, height = 0; /* Candidate extent reported by the active font. */
                TTF_SizeUTF8(font, line.c_str(), &width, &height);
                if (length && width > maxWidth) { if (lastBreak) { length = lastBreak; } break; }
                length = candidate;
                if (code == ' ' || code >= 0x2E80) { lastBreak = code == ' ' ? static_cast<size_t>(cursor - start) : length; }
                cursor = next;
            }
            result.append(start, length);
            start += length;
            if (*start == '\n') { ++start; }
            while (*start == ' ') { ++start; }
            if (*start) { result += '\n'; }
        }
        return result;
    }

    inline const CachedText *Render(const std::string &s, int size, SDL_Color c, int wrap = 0, bool glyph = false) {
        if (s.empty()) { return nullptr; }
        TTF_Font *f = glyph ? g_glyphs : g_font[size];
        if (f == nullptr) { return nullptr; }
        char key[48];
        std::snprintf(key, sizeof(key), "%d|%d|%02x%02x%02x%02x|%d|", size, wrap, c.r, c.g, c.b, c.a, glyph ? 1 : 0);
        const std::string k = key + s;
        if (auto it = g_cache.find(k); it != g_cache.end()) { return &it->second; }
        if (g_cache.size() > 500) {
            for (auto &e : g_cache) { SDL_DestroyTexture(e.second.tex); }
            g_cache.clear();
        }
        SDL_Color opaque = c;
        opaque.a = 255;
        const std::string wrapped = wrap > 0 ? wrapText(s, f, wrap) : s; /* Explicit Chinese/word breaks for rendering and cached height. */
        SDL_Surface *surf = wrap > 0 ? TTF_RenderUTF8_Blended_Wrapped(f, wrapped.c_str(), opaque, static_cast<Uint32>(wrap)) : TTF_RenderUTF8_Blended(f, s.c_str(), opaque);
        if (surf == nullptr) { return nullptr; }
        CachedText t = { SDL_CreateTextureFromSurface(g_ren, surf), surf->w, surf->h };
        SDL_FreeSurface(surf);
        if (t.tex != nullptr && c.a != 255) { SDL_SetTextureAlphaMod(t.tex, c.a); }
        return &g_cache.emplace(k, t).first->second;
    }

    inline int TextW(const std::string &s, int size) { const CachedText *t = Render(s, size, Ink); return t ? t->w : 0; }

    /* align: 0 left, 1 centre, 2 right; y is the top. Returns the height drawn. */
    inline int Text(const std::string &s, int x, int y, int size, SDL_Color c, int align = 0, int wrap = 0) {
        const CachedText *t = Render(s, size, c, wrap);
        if (t == nullptr) { return 0; }
        SDL_Rect r = { x - (align == 1 ? t->w / 2 : align == 2 ? t->w : 0), y, t->w, t->h };
        SDL_RenderCopy(g_ren, t->tex, nullptr, &r);
        return t->h;
    }

    /* the height a wrapped text takes */
    inline int TextH(const std::string &s, int size, int wrap) { const CachedText *t = Render(s, size, Dim, wrap); return t ? t->h : 0; }

    /* ---- shapes ---------------------------------------------------------- */

    inline void Fill(int x, int y, int w, int h, SDL_Color c) {
        SDL_SetRenderDrawColor(g_ren, c.r, c.g, c.b, c.a);
        SDL_Rect r = { x, y, w, h };
        SDL_RenderFillRect(g_ren, &r);
    }

    /* a 96 px disc, 4x4 supersampled: the corners of every rounded shape */
    inline SDL_Texture *g_disc = nullptr;
    inline void DiscInit() {
        if (g_disc != nullptr) { return; }
        constexpr int D = 96;
        static Uint32 px[D * D];
        const float c = D / 2.0f, rr = D / 2.0f;
        for (int y = 0; y < D; ++y) {
            for (int x = 0; x < D; ++x) {
                int in = 0;
                for (int sy = 0; sy < 4; ++sy) {
                    for (int sx = 0; sx < 4; ++sx) {
                        const float fx = x + (sx + 0.5f) / 4 - c, fy = y + (sy + 0.5f) / 4 - c;
                        if (fx * fx + fy * fy <= rr * rr) { ++in; }
                    }
                }
                px[y * D + x] = (static_cast<Uint32>(in * 255 / 16) << 24) | 0xFFFFFFu;
            }
        }
        g_disc = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, D, D);
        if (g_disc != nullptr) {
            SDL_UpdateTexture(g_disc, nullptr, px, D * 4);
            SDL_SetTextureBlendMode(g_disc, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(g_disc, SDL_ScaleModeLinear);
        }
    }

    /* whole pixels: fractional edges left seams where the pieces meet */
    inline void RRect(float xf, float yf, float wf, float hf, float rf, SDL_Color c) {
        const int x = static_cast<int>(std::lround(xf)), y = static_cast<int>(std::lround(yf));
        const int w = static_cast<int>(std::lround(wf)), h = static_cast<int>(std::lround(hf));
        if (w <= 0 || h <= 0) { return; }
        int r = static_cast<int>(std::lround(rf));
        if (r > w / 2) { r = w / 2; }
        if (r > h / 2) { r = h / 2; }
        DiscInit();
        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_ren, c.r, c.g, c.b, c.a);
        if (g_disc == nullptr || r < 1) { const SDL_Rect f = { x, y, w, h }; SDL_RenderFillRect(g_ren, &f); return; }
        const SDL_Rect rects[3] = { { x, y + r, w, h - 2 * r }, { x + r, y, w - 2 * r, r }, { x + r, y + h - r, w - 2 * r, r } };
        for (const SDL_Rect &f : rects) { if (f.w > 0 && f.h > 0) { SDL_RenderFillRect(g_ren, &f); } }
        SDL_SetTextureColorMod(g_disc, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(g_disc, c.a);
        constexpr int Hf = 48;
        const SDL_Rect q[4] = { { 0, 0, Hf, Hf }, { Hf, 0, Hf, Hf }, { 0, Hf, Hf, Hf }, { Hf, Hf, Hf, Hf } };
        const SDL_Rect d[4] = { { x, y, r, r }, { x + w - r, y, r, r }, { x, y + h - r, r, r }, { x + w - r, y + h - r, r, r } };
        for (int i = 0; i < 4; ++i) { SDL_RenderCopy(g_ren, g_disc, &q[i], &d[i]); }
    }

    /* a rounded outline: the ring, then the inside again */
    inline void RRectRing(float x, float y, float w, float h, float r, float t, SDL_Color ring, SDL_Color inside) {
        RRect(x, y, w, h, r, ring);
        RRect(x + t, y + t, w - 2 * t, h - 2 * t, r - t, inside);
    }

    /* the whole disc in one copy (four quarters crossed where they met) */
    inline void Disc(float cx, float cy, float r, SDL_Color c) {
        DiscInit();
        if (g_disc == nullptr) { return; }
        SDL_SetTextureColorMod(g_disc, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(g_disc, c.a);
        const SDL_FRect d = { cx - r, cy - r, 2 * r, 2 * r };
        SDL_RenderCopyF(g_ren, g_disc, nullptr, &d);
    }

    inline void Triangle(float x1, float y1, float x2, float y2, float x3, float y3, SDL_Color c) {
        const SDL_Vertex v[3] = { { { x1, y1 }, c, { 0, 0 } }, { { x2, y2 }, c, { 0, 0 } }, { { x3, y3 }, c, { 0, 0 } } };
        SDL_RenderGeometry(g_ren, nullptr, v, 3, nullptr, 0);
    }

    /* a chevron pointing left (dir -1) or right (+1), centred on cx, cy */
    inline void Chevron(float cx, float cy, float s, int dir, SDL_Color c) {
        const float t = s * 0.28f;                 /* stroke */
        const float hx = s * 0.30f, hy = s * 0.5f;
        const float tip = cx + dir * hx, back = cx - dir * hx;
        /* two quads as four triangles */
        Triangle(back, cy - hy, back + dir * t, cy - hy, tip, cy, c);
        Triangle(back + dir * t, cy - hy, tip + dir * t, cy, tip, cy, c);
        Triangle(back, cy + hy, back + dir * t, cy + hy, tip, cy, c);
        Triangle(back + dir * t, cy + hy, tip + dir * t, cy, tip, cy, c);
    }

    /* an on/off switch, its right edge at xr, centred on cy */
    inline void Toggle(float xr, float cy, bool on, SDL_Color on_color = Acc) {
        const float w = 58, h = 32, knob = 24, inset = 4, x = xr - w, y = cy - h / 2;
        RRect(x, y, w, h, h / 2, on ? on_color : Surf3);
        Disc(on ? x + w - inset - knob / 2 : x + inset + knob / 2, cy, knob / 2, on ? Ink : Dim);
    }

    /* a small rounded label: text on a tinted pill. Returns its width. */
    inline int Chip(const std::string &s, int x, int y, SDL_Color c, int align = 0) {
        const int w = TextW(s, Small) + 22, h = 28;
        const int x0 = align == 2 ? x - w : x;
        RRect(x0, y, w, h, h / 2, Alpha(c, 38));
        Text(s, x0 + 11, y + 4, Small, c);
        return w;
    }

    /* a controller button: the console's own glyph when its font is there,
     * else a disc with the letter (the PC preview) */
    inline int Button(const char *glyph, char letter, int x, int cy) {
        if (g_glyphs != nullptr) {
            if (const CachedText *t = Render(glyph, 0, Ink, 0, true)) {
                SDL_Rect r = { x, cy - t->h / 2, t->w, t->h };
                SDL_RenderCopy(g_ren, t->tex, nullptr, &r);
                return t->w;
            }
        }
        Disc(x + 13, cy, 13, Ink);
        const char s[2] = { letter, 0 };
        Text(s, x + 13, cy - 11, Small, Bg, 1);
        return 26;
    }

    /* the brand mark: two Joy-Con with a screen and a play triangle between
     * (docs/hbas-banner.png), h tall */
    inline void Logo(float x, float y, float h) {
        const float jw = std::round(h * 0.30f), gap = std::max(1.0f, std::round(h * 0.05f)), sw = std::round(h * 0.92f);
        const float sh = std::round(h * 0.86f), sy = y + std::round((h - sh) / 2);
        RRect(x, y, jw, h, jw / 2, JoyBlue);
        RRect(x + jw + gap, sy, sw, sh, std::round(h * 0.12f), Surf3);
        RRect(x + jw + gap + sw + gap, y, jw, h, jw / 2, JoyRed);
        Disc(x + jw / 2, y + h * 0.30f, h * 0.075f, Bg);
        Disc(x + jw + gap + sw + gap + jw / 2, y + h * 0.66f, h * 0.075f, Bg);
        const float cx = x + jw + gap + sw / 2 + h * 0.04f, cy = y + h / 2, s = h * 0.22f;
        Triangle(cx - s * 0.8f, cy - s, cx - s * 0.8f, cy + s, cx + s, cy, Ink);
    }

}
