/*
 * The PC preview's SDL2_ttf: the five calls the manager uses, on stb_truetype
 * (ref/libtesla/include). Sizes as SDL_ttf: ptsize pixels to the em; a wrapped
 * text is as wide as its widest line.
 */
#pragma once
#include <SDL2/SDL.h>
#include <cmath>
#include <string>
#include <vector>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

struct _TTF_Font {
    std::vector<unsigned char> data;
    stbtt_fontinfo info;
    float scale;
    int ascent, descent, linegap;
};
typedef _TTF_Font TTF_Font;

inline int TTF_Init() { return 0; }
inline void TTF_Quit() {}

inline TTF_Font *TTF_OpenFontRW(SDL_RWops *src, int freesrc, int ptsize) {
    auto *f = new TTF_Font();
    const Sint64 n = SDL_RWsize(src);
    f->data.resize(static_cast<size_t>(n));
    SDL_RWread(src, f->data.data(), 1, static_cast<size_t>(n));
    if (freesrc) { SDL_RWclose(src); }
    if (!stbtt_InitFont(&f->info, f->data.data(), stbtt_GetFontOffsetForIndex(f->data.data(), 0))) { delete f; return nullptr; }
    f->scale = stbtt_ScaleForMappingEmToPixels(&f->info, static_cast<float>(ptsize));
    stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->linegap);
    return f;
}

inline void TTF_CloseFont(TTF_Font *f) { delete f; }

namespace ttfpreview {

    inline std::vector<int> Decode(const char *s) {
        std::vector<int> out;
        const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
        while (*p) {
            int c = *p++;
            if (c >= 0xF0) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
            else if (c >= 0xE0) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
            else if (c >= 0xC0) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
            out.push_back(c);
        }
        return out;
    }

    inline float Width(TTF_Font *f, const std::vector<int> &cp, size_t a, size_t b) {
        float x = 0;
        for (size_t i = a; i < b; ++i) {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&f->info, cp[i], &adv, &lsb);
            x += adv * f->scale;
            if (i + 1 < b) { x += stbtt_GetCodepointKernAdvance(&f->info, cp[i], cp[i + 1]) * f->scale; }
        }
        return x;
    }

    inline int LineH(TTF_Font *f) { return static_cast<int>(std::ceil(f->ascent * f->scale) - std::floor(f->descent * f->scale)); }
    inline int LineSkip(TTF_Font *f) { return static_cast<int>(std::lround((f->ascent - f->descent + f->linegap) * f->scale)); }

    /* draw one line's coverage into alpha (w x h), baseline at by */
    inline void Draw(TTF_Font *f, const std::vector<int> &cp, size_t a, size_t b, std::vector<unsigned char> &alpha, int w, int h, int by) {
        float x = 0;
        for (size_t i = a; i < b; ++i) {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&f->info, cp[i], &adv, &lsb);
            int x0, y0, x1, y1;
            const float sx = x - std::floor(x);
            stbtt_GetCodepointBitmapBoxSubpixel(&f->info, cp[i], f->scale, f->scale, sx, 0, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                std::vector<unsigned char> g(static_cast<size_t>(gw * gh));
                stbtt_MakeCodepointBitmapSubpixel(&f->info, g.data(), gw, gh, gw, f->scale, f->scale, sx, 0, cp[i]);
                const int ox = static_cast<int>(std::floor(x)) + x0, oy = by + y0;
                for (int yy = 0; yy < gh; ++yy) {
                    for (int xx = 0; xx < gw; ++xx) {
                        const int px = ox + xx, py = oy + yy;
                        if (px < 0 || py < 0 || px >= w || py >= h) { continue; }
                        unsigned char &d = alpha[static_cast<size_t>(py * w + px)];
                        const int v = d + g[static_cast<size_t>(yy * gw + xx)];
                        d = static_cast<unsigned char>(v > 255 ? 255 : v);
                    }
                }
            }
            x += adv * f->scale;
            if (i + 1 < b) { x += stbtt_GetCodepointKernAdvance(&f->info, cp[i], cp[i + 1]) * f->scale; }
        }
    }

    inline SDL_Surface *Surface(const std::vector<unsigned char> &alpha, int w, int h, SDL_Color c) {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (s == nullptr) { return nullptr; }
        for (int y = 0; y < h; ++y) {
            Uint32 *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(s->pixels) + y * s->pitch);
            for (int x = 0; x < w; ++x) {
                row[x] = (static_cast<Uint32>(alpha[static_cast<size_t>(y * w + x)]) << 24) | (c.r << 16) | (c.g << 8) | c.b;
            }
        }
        SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
        return s;
    }

}

/* Measure UTF-8 text using f; w/h receive pixel extents. Returns zero.
 * This mirrors SDL_ttf for the manager's Chinese line-break measurement. */
inline int TTF_SizeUTF8(TTF_Font *f, const char *text, int *w, int *h) {
    const std::vector<int> codepoints = ttfpreview::Decode(text); /* Scalars measured with the preview font. */
    *w = static_cast<int>(std::ceil(ttfpreview::Width(f, codepoints, 0, codepoints.size())));
    *h = ttfpreview::LineH(f);
    return 0;
}

inline SDL_Surface *TTF_RenderUTF8_Blended(TTF_Font *f, const char *text, SDL_Color c) {
    const std::vector<int> cp = ttfpreview::Decode(text);
    const int w = std::max(1, static_cast<int>(std::ceil(ttfpreview::Width(f, cp, 0, cp.size()))) + 1), h = ttfpreview::LineH(f);
    std::vector<unsigned char> alpha(static_cast<size_t>(w * h));
    ttfpreview::Draw(f, cp, 0, cp.size(), alpha, w, h, static_cast<int>(std::ceil(f->ascent * f->scale)));
    return ttfpreview::Surface(alpha, w, h, c);
}

inline SDL_Surface *TTF_RenderUTF8_Blended_Wrapped(TTF_Font *f, const char *text, SDL_Color c, Uint32 wrap) {
    const std::vector<int> cp = ttfpreview::Decode(text);
    /* greedy word wrap; '\n' ends a line */
    std::vector<std::pair<size_t, size_t>> lines;
    size_t start = 0;
    while (start <= cp.size()) {
        size_t end = start, last_space = std::string::npos;
        while (end < cp.size() && cp[end] != '\n') {
            if (cp[end] == ' ') { last_space = end; }
            if (ttfpreview::Width(f, cp, start, end + 1) > static_cast<float>(wrap) && end > start) {
                if (last_space != std::string::npos && last_space > start) { end = last_space; }
                break;
            }
            ++end;
        }
        lines.push_back({ start, end });
        if (end >= cp.size()) { break; }
        start = end;
        if (cp[start] == ' ' || cp[start] == '\n') { ++start; }
    }
    float wmax = 1;
    for (auto &l : lines) { wmax = std::max(wmax, ttfpreview::Width(f, cp, l.first, l.second)); }
    const int skip = ttfpreview::LineSkip(f);
    const int w = static_cast<int>(std::ceil(wmax)) + 1, h = skip * static_cast<int>(lines.size() - 1) + ttfpreview::LineH(f);
    std::vector<unsigned char> alpha(static_cast<size_t>(w * h));
    for (size_t i = 0; i < lines.size(); ++i) {
        ttfpreview::Draw(f, cp, lines[i].first, lines[i].second, alpha, w, h, static_cast<int>(i) * skip + static_cast<int>(std::ceil(f->ascent * f->scale)));
    }
    return ttfpreview::Surface(alpha, w, h, c);
}
