/* padui.h - v0.7.20: the Controller page of the drawer (top-left button, G,
 * or Settings > Controller menu). Three devices that play the Switch, a strip
 * of what is pressed right now, and a tab each for the keyboard, the mouse
 * and the controller: what each key or button does, and the options that go
 * with it. The data and its rules are padcfg.h, the devices pad.h. */
#pragma once

/* a bind cell's (id, arg): device 1 keyboard / 2 controller, the Switch button or stick direction, the cell (keyboard has two) */
#define PUI_ARG(dev, slot, cell) ((dev) * 1000 + (slot) * 2 + (cell))

static void pui_capture(int arg)
{
    const int dev = arg / 1000;
    if (dev == 2 && !g_pad) { hud_toast("请先连接手柄"); return; }
    g_pcap_dev = dev;
    g_pcap_slot = (arg % 1000) / 2;
    g_pcap_cell = dev == 1 ? (arg & 1) : 0;
    g_pad_redraw = 1;
}

static void pui_clear(int arg)
{
    pad_bind_clear(arg / 1000, (arg % 1000) / 2, arg & 1);
}

static void pui_mouse_cycle(int m, int dir)
{
    int v = g_bind_mouse[m] + dir;
    if (v < -1) v = PS_NBTN - 1;
    if (v >= PS_NBTN) v = -1;
    g_bind_mouse[m] = v;
    prefs_save();
}

static void pui_swap(void)
{
    pcfg_swap_face();
    prefs_save();
    hud_toast("已交换 A / B 和 X / Y");
}

static const char *ui_pad_summary(char *b, size_t n)
{
    snprintf(b, n, "手柄 %s，键盘 %s，鼠标 %s", g_pad_send ? "开启" : "关闭", g_pad_kbd ? "开启" : "关闭", g_pad_mouse ? "开启" : "关闭");
    return b;
}

/* one clickable cell: a key, a controller button, or "Press..." while it waits for one */
static void pui_cell(SDL_Renderer *ren, float x, float y, float w, float h, const char *txt, int unset, int cap, int focus, int arg)
{
    const int hov = ui_in(x, y, w, h);
    const int px = ui_px(14);
    if (focus) ui_rrect(ren, x - 2, y - 2, w + 4, h + 4, ui_d(9), UI_INK);
    SDL_Color f = cap ? UI_ACC : UI_SURF3;
    if (hov && !cap) { f.r = (Uint8)(f.r + (255 - f.r) / 8); f.g = (Uint8)(f.g + (255 - f.g) / 8); f.b = (Uint8)(f.b + (255 - f.b) / 8); }
    ui_rrect(ren, x, y, w, h, ui_d(8), f);
    ui_text_fit(ren, cap ? (g_pcap_dev == 1 ? "按下键盘按键" : "按下手柄按键") : txt, (int)(x + w / 2), (int)(y + (h - px * 1.18f) / 2), px, 1,
                cap ? (SDL_Color){ 8, 20, 30, 255 } : unset ? UI_FAINT : UI_INK, 1, (int)(w - ui_d(8)));
    ui_hit(x, y, w, h, H_PADBIND, arg);
}

