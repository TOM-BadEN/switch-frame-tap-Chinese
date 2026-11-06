/* padcfg.h - v0.7.20: what the Controller menu sets (ui.h / padui.h draw it,
 * pad.h plays it). Only data and its rules here: no drawing, no prefs_save,
 * no toasts - the callers do those.
 *
 * Three tables say which physical input is which Switch button:
 *   keyboard: two keys per Switch button or stick direction (by place on the
 *             keyboard - scancodes - so WASD stays WASD on AZERTY)
 *   mouse:    each mouse button / wheel direction -> one Switch button
 *   controller: one SDL button (or trigger) per Switch button; the sticks
 *             stay analog (left to left, right to right)
 * A key or controller button drives one Switch button only: binding it again
 * moves it. */
#pragma once

enum { PS_A, PS_B, PS_X, PS_Y, PS_L3, PS_R3, PS_L, PS_R, PS_ZL, PS_ZR, PS_PLUS, PS_MINUS,
       PS_LEFT, PS_UP, PS_RIGHT, PS_DOWN, PS_HOME, PS_CAPTURE,
       PS_LS_UP, PS_LS_LEFT, PS_LS_DOWN, PS_LS_RIGHT, PS_RS_UP, PS_RS_LEFT, PS_RS_DOWN, PS_RS_RIGHT, PS_N };
#define PS_NBTN 18                         /* PS_A .. PS_CAPTURE: the buttons; the rest are stick directions */

/* the message's bit for each button (HidNpadButton) */
static const unsigned char ps_hidbit[PS_NBTN] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 18, 19 };
static const char *const ps_name[PS_N] = {
    "A", "B", "X", "Y", "按下左摇杆", "按下右摇杆", "L", "R", "ZL", "ZR", "+", "-",
    "方向键左", "方向键上", "方向键右", "方向键下", "HOME", "Capture",
    "左摇杆向上", "左摇杆向左", "左摇杆向下", "左摇杆向右",
    "右摇杆向上", "右摇杆向左", "右摇杆向下", "右摇杆向右" };
static const char *const ps_key[PS_N] = {      /* the names in viewer.ini */
    "a", "b", "x", "y", "l3", "r3", "l", "r", "zl", "zr", "plus", "minus",
    "dleft", "dup", "dright", "ddown", "home", "capture",
    "lsup", "lsleft", "lsdown", "lsright", "rsup", "rsleft", "rsdown", "rsright" };

enum { CT_TRIG_L = 100, CT_TRIG_R = 101 };   /* a controller code: an SDL button (0..) or one of these */
enum { PMB_LEFT, PMB_RIGHT, PMB_MIDDLE, PMB_X1, PMB_X2, PMB_WHEEL_UP, PMB_WHEEL_DOWN, PMB_N };
static const char *const mb_name[PMB_N] = { "鼠标左键", "鼠标右键", "鼠标中键", "后退键", "前进键",
                                           "滚轮向上", "滚轮向下" };
static const char *const mb_key[PMB_N] = { "left", "right", "middle", "x1", "x2", "wup", "wdown" };

static int g_pad_send = 1;                 /* a controller on this computer plays the Switch */
static int g_pad_kbd = 0;                  /* ...and the keyboard (off by default: it takes the letter keys) */
static int g_pad_mouse = 0;                /* ...and the mouse (off: it takes the pointer) */
static int g_pad_mstick = 0;               /* the mouse moves 0 = the right stick, 1 = the left */
static int g_pad_msens = 50;               /* 5..100 */
static int g_pad_minvert = 0;
static int g_pad_dz = 8;                   /* controller sticks: dead zone, percent */
static int g_pad_trig = 30;                /* controller triggers: press point, percent */
static char g_pad_pref[64] = "";           /* the controller to use when it is connected ("" = the first) */

static int g_bind_kb[2][PS_N];             /* SDL scancodes, 0 = none */
static int g_bind_ctl[PS_NBTN];            /* SDL_GameControllerButton or CT_TRIG_*, -1 = none */
static int g_bind_mouse[PMB_N];             /* a button (PS_A..PS_CAPTURE), -1 = none */

static const struct { const char *name; int *v; int lo, hi, step, def; const char *unit; } pad_sliders[] = {
    { "摇杆死区",  &g_pad_dz,   0, 30, 1, 8,  "%" },
    { "扳机触发阈值", &g_pad_trig, 5, 90, 5, 30, "%" },
    { "鼠标灵敏度",      &g_pad_msens, 5, 100, 5, 50, "" },
};
#define PAD_NSLIDERS 3

