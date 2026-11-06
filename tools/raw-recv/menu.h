/* Text rendered from HarmonyOS Sans SC; generated atlas needs SDL2 only. */
#include "font_harmony.h"
#include "../../common/utf8.h"

enum { MENU_WAITING = 0, MENU_NODRIVER, MENU_WRONG_DEVICE, MENU_CONNECTED, MENU_PAUSED };

static int g_menu = 0;                 /* The menu is on screen instead of a frame. */
static int g_menu_state = MENU_WAITING; /* Current connection state. */
static char g_menu_err[64] = "";        /* libusb error for missing/inaccessible driver. */
static char g_menu_other[64] = "";      /* Conflicting USB program name. */
static SDL_Texture *g_font_tex = NULL; /* Shared regular/bold glyph texture. */
static SDL_Renderer *g_font_ren = NULL; /* Renderer owning the glyph texture. */

/* Load the generated atlas into ren. Returns zero on success, -1 on failure;
 * replaces any font texture previously owned by this renderer. */
static int text_init(SDL_Renderer *ren)
{
    if (g_font_tex && g_font_ren == ren) return 0;
    if (g_font_tex) SDL_DestroyTexture(g_font_tex);
    g_font_tex = NULL;
    g_font_ren = ren;
    Uint32 *pixels = malloc((size_t)FONT_ATLAS_W * FONT_ATLAS_H * 4); /* Expanded white ARGB glyph coverage. */
    if (!pixels) return -1;
    size_t offset = 0; /* Next output pixel in the decompressed atlas. */
    for (size_t index = 0; index < sizeof(font_atlas); index += 2) { /* Count/value pair offset in the generated RLE stream. */
        const unsigned value = font_atlas[index + 1]; /* Two packed four-bit alpha samples. */
        for (unsigned run = 0; run < font_atlas[index]; ++run) { /* Packed-byte position within this repeated run. */
            pixels[offset++] = (((value >> 4) * 17) << 24) | 0xFFFFFFu;
            pixels[offset++] = (((value & 15) * 17) << 24) | 0xFFFFFFu;
        }
    }
    g_font_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, FONT_ATLAS_W, FONT_ATLAS_H);
    if (g_font_tex) {
        SDL_UpdateTexture(g_font_tex, NULL, pixels, FONT_ATLAS_W * 4);
        SDL_SetTextureBlendMode(g_font_tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g_font_tex, SDL_ScaleModeLinear);
    }
    free(pixels);
    return g_font_tex ? 0 : -1;
}

/* Find code in the sorted atlas. Unknown scalars use a visible question mark;
 * returns the shared glyph-table index without modifying renderer state. */
static int textGlyph(uint32_t code)
{
    int low = 0, high = FONT_COUNT; /* Half-open glyph index range to search. */
    while (low < high) {
        const int middle = low + (high - low) / 2; /* Glyph index at the search midpoint. */
        if (font_codes[middle] < code) low = middle + 1;
        else high = middle;
    }
    return low < FONT_COUNT && font_codes[low] == code ? low : '?' - 32;
}

/* Measure NUL-terminated UTF-8 s at px pixels, using weight bold (0 or 1).
 * Returns horizontal advance in pixels; malformed input uses a fallback glyph. */
static int text_width(const char *s, int px, int bold)
{
    int width = 0; /* Sum of advances in the atlas font's native pixels. */
    while (*s) width += font_glyphs[bold][textGlyph(sftUtf8Next(&s))][4];
    return width * px / font_px[bold];
}

/* Draw UTF-8 s on ren at x,y with em size px and weight bold (0 or 1).
 * col is the ink colour; align is 0 left, 1 centre or 2 right. No return value;
 * does nothing if the atlas could not be loaded. */
static void text_draw(SDL_Renderer *ren, const char *s, int x, int y, int px, int bold, SDL_Color col, int align)
{
    if (!g_font_tex) return;
    if (align) x -= text_width(s, px, bold) / (align == 1 ? 2 : 1);
    SDL_SetTextureColorMod(g_font_tex, col.r, col.g, col.b);
    SDL_SetTextureAlphaMod(g_font_tex, col.a);
    const int fontSize = font_px[bold]; /* Native em size for this atlas weight. */
    int pen = x * fontSize; /* Horizontal pen position, retaining fractional scaled advances. */
    while (*s) {
        const uint32_t code = sftUtf8Next(&s); /* Next Unicode scalar from the UI string. */
        const unsigned short *glyph = font_glyphs[bold][textGlyph(code)]; /* Atlas rectangle and advance for this scalar. */
        if (code != ' ') {
            SDL_Rect source = { glyph[0], glyph[1], glyph[2], glyph[3] }; /* Glyph coverage rectangle in the atlas. */
            SDL_Rect destination = { pen / fontSize, y, glyph[2] * px / fontSize + 1, glyph[3] * px / fontSize }; /* Scaled rectangle on screen. */
            SDL_RenderCopy(ren, g_font_tex, &source, &destination);
        }
        pen += glyph[4] * px;
    }
}