/* the Switch buttons (or stick directions) of one group, each with the key(s) or the controller button that drives it */
static void pui_binds(SDL_Renderer *ren, float cx, float xr, float *y, const char *title, const int *slots, int n, int dev)
{
    const float h = ui_d(36), step = h + ui_d(2), pad = ui_d(6);
    float cw = dev == 1 ? ui_d(112) : ui_d(170);
    if (cw > (xr - cx) * 0.34f) cw = floorf((xr - cx) * 0.34f);   /* whole pixels: a fraction leaves a seam in the rounded cell */
    cw = floorf(cw);
    const float x2 = floorf(xr - cw), x1 = dev == 1 ? x2 - floorf(pad) - cw : x2;
    ui_section(ren, cx, y, title);
    for (int i = 0; i < n; i++) {
        const int s = slots[i];
        const int px = ui_px(16);
        ui_text_fit(ren, ps_name[s], (int)cx, (int)(*y + (h - px * 1.18f) / 2), px, 0, UI_INK, 0, (int)(x1 - cx - ui_d(8)));
        for (int c = 0; c < (dev == 1 ? 2 : 1); c++) {
            const int arg = PUI_ARG(dev, s, c);
            const int v = dev == 1 ? g_bind_kb[c][s] : g_bind_ctl[s];
            const int unset = dev == 1 ? v <= 0 : v < 0;
            ui_row(H_PADBIND, arg, *y, h);
            pui_cell(ren, c ? x2 : x1, *y + 1, cw, h - 2, dev == 1 ? pcfg_key_name(v) : pcfg_ct_name(v), unset,
                     g_pcap_dev == dev && g_pcap_slot == s && (dev == 2 || g_pcap_cell == c), ui_focused(H_PADBIND, arg), arg);
        }
        *y += step;
    }
}

/* a mouse button -> the Switch button it presses, with arrows to pick another */
static void pui_mouse_binds(SDL_Renderer *ren, float cx, float xr, float *y)
{
    const float h = ui_d(36), step = h + ui_d(2);
    float vw = ui_d(150), aw = ui_d(30);
    if (vw + 2 * aw > (xr - cx) * 0.6f) vw = floorf((xr - cx) * 0.6f - 2 * aw);
    vw = floorf(vw); aw = floorf(aw);
    const float x = floorf(xr - vw - 2 * aw);
    ui_section(ren, cx, y, "按键");
    for (int m = 0; m < PMB_N; m++) {
        const int px = ui_px(16);
        ui_row(H_PADMBTN, m * 2 + 1, *y, h);
        ui_text_fit(ren, mb_name[m], (int)cx, (int)(*y + (h - px * 1.18f) / 2), px, 0, UI_INK, 0, (int)(x - cx - ui_d(8)));
        const int focus = ui_focused(H_PADMBTN, m * 2 + 1), v = g_bind_mouse[m];
        if (focus) ui_rrect(ren, x - 2, *y - 1, vw + 2 * aw + 4, h + 2, ui_d(9), UI_INK);
        const SDL_Color bg = UI_SURF3;
        ui_rrect(ren, x, *y + 1, aw, h - 2, ui_d(8), bg);
        ui_rrect(ren, x + aw + vw, *y + 1, aw, h - 2, ui_d(8), bg);
        ui_rrect(ren, x + aw, *y + 1, vw, h - 2, 0, ui_in(x + aw, *y, vw, h) ? (SDL_Color){ 58, 64, 76, 255 } : bg);
        const int tpx = ui_px(14), ty = (int)(*y + (h - tpx * 1.18f) / 2);
        text_draw(ren, "<", (int)(x + aw / 2), ty, tpx, 1, UI_DIM, 1);
        text_draw(ren, ">", (int)(x + aw + vw + aw / 2), ty, tpx, 1, UI_DIM, 1);
        ui_text_fit(ren, v >= 0 ? ps_name[v] : "无", (int)(x + aw + vw / 2), ty, tpx, 1, v >= 0 ? UI_INK : UI_FAINT, 1, (int)(vw - ui_d(8)));
        ui_hit(x, *y, aw, h, H_PADMBTN, m * 2);
        ui_hit(x + aw, *y, vw, h, H_PADMBTN, m * 2 + 1);
        ui_hit(x + aw + vw, *y, aw, h, H_PADMBTN, m * 2 + 1);
        *y += step;
    }
}