/* binding by pressing the next key / button: 0 = off, 1 = keyboard, 2 = controller */
static int g_pcap_dev = 0, g_pcap_slot = 0, g_pcap_cell = 0;

static const char *pcfg_ct_name(int code)
{
    static const char *const n[] = { "下方（A）", "右侧（B）", "左侧（X）", "上方（Y）", "Back / Share", "Guide / Home", "Start / Options",
                                     "左摇杆", "右摇杆", "左肩键", "右肩键", "方向键上", "方向键下", "方向键左",
                                     "方向键右", "Misc / Share", "背键 1", "背键 2", "背键 3", "背键 4", "触摸板" };
    if (code == CT_TRIG_L) return "左扳机";
    if (code == CT_TRIG_R) return "右扳机";
    if (code >= 0 && code < (int)(sizeof(n) / sizeof(n[0]))) return n[code];
    return "-";
}

/* the key's name, ASCII only (the viewer's font has nothing else) */
static const char *pcfg_key_name(int sc)
{
    static char b[40];
    const char *n = sc > 0 ? SDL_GetScancodeName((SDL_Scancode)sc) : "";
    if (!n || !n[0]) return sc > 0 ? "Key" : "-";
    size_t i = 0;
    for (; n[i] && i < sizeof(b) - 1; i++) b[i] = (n[i] >= 32 && n[i] < 127) ? n[i] : '?';
    b[i] = 0;
    return b;
}

static void pcfg_defaults_kb(void)
{
    memset(g_bind_kb, 0, sizeof(g_bind_kb));
    static const int d[PS_N] = {           /* Ryujinx's defaults */
        SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_F, SDL_SCANCODE_H,
        SDL_SCANCODE_E, SDL_SCANCODE_U, SDL_SCANCODE_Q, SDL_SCANCODE_O, SDL_SCANCODE_RETURN, SDL_SCANCODE_MINUS,
        SDL_SCANCODE_LEFT, SDL_SCANCODE_UP, SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN, SDL_SCANCODE_HOME, SDL_SCANCODE_END,
        SDL_SCANCODE_W, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_I, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L };
    for (int i = 0; i < PS_N; i++) g_bind_kb[0][i] = d[i];
    g_bind_kb[1][PS_PLUS] = SDL_SCANCODE_EQUALS;
    g_bind_kb[1][PS_MINUS] = SDL_SCANCODE_KP_MINUS;
}

static void pcfg_defaults_ctl(void)
{
    /* by place: the bottom button is the Switch's B on every controller */
    static const int d[PS_NBTN] = {
        SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_X,
        SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, CT_TRIG_L, CT_TRIG_R,
        SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_MISC1 };
    for (int i = 0; i < PS_NBTN; i++) g_bind_ctl[i] = d[i];
}

static void pcfg_defaults_mouse(void)
{
    for (int i = 0; i < PMB_N; i++) g_bind_mouse[i] = -1;
    g_bind_mouse[PMB_LEFT] = PS_ZR;
    g_bind_mouse[PMB_RIGHT] = PS_ZL;
    g_bind_mouse[PMB_MIDDLE] = PS_R3;
    g_bind_mouse[PMB_X1] = PS_L;
    g_bind_mouse[PMB_X2] = PS_R;
    g_pad_mstick = 0; g_pad_msens = 50; g_pad_minvert = 0;
}

static void pcfg_defaults_ctl_options(void) { g_pad_dz = 8; g_pad_trig = 30; }

static void pad_cfg_defaults(void)
{
    g_pad_send = 1; g_pad_kbd = 0; g_pad_mouse = 0;
    g_pad_pref[0] = 0;
    pcfg_defaults_kb();
    pcfg_defaults_ctl();
    pcfg_defaults_ctl_options();
    pcfg_defaults_mouse();
}

/* bind a key: it leaves any other Switch button it drove; returns that button, or -1 */
static int pcfg_set_key(int slot, int cell, int sc)
{
    int was = -1;
    if (sc > 0)
        for (int c = 0; c < 2; c++)
            for (int s = 0; s < PS_N; s++)
                if (g_bind_kb[c][s] == sc && !(s == slot && c == cell)) { g_bind_kb[c][s] = 0; was = s; }
    g_bind_kb[cell][slot] = sc;
    return was;
}

static int pcfg_set_ctl(int slot, int code)
{
    int was = -1;
    if (code >= 0)
        for (int s = 0; s < PS_NBTN; s++)
            if (g_bind_ctl[s] == code && s != slot) { g_bind_ctl[s] = -1; was = s; }
    g_bind_ctl[slot] = code;
    return was;
}