static void fill(SDL_Renderer *ren, int x, int y, int w, int h, SDL_Color c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(ren, &r);
}

/* v0.7.12: rounded panels - a smooth (anti-aliased) disc made once per
 * renderer, its quarters for the corners, plain rectangles for the rest.
 * Coordinates in the current units (float: the overlay draws scaled). */
static SDL_Texture *g_ui_disc = NULL;
static SDL_Renderer *g_ui_disc_ren = NULL;

static void ui_disc_init(SDL_Renderer *ren)
{
    if (g_ui_disc && g_ui_disc_ren == ren) return;
    if (g_ui_disc && g_ui_disc_ren != ren) g_ui_disc = NULL;   /* went with its renderer */
    enum { D = 96 };
    static Uint32 px[D * D];
    for (int y = 0; y < D; y++)
        for (int x = 0; x < D; x++) {
            /* 4x4 samples a pixel: the coverage of the disc */
            int in = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    const float fx = x + (sx + 0.5f) / 4 - D / 2.0f, fy = y + (sy + 0.5f) / 4 - D / 2.0f;
                    in += fx * fx + fy * fy <= (D / 2.0f) * (D / 2.0f);
                }
            px[y * D + x] = ((Uint32)(in * 255 / 16) << 24) | 0xFFFFFFu;
        }
    g_ui_disc = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, D, D);
    g_ui_disc_ren = ren;
    if (g_ui_disc) {
        SDL_UpdateTexture(g_ui_disc, NULL, px, D * 4);
        SDL_SetTextureBlendMode(g_ui_disc, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g_ui_disc, SDL_ScaleModeLinear);
    }
}

/* v0.7.12: the settings icon - a gear (8 teeth, a hole), smooth like the disc */
static SDL_Texture *g_ui_gear = NULL;
static SDL_Renderer *g_ui_gear_ren = NULL;
/* Always-on-top pin (map-pin silhouette, same AA style as the gear). */
static SDL_Texture *g_ui_pin = NULL;
static SDL_Renderer *g_ui_pin_ren = NULL;

static void ui_gear_init(SDL_Renderer *ren)
{
    if (g_ui_gear && g_ui_gear_ren == ren) return;
    enum { D = 96 };
    static Uint32 px[D * D];
    const float c = D / 2.0f, rb = D * 0.34f, rt = D * 0.47f, rh = D * 0.15f;
    for (int y = 0; y < D; y++)
        for (int x = 0; x < D; x++) {
            int in = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    const float fx = x + (sx + 0.5f) / 4 - c, fy = y + (sy + 0.5f) / 4 - c;
                    const float r = sqrtf(fx * fx + fy * fy);
                    if (r < rh) continue;                      /* the hole */
                    if (r <= rb) { in++; continue; }           /* the body */
                    if (r > rt) continue;
                    /* a tooth: 8 of them, a little narrower at the tip */
                    float a = atan2f(fy, fx) / (2 * 3.14159265f) * 8.0f;
                    a -= floorf(a);                            /* 0..1 within one tooth period */
                    const float half = 0.25f - 0.07f * (r - rb) / (rt - rb);
                    if (fabsf(a - 0.5f) < half) in++;
                }
            px[y * D + x] = ((Uint32)(in * 255 / 16) << 24) | 0xFFFFFFu;
        }
    g_ui_gear = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, D, D);
    g_ui_gear_ren = ren;
    if (g_ui_gear) {
        SDL_UpdateTexture(g_ui_gear, NULL, px, D * 4);
        SDL_SetTextureBlendMode(g_ui_gear, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g_ui_gear, SDL_ScaleModeLinear);
    }
}