static void pui_slider(SDL_Renderer *ren, float cx, float xr, float *y, int i)
{
    const float h = ui_d(52), w = xr - cx;
    const int v = *pad_sliders[i].v, lo = pad_sliders[i].lo, hi = pad_sliders[i].hi, def = pad_sliders[i].def;
    ui_row(H_PADSL, i, *y, h);
    if (ui_focused(H_PADSL, i)) ui_rrect(ren, cx - ui_d(12), *y - 2, w + ui_d(24), h + 4, ui_d(10), UI_SURF2);
    text_draw(ren, pad_sliders[i].name, (int)cx, (int)(*y + ui_d(4)), ui_px(16), 0, UI_INK, 0);
    char t[24];
    snprintf(t, sizeof(t), "%d%s", v, pad_sliders[i].unit);
    text_draw(ren, t, (int)xr, (int)(*y + ui_d(4)), ui_px(15), 1, v == def ? UI_DIM : UI_ACC, 2);
    const float ty = *y + ui_d(34);
    ui_rrect(ren, cx, ty - 2, w, 4, 2, UI_SURF3);
    const float fx = cx + (float)(def - lo) / (hi - lo) * w, vx = cx + (float)(v - lo) / (hi - lo) * w;
    ui_rrect(ren, vx < fx ? vx : fx, ty - 2, vx < fx ? fx - vx : vx - fx, 4, 2, UI_ACC);
    ui_rrect(ren, fx - 1, ty - ui_d(6), 2, ui_d(12), 1, UI_FAINT);
    const int hov = ui_in(cx - ui_d(8), ty - ui_d(14), w + ui_d(16), ui_d(28)) || (g_ui_drag == H_PADSL && g_ui_drag_arg == i);
    const float ks = hov ? ui_d(18) : ui_d(16);
    ui_rrect(ren, vx - ks * 0.5f, ty - ks * 0.5f, ks, ks, ks * 0.5f, UI_INK);
    ui_hit(cx - ui_d(8), ty - ui_d(14), w + ui_d(16), ui_d(28), H_PADSL, i);
    *y += h + ui_d(2);
}

static void pui_note(SDL_Renderer *ren, float cx, float w, float *y, const char *s)
{
    *y += ui_wrap_a(ren, s, (int)cx, (int)*y, ui_px(14), 0, UI_DIM, (int)w, (int)ui_d(20), 0) + ui_d(8);
}

static const int pui_g_btn[] = { PS_A, PS_B, PS_X, PS_Y, PS_L, PS_R, PS_ZL, PS_ZR, PS_PLUS, PS_MINUS, PS_HOME, PS_CAPTURE, PS_L3, PS_R3 };
static const int pui_g_dpad[] = { PS_UP, PS_DOWN, PS_LEFT, PS_RIGHT };
static const int pui_g_ls[] = { PS_LS_UP, PS_LS_LEFT, PS_LS_DOWN, PS_LS_RIGHT };
static const int pui_g_rs[] = { PS_RS_UP, PS_RS_LEFT, PS_RS_DOWN, PS_RS_RIGHT };