/* the Xbox / PlayStation label layout and back: A <-> B, X <-> Y */
static void pcfg_swap_face(void)
{
    int t = g_bind_ctl[PS_A]; g_bind_ctl[PS_A] = g_bind_ctl[PS_B]; g_bind_ctl[PS_B] = t;
    t = g_bind_ctl[PS_X]; g_bind_ctl[PS_X] = g_bind_ctl[PS_Y]; g_bind_ctl[PS_Y] = t;
}

static int pcfg_slot_by_key(const char *s)
{
    for (int i = 0; i < PS_N; i++) if (!strcmp(s, ps_key[i])) return i;
    return -1;
}

/* one line of viewer.ini */
static void pad_prefs_load(const char *line)
{
    int v, a, b;
    char name[24];
    if (sscanf(line, "controller_to_switch=%d", &v) == 1) g_pad_send = v != 0;
    else if (sscanf(line, "keyboard_to_switch=%d", &v) == 1) g_pad_kbd = v != 0;
    else if (sscanf(line, "mouse_to_switch=%d", &v) == 1) g_pad_mouse = v != 0;
    else if (sscanf(line, "mouse_stick=%d", &v) == 1) g_pad_mstick = v != 0;
    else if (sscanf(line, "mouse_speed=%d", &v) == 1) g_pad_msens = SDL_clamp(v, 5, 100);
    else if (sscanf(line, "mouse_invert=%d", &v) == 1) g_pad_minvert = v != 0;
    else if (sscanf(line, "stick_deadzone=%d", &v) == 1) g_pad_dz = SDL_clamp(v, 0, 30);
    else if (sscanf(line, "trigger_point=%d", &v) == 1) g_pad_trig = SDL_clamp(v, 5, 90);
    else if (!strncmp(line, "controller_name=", 16)) {
        snprintf(g_pad_pref, sizeof(g_pad_pref), "%.63s", line + 16);
        g_pad_pref[strcspn(g_pad_pref, "\r\n")] = 0;
    } else if (sscanf(line, "key_%23[a-z0-9]=%d,%d", name, &a, &b) == 3) {
        const int s = pcfg_slot_by_key(name);
        if (s >= 0 && a >= 0 && a < SDL_NUM_SCANCODES && b >= 0 && b < SDL_NUM_SCANCODES) { g_bind_kb[0][s] = a; g_bind_kb[1][s] = b; }
    } else if (sscanf(line, "pad_%23[a-z0-9]=%d", name, &a) == 2) {
        const int s = pcfg_slot_by_key(name);
        if (s >= 0 && s < PS_NBTN && (a == -1 || a == CT_TRIG_L || a == CT_TRIG_R || (a >= 0 && a < SDL_CONTROLLER_BUTTON_MAX)))
            g_bind_ctl[s] = a;
    } else if (!strncmp(line, "mouse_btn_", 10)) {
        char mb[24], to[24];
        if (sscanf(line, "mouse_btn_%23[a-z0-9]=%23[a-z0-9]", mb, to) == 2) {
            int m = -1;
            for (int i = 0; i < PMB_N; i++) if (!strcmp(mb, mb_key[i])) m = i;
            const int s = !strcmp(to, "none") ? -1 : pcfg_slot_by_key(to);
            const int known = !strcmp(to, "none") || (s >= 0 && s < PS_NBTN);
            if (m >= 0 && known) g_bind_mouse[m] = s;
        }
    }
}

static void pad_prefs_save(FILE *f)
{
    fprintf(f, "controller_to_switch=%d\nkeyboard_to_switch=%d\nmouse_to_switch=%d\n", g_pad_send, g_pad_kbd, g_pad_mouse);
    fprintf(f, "mouse_stick=%d\nmouse_speed=%d\nmouse_invert=%d\nstick_deadzone=%d\ntrigger_point=%d\n",
            g_pad_mstick, g_pad_msens, g_pad_minvert, g_pad_dz, g_pad_trig);
    if (g_pad_pref[0]) fprintf(f, "controller_name=%s\n", g_pad_pref);
    for (int s = 0; s < PS_N; s++) fprintf(f, "key_%s=%d,%d\n", ps_key[s], g_bind_kb[0][s], g_bind_kb[1][s]);
    for (int s = 0; s < PS_NBTN; s++) fprintf(f, "pad_%s=%d\n", ps_key[s], g_bind_ctl[s]);
    for (int m = 0; m < PMB_N; m++)
        fprintf(f, "mouse_btn_%s=%s\n", mb_key[m], g_bind_mouse[m] >= 0 ? ps_key[g_bind_mouse[m]] : "none");
}