static void ui_pin_init(SDL_Renderer *ren)
{
    if (g_ui_pin && g_ui_pin_ren == ren) return;
    enum { D = 96 };
    static Uint32 px[D * D];
    /* Upright map pin: round head with a hole, tapered point below.
     * Coords are relative to the head centre (slightly above texture mid). */
    const float c = D / 2.0f;
    const float hy = -D * 0.08f;                /* head centre above mid */
    const float hr = D * 0.27f, hole = D * 0.105f;
    const float tip = D * 0.52f;                /* tip distance below head */
    for (int y = 0; y < D; y++)
        for (int x = 0; x < D; x++) {
            int in = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    const float fx = x + (sx + 0.5f) / 4 - c;
                    const float fy = y + (sy + 0.5f) / 4 - c - hy;
                    const float r = sqrtf(fx * fx + fy * fy);
                    int hit = 0;
                    if (r <= hr) hit = 1;                      /* head disc */
                    else if (fy > 0.f && fy <= tip) {
                        /* Tapered point: width shrinks from head to tip. */
                        const float t = fy / tip;
                        const float half = hr * (1.f - t);
                        if (fabsf(fx) <= half) hit = 1;
                    }
                    if (hit && !(r < hole && fy < hr * 0.35f)) in++;
                }
            px[y * D + x] = ((Uint32)(in * 255 / 16) << 24) | 0xFFFFFFu;
        }
    g_ui_pin = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, D, D);
    g_ui_pin_ren = ren;
    if (g_ui_pin) {
        SDL_UpdateTexture(g_ui_pin, NULL, px, D * 4);
        SDL_SetTextureBlendMode(g_ui_pin, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g_ui_pin, SDL_ScaleModeLinear);
    }
}

/* v0.7.20: the Controller menu's icon - a gamepad: body, two grips, a d-pad
 * and two buttons cut out */
static SDL_Texture *g_ui_pad = NULL;
static SDL_Renderer *g_ui_pad_ren = NULL;

static int ui_pad_rr(float x, float y, float x0, float y0, float x1, float y1, float r)
{
    const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    float dx = fabsf(x - cx) - ((x1 - x0) / 2 - r), dy = fabsf(y - cy) - ((y1 - y0) / 2 - r);
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
    return dx * dx + dy * dy <= r * r;
}

static int ui_pad_disc(float x, float y, float cx, float cy, float r)
{
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

static void ui_pad_init(SDL_Renderer *ren)
{
    if (g_ui_pad && g_ui_pad_ren == ren) return;
    enum { D = 96 };
    static Uint32 px[D * D];
    for (int y = 0; y < D; y++)
        for (int x = 0; x < D; x++) {
            int in = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    const float fx = x + (sx + 0.5f) / 4, fy = y + (sy + 0.5f) / 4;
                    const int body = ui_pad_rr(fx, fy, 9, 22, 87, 58, 17) || ui_pad_disc(fx, fy, 26, 60, 15) ||
                                     ui_pad_disc(fx, fy, 70, 60, 15);
                    if (!body) continue;
                    if (ui_pad_rr(fx, fy, 31 - 3.3f, 40 - 9, 31 + 3.3f, 40 + 9, 1.2f) ||          /* the d-pad */
                        ui_pad_rr(fx, fy, 31 - 9, 40 - 3.3f, 31 + 9, 40 + 3.3f, 1.2f) ||
                        ui_pad_disc(fx, fy, 62, 45, 4.4f) || ui_pad_disc(fx, fy, 72, 36, 4.4f))      /* the buttons */
                        continue;
                    in++;
                }
            px[y * D + x] = ((Uint32)(in * 255 / 16) << 24) | 0xFFFFFFu;
        }
    g_ui_pad = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, D, D);
    g_ui_pad_ren = ren;
    if (g_ui_pad) {
        SDL_UpdateTexture(g_ui_pad, NULL, px, D * 4);
        SDL_SetTextureBlendMode(g_ui_pad, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g_ui_pad, SDL_ScaleModeLinear);
    }
}

static void ui_rrect(SDL_Renderer *ren, float x, float y, float w, float h, float r, SDL_Color c)
{
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    ui_disc_init(ren);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    if (!g_ui_disc || r < 1) { SDL_FRect f = { x, y, w, h }; SDL_RenderFillRectF(ren, &f); return; }
    const SDL_FRect mid = { x, y + r, w, h - 2 * r }, top = { x + r, y, w - 2 * r, r }, bot = { x + r, y + h - r, w - 2 * r, r };
    SDL_RenderFillRectF(ren, &mid);
    SDL_RenderFillRectF(ren, &top);
    SDL_RenderFillRectF(ren, &bot);
    SDL_SetTextureColorMod(g_ui_disc, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(g_ui_disc, c.a);
    const int H = 48;   /* half the disc texture */
    const SDL_Rect q[4] = { { 0, 0, H, H }, { H, 0, H, H }, { 0, H, H, H }, { H, H, H, H } };
    const SDL_FRect d[4] = { { x, y, r, r }, { x + w - r, y, r, r }, { x, y + h - r, r, r }, { x + w - r, y + h - r, r, r } };
    for (int i = 0; i < 4; i++) SDL_RenderCopyF(ren, g_ui_disc, &q[i], &d[i]);
}


static const char *rec_dir_for_menu(void);
static const char *net_peer_for_menu(void);
static int status_for_menu(char *head, size_t hn, char *detail, size_t dn, uint32_t *reason);

static void menu_draw(SDL_Renderer *ren);   /* v0.7.12: ui.h - the home screen */