/* the page's content, in the drawer's columns (two when `two`); y_l / y_r come in at the top of the scroll area and leave at its end */
static void ui_pad_page(SDL_Renderer *ren, float cx_l, float xr_l, float cx_r, float xr_r, float col_w, int two, float *py_l, float *py_r)
{
    const float h = ui_d(48);
    float yl = *py_l, yr = *py_r;
    char sub[220];

    ui_section(ren, cx_l, &yl, "操作 SWITCH");
    ui_row(H_PAD, 0, yl, h);
    ui_label(ren, cx_l, yl, h, "控制器", pad_status(sub, sizeof(sub)), ui_focused(H_PAD, 0), col_w);
    ui_toggle(ren, xr_l, yl + ui_d(12), g_pad_send, H_PAD, 1);
    yl += h + ui_d(2);
    if (pad_count() > 1) {
        char s2[96];
        snprintf(s2, sizeof(s2), "已连接 %d 个", pad_count());
        ui_row(H_PADCHANGE, 0, yl, h);
        ui_label(ren, cx_l, yl, h, "切换手柄", s2, ui_focused(H_PADCHANGE, 0), col_w);
        ui_button(ren, xr_l, 0, yl + ui_d(8), "更改", UI_SURF3, UI_INK, H_PADCHANGE, 0, 1);
        yl += h + ui_d(2);
    }
    ui_row(H_PADKB, 0, yl, h);
    ui_label(ren, cx_l, yl, h, "键盘", pad_kbd_status(sub, sizeof(sub)), ui_focused(H_PADKB, 0), col_w);
    ui_toggle(ren, xr_l, yl + ui_d(12), g_pad_kbd, H_PADKB, 1);
    yl += h + ui_d(2);
    ui_row(H_PADMOUSE, 0, yl, h);
    ui_label(ren, cx_l, yl, h, "鼠标", pad_mouse_status(sub, sizeof(sub)), ui_focused(H_PADMOUSE, 0), col_w);
    ui_toggle(ren, xr_l, yl + ui_d(12), g_pad_mouse, H_PADMOUSE, 1);
    yl += h + ui_d(2);

    /* what is pressed right now (the same devices, the keyboard too even though the menu has the keys) */
    {
        float *py = two ? &yr : &yl;
        const float cxp = two ? cx_r : cx_l, wp = col_w;
        const float bh = two ? ui_d(150) : ui_d(92);
        char pr[240];
        ui_section(ren, cxp, py, "当前输入");
        ui_rrect(ren, cxp, *py - ui_d(4), wp, bh, ui_d(12), UI_SURF2);
        pad_pressed_text(pr, sizeof(pr));
        if (pr[0]) ui_wrap_a(ren, pr, (int)(cxp + ui_d(14)), (int)(*py + ui_d(8)), ui_px(16), 1, UI_ACC, (int)(wp - ui_d(28)), (int)ui_d(24), 0);
        else text_draw(ren, "未检测到按键", (int)(cxp + ui_d(14)), (int)(*py + ui_d(8)), ui_px(16), 0, UI_FAINT, 0);
        ui_text_fit(ren, !g_pad && !g_pad_kbd ? "连接手柄或开启键盘控制后，可在此测试" : "按下任意按键进行测试",
                    (int)(cxp + ui_d(14)), (int)(*py - ui_d(4) + bh - ui_d(28)), ui_px(13), 0, UI_DIM, 0, (int)(wp - ui_d(28)));
        *py += bh + ui_d(4);
    }

    float y = two && yr > yl ? yr : yl;
    const float xr_all = two ? xr_r : xr_l, all_w = xr_all - cx_l, mid = (cx_l + xr_all) * 0.5f;
    y += ui_d(14);

    /* the tabs */
    {
        static const char *const tabs[] = { "键盘", "鼠标", "控制器" };
        ui_row(H_PADTAB, 0, y, ui_d(36));
        if (ui_focused(H_PADTAB, 0)) ui_rrect(ren, cx_l - ui_d(12), y - 3, all_w + ui_d(24), ui_d(38), ui_d(10), UI_SURF2);
        ui_seg(ren, -1, mid, y, tabs, 3, g_pad_tab, H_PADTAB, 15, all_w);
        y += ui_d(32) + ui_d(14);
    }
    const char *const note[3] = {
        "点击绑定项，再按下要使用的键。每个按键可绑定两个键。Esc 取消，Delete 清除。Esc、F3、F10 和 F11 保留为客户端快捷键。",
        "默认关闭。点击画面后鼠标开始控制 Switch，F10 释放。移动鼠标模拟摇杆，鼠标按键映射为 Switch 按键。",
        "点击绑定项，再按下手柄上要使用的按键。摇杆保持原有对应关系：左对左，右对右。" };
    pui_note(ren, cx_l, all_w, &y, note[g_pad_tab]);

    float ya = y, yb = y;
    float *pa = &ya, *pb = two ? &yb : &ya;
    const float cxb = two ? cx_r : cx_l, xrb = two ? xr_r : xr_l;
    if (g_pad_tab == 0) {
        pui_binds(ren, cx_l, xr_l, pa, "按键", pui_g_btn, 14, 1);
        pui_binds(ren, cxb, xrb, pb, "方向键", pui_g_dpad, 4, 1);
        pui_binds(ren, cxb, xrb, pb, "左摇杆", pui_g_ls, 4, 1);
        pui_binds(ren, cxb, xrb, pb, "右摇杆", pui_g_rs, 4, 1);
    } else if (g_pad_tab == 1) {
        static const char *const sticks[] = { "右摇杆", "左摇杆" };
        const float bh = ui_d(44), gy = ui_d(6);
        pui_mouse_binds(ren, cx_l, xr_l, pa);
        ui_section(ren, cxb, pb, "移动");
        ui_row(H_PADMODE, 0, *pb, bh);
        ui_label(ren, cxb, *pb, bh, "映射至", NULL, ui_focused(H_PADMODE, 0), col_w * 0.38f);
        ui_seg(ren, xrb, 0, *pb + gy, sticks, 2, g_pad_mstick, H_PADMODE, 14, col_w * 0.62f);
        *pb += bh + gy;
        pui_slider(ren, cxb, xrb, pb, 2);
        ui_row(H_PADINV, 0, *pb, ui_d(48));
        ui_label(ren, cxb, *pb, ui_d(48), "反转上下方向", NULL, ui_focused(H_PADINV, 0), col_w);
        ui_toggle(ren, xrb, *pb + ui_d(12), g_pad_minvert, H_PADINV, 1);
        *pb += ui_d(52);
    } else {
        pui_binds(ren, cx_l, xr_l, pa, "按键", pui_g_btn, 14, 2);
        pui_binds(ren, cxb, xrb, pb, "方向键", pui_g_dpad, 4, 2);
        ui_section(ren, cxb, pb, "摇杆与扳机");
        pui_slider(ren, cxb, xrb, pb, 0);
        pui_slider(ren, cxb, xrb, pb, 1);
        {
            const int sw = g_bind_ctl[PS_A] == SDL_CONTROLLER_BUTTON_A && g_bind_ctl[PS_B] == SDL_CONTROLLER_BUTTON_B &&
                           g_bind_ctl[PS_X] == SDL_CONTROLLER_BUTTON_X && g_bind_ctl[PS_Y] == SDL_CONTROLLER_BUTTON_Y;
            const int def = g_bind_ctl[PS_A] == SDL_CONTROLLER_BUTTON_B && g_bind_ctl[PS_B] == SDL_CONTROLLER_BUTTON_A &&
                            g_bind_ctl[PS_X] == SDL_CONTROLLER_BUTTON_Y && g_bind_ctl[PS_Y] == SDL_CONTROLLER_BUTTON_X;
            ui_row(H_PADSWAP, 0, *pb, h);
            ui_label(ren, cxb, *pb, h, "交换 A / B 和 X / Y",
                     sw ? "按名称对应：Xbox A 对应 A" : def ? "按位置对应：下方按键对应 B" : "当前：自定义布局",
                     ui_focused(H_PADSWAP, 0), col_w);
            ui_button(ren, xrb, 0, *pb + ui_d(8), "交换", UI_SURF3, UI_INK, H_PADSWAP, 0, 1);
            *pb += h + ui_d(2);
        }
    }

    y = (ya > yb ? ya : yb) + ui_d(12);
    {
        const char *lab = "恢复默认设置";
        const float bw = text_width(lab, ui_px(15), 1) + ui_d(32);
        ui_row(H_PADRESET, g_pad_tab, y, ui_d(32));
        if (ui_focused(H_PADRESET, g_pad_tab)) ui_rrect(ren, mid - bw / 2 - ui_d(8), y - 4, bw + ui_d(16), ui_d(40), ui_d(20), UI_SURF2);
        ui_button(ren, -1, mid, y, lab, UI_SURF3, UI_INK, H_PADRESET, g_pad_tab, 1);
        y += ui_d(32) + ui_d(24);
    }
    *py_l = *py_r = y;
}
