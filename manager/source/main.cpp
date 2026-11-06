/*
 * Switch Frame Tap - the manager app (hbmenu). Settings, status and a setup
 * check for the streaming sysmodule, in the spirit of SysDVR's settings app.
 *
 *  - Settings live in sdmc:/config/switch-frame-tap/config.ini (the format
 *    tier4/applet-mitm/source/applet_mitm_armfile.hpp BuildReleaseArm reads).
 *    Every change is written at once and, if the sysmodule is running, it is
 *    told to reload (sftap command 2): a stream in progress restarts with it.
 *  - Stream on/off goes through sftap (command 1) when the sysmodule runs,
 *    else the file it reads at boot (stream-off).
 *  - Start with the console: the launcher's flags/boot2.flag (v0.7.13: the
 *    donor, C21, starts the sysmodule once the system has booted).
 *
 * SDL2 for drawing (ui.hpp: v0.7.14's look, the PC viewer's theme - a sidebar
 * of pages, rounded cards, switches), the console's own shared fonts through
 * SDL2_ttf, libnx pad/touch for input. tools/manager-preview draws the same
 * screens on a PC.
 */
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <functional>
#include "../../common/sound_patch.hpp"
#include "../../tier4/applet-mitm/source/shot_buttons.hpp"
#ifdef SFT_PREVIEW
#include "preview_extras.hpp"   /* tools/manager-preview: the PC preview's stand-ins */
#else
#include "extras.hpp"
#endif
#include "ui.hpp"

namespace {

    constexpr const char *AppVersion  = "0.7.21.1";
    constexpr u64 ModuleTid           = 0x0100000000000C20ull;
    constexpr const char *ModuleDir   = "sdmc:/atmosphere/contents/0100000000000C20";
    constexpr const char *ModuleNsp   = "sdmc:/atmosphere/contents/0100000000000C20/exefs.nsp";
    /* v0.7.13: the launcher (the donor, C21) holds the boot flag and starts the
     * sysmodule once the system has booted; C20's own flag is an older install's */
    constexpr const char *BootFlag    = "sdmc:/atmosphere/contents/0100000000000C21/flags/boot2.flag";
    constexpr const char *OldBootFlag = "sdmc:/atmosphere/contents/0100000000000C20/flags/boot2.flag";
    constexpr const char *LauncherDir = "sdmc:/atmosphere/contents/0100000000000C21";
    constexpr const char *LauncherTxt = "sdmc:/config/switch-frame-tap/launcher.txt";
    constexpr const char *ConfigDir   = "sdmc:/config/switch-frame-tap";
    constexpr const char *ConfigIni   = "sdmc:/config/switch-frame-tap/config.ini";
    constexpr const char *StreamOff   = "sdmc:/config/switch-frame-tap/stream-off";
    constexpr const char *ExclFile    = "sdmc:/config/switch-frame-tap/excluded.txt";

    /* v0.1.1b: the never-attach list (one hex program id per line) */
    size_t CountExcluded() {
        size_t n = 0;
        if (FILE *f = std::fopen(ExclFile, "rb")) {
            char line[64];
            while (std::fgets(line, sizeof(line), f)) { if (std::strlen(line) >= 16) { ++n; } }
            std::fclose(f);
        }
        return n;
    }
    constexpr const char *TestArmFile = "sdmc:/applet-mitm.armed";
    constexpr const char *ShotDir     = "sdmc:/switch/switch-frame-tap/screenshots";

    /* v0.7.11: the system's screenshot folder, as the sysmodule found it
     * (Nintendo/Album, or the redirected one under emuMMC) */
    std::string AlbumRoot() {
        std::string r = "sdmc:/Nintendo/Album";
        if (FILE *f = std::fopen("sdmc:/config/switch-frame-tap/album_path.txt", "rb")) {
            char b[0x110] = "";
            if (std::fgets(b, sizeof(b), f)) {
                std::string l = b;
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) { l.pop_back(); }
                if (l.rfind("sdmc:/", 0) == 0) { r = l; }
            }
            std::fclose(f);
        }
        return r;
    }

    std::string BaseName(const std::string &p) { const size_t k = p.rfind('/'); return k == std::string::npos ? p : p.substr(k + 1); }

    /* when it was taken, as YYYYMMDDHHMMSSnn, from either name:
     * "2026-09-29_17-27-10.png" or "2026092917271000-SwitchFrameTap.png" */
    std::string ShotKey(const std::string &path) {
        const std::string f = BaseName(path);
        if (f.size() >= 23 && f[10] == '_') {
            std::string k;
            for (char c : f.substr(0, 19)) { if (c >= '0' && c <= '9') { k += c; } }
            return k + (f.size() > 23 ? f.substr(19, f.size() - 23) : "00");
        }
        return f.substr(0, 16);
    }

    /* the screenshots, newest first: Switch Frame Tap's folder and (v0.7.11)
     * the ones saved next to the system's, Album/YYYY/MM/DD/*-SwitchFrameTap.png */
    std::vector<std::string> ListShots() {
        std::vector<std::string> v;
        auto scan = [&v](const std::string &dir, const char *suffix) {
            if (DIR *d = opendir(dir.c_str())) {
                const size_t sl = std::strlen(suffix);
                while (dirent *e = readdir(d)) {
                    const size_t n = std::strlen(e->d_name);
                    if (n > sl && std::strcmp(e->d_name + n - sl, suffix) == 0) { v.push_back(dir + "/" + e->d_name); }
                }
                closedir(d);
            }
        };
        auto subdirs = [](const std::string &dir) {
            std::vector<std::string> out;
            if (DIR *d = opendir(dir.c_str())) {
                while (dirent *e = readdir(d)) {
                    if (e->d_name[0] != '.' && std::strlen(e->d_name) <= 4 && std::isdigit(static_cast<unsigned char>(e->d_name[0]))) { out.push_back(dir + "/" + e->d_name); }
                }
                closedir(d);
            }
            return out;
        };
        scan(ShotDir, ".png");
        for (const auto &y : subdirs(AlbumRoot())) {
            for (const auto &m : subdirs(y)) {
                for (const auto &d : subdirs(m)) { scan(d, "-SwitchFrameTap.png"); }
            }
        }
        std::sort(v.begin(), v.end(), [](const std::string &a, const std::string &b) { return ShotKey(a) > ShotKey(b); });
        return v;
    }

    /* "2026-09-29  17:27:10", from either name */
    std::string ShotLabel(const std::string &path) {
        const std::string k = ShotKey(path);
        if (k.size() >= 14) {
            std::string l = k.substr(0, 4) + "-" + k.substr(4, 2) + "-" + k.substr(6, 2) + "  " + k.substr(8, 2) + ":" + k.substr(10, 2) + ":" + k.substr(12, 2);
            if (k.size() >= 16 && k.substr(14, 2) != "00") { l += " (" + k.substr(14, 2) + ")"; }
            if (BaseName(path).find("-SwitchFrameTap") != std::string::npos) { l += "  相册"; }
            return l;
        }
        return BaseName(path);
    }

    bool FileExists(const char *p) { struct stat st; return stat(p, &st) == 0; }

    bool TouchFile(const char *p) {
        FILE *f = std::fopen(p, "wb");
        if (f == nullptr) { return false; }
        std::fclose(f);
        return true;
    }

    /* ---- the sysmodule's control service (applet_mitm_control.hpp) ------ */

    struct StreamStatus {
        u32 version, state, enabled, width, height, fps_x10, game_fps_x10, sessions;
        u32 shot_enabled, shots, shot_fails, reserved;     /* version 2 (M99) */
        u64 app_tid; u32 app_excluded, reserved3;          /* version 3 (v0.1.1b) */
    };

    Service g_sftap;
    bool g_sftap_open = false;

    bool ModuleRunning() {
        u64 pid = 0;
        return R_SUCCEEDED(pmdmntGetProcessId(&pid, ModuleTid));
    }

    /* only once the module's process exists: sm would otherwise defer the
     * request until someone registers "sftap" - forever, without the module */
    bool SftapConnect() {
        if (g_sftap_open) { return true; }
        if (!ModuleRunning()) { return false; }
        g_sftap_open = R_SUCCEEDED(smGetService(&g_sftap, "sftap"));
        return g_sftap_open;
    }

    bool SftapStatus(StreamStatus *st) {
        if (!SftapConnect()) { return false; }
        if (R_FAILED(serviceDispatchOut(&g_sftap, 0, *st))) { serviceClose(&g_sftap); g_sftap_open = false; return false; }
        return true;
    }

    bool SftapSetEnabled(bool on) {
        if (!SftapConnect()) { return false; }
        const u8 v = on ? 1 : 0;
        return R_SUCCEEDED(serviceDispatchIn(&g_sftap, 1, v));
    }

    void SftapReload() {
        if (SftapConnect()) { serviceDispatch(&g_sftap, 2); }
    }

    /* ---- settings (config.ini) ------------------------------------------ */

    const char *const QualityNames[]  = { "高", "中", "低" };
    const char *const ConnKeys[]      = { "usb", "webcam", "webcam-any", "network" };
    const char *const ConnNames[]     = { "USB - 电脑客户端", "USB - 摄像头（OBS）", "USB - 摄像头（通用）", "网络" };
    constexpr int ConnCount = 4, ConnNetwork = 3;
    const char *const QualityKeys[]   = { "high", "medium", "low" };
    const int KeyframeFrames[]        = { 30, 60, 120, 240 };
    const char *const KeyframeNames[] = { "每 0.5 秒", "每秒", "每 2 秒", "每 4 秒" };
    const int DelaySeconds[]          = { 5, 10, 20, 30, 60 };
    const char *const ComboNames[]    = { "L3 + R3（按下双摇杆）", "L + R + 方向键下", "ZL + ZR + 方向键下", "Minus + 方向键下", "自定义" };
    constexpr int ComboCustom = 4;   /* v0.7.11: screenshot_custom */

    struct Settings {
        int quality = 0;        /* index into QualityKeys */
        int keyframe = 1;       /* index into KeyframeFrames */
        bool cap720 = false;
        bool scale1080 = false;
        bool mem_boost = false;   /* v0.7.20: memory at 1600 MHz while streaming */   /* v0.7.20 (#65): docked pictures between 720p and 1080p go up to 1080p */
        int delay = 2;          /* index into DelaySeconds */
        bool any_fw = false;
        bool shot = false;
        int shot_combo = 0;     /* index into ComboNames */
        uint32_t shot_mask = 0;   /* v0.7.11: the custom combo's buttons (HidNpadButton bits 0-15) */
        bool shot_capture = false;/* v0.7.11: ...and the Capture button */
        bool audio = true;      /* v0.3 */
        bool shot_album = false;  /* v0.7.11: screenshots next to the system's (Album) */
        bool detach_home = true;  /* v0.7.11 (PR #13): end the stream on HOME; kept when saving */
        bool screen_off = false;  /* v0.7.12: the console's screen dark while streaming */
        std::string early = "1";  /* v0.7.12: early_collect (1 / 0 / ab); kept when saving */
        int usb_gop = 240;        /* usb_keyframe_interval (0..250); kept when saving */
        bool home_menu = false;   /* v0.7.14: stream the HOME menu and system applets too (beta); from the next boot */
        bool overlays = false;    /* v0.7.14: the system overlays in the stream (beta); from the next boot */
        bool hb_docked = false;   /* v0.7.18: homebrew is told the console is docked (1080p); the next app started */
        bool viewer_input = false;/* v0.7.20: a controller on the viewer's PC plays the console; right away */
        bool update_betas = false;/* v0.7.14: "Check for updates" offers pre-releases too (this app's own key) */
        int conn = 0;           /* v0.7: connection - 0 USB (PC viewer), 1 USB webcam, 2 (v0.7.7) USB webcam NV12, 3 network; from the next boot */
    };

    bool IniGet(const std::string &ini, const char *key, std::string *out) {
        size_t i = 0;
        const size_t klen = std::strlen(key);
        while (i < ini.size()) {
            size_t e = ini.find('\n', i);
            if (e == std::string::npos) { e = ini.size(); }
            std::string line = ini.substr(i, e - i);
            i = e + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) { line.pop_back(); }
            size_t a = line.find_first_not_of(" \t");
            if (a == std::string::npos || line[a] == '#' || line[a] == ';' || line[a] == '[') { continue; }
            const size_t eq = line.find('=', a);
            if (eq == std::string::npos) { continue; }
            size_t kend = eq;
            while (kend > a && (line[kend - 1] == ' ' || line[kend - 1] == '\t')) { --kend; }
            if (kend - a != klen || line.compare(a, klen, key) != 0) { continue; }
            const size_t v = line.find_first_not_of(" \t", eq + 1);
            *out = v == std::string::npos ? "" : line.substr(v);
            return true;
        }
        return false;
    }

    Settings LoadSettings() {
        Settings s;
        std::string ini;
        if (FILE *f = std::fopen(ConfigIni, "rb")) {
            char buf[1024];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) { ini.append(buf, n); }
            std::fclose(f);
        }
        std::string v;
        if (IniGet(ini, "quality", &v)) { for (int i = 0; i < 3; ++i) { if (v == QualityKeys[i]) { s.quality = i; } } }
        if (IniGet(ini, "keyframe_interval", &v)) { for (int i = 0; i < 4; ++i) { if (std::atoi(v.c_str()) == KeyframeFrames[i]) { s.keyframe = i; } } }
        if (IniGet(ini, "max_resolution", &v)) { s.cap720 = v == "720"; }
        if (IniGet(ini, "scale_to_1080", &v)) { s.scale1080 = v == "1"; }
        if (IniGet(ini, "memory_boost", &v)) { s.mem_boost = v == "1"; }
        if (IniGet(ini, "start_delay", &v)) { for (int i = 0; i < 5; ++i) { if (std::atoi(v.c_str()) == DelaySeconds[i]) { s.delay = i; } } }
        if (IniGet(ini, "allow_untested_firmware", &v)) { s.any_fw = v == "1"; }
        if (IniGet(ini, "screenshot", &v)) { s.shot = v == "1"; }
        if (IniGet(ini, "screenshot_location", &v)) { s.shot_album = v == "album"; }
        if (IniGet(ini, "detach_on_home", &v)) { s.detach_home = v != "0"; }
        if (IniGet(ini, "screen_off", &v)) { s.screen_off = v == "1"; }
        if (IniGet(ini, "early_collect", &v) && (v == "0" || v == "1" || v == "ab")) { s.early = v; }
        if (IniGet(ini, "usb_keyframe_interval", &v) && !v.empty() && v.find_first_not_of("0123456789") == std::string::npos && v.size() <= 3 && std::atoi(v.c_str()) <= 250) { s.usb_gop = std::atoi(v.c_str()); }
        if (IniGet(ini, "audio", &v)) { s.audio = v != "0"; }
        if (IniGet(ini, "home_menu", &v)) { s.home_menu = v == "1"; }
        if (IniGet(ini, "overlays", &v)) { s.overlays = v == "1"; }
        if (IniGet(ini, "homebrew_docked", &v)) { s.hb_docked = v == "1"; }
        if (IniGet(ini, "viewer_input", &v)) { s.viewer_input = v == "1"; }
        if (IniGet(ini, "update_betas", &v)) { s.update_betas = v == "1"; }
        /* the v0.4-v0.7 keys first, then the one that replaced them */
        if (IniGet(ini, "usb_mode", &v) && v == "webcam") { s.conn = 1; }
        if (IniGet(ini, "network", &v) && v == "1") { s.conn = ConnNetwork; }
        if (IniGet(ini, "connection", &v)) { s.conn = v == "network" ? ConnNetwork : v == "webcam-any" ? 2 : v == "webcam" ? 1 : 0; }
        if (IniGet(ini, "screenshot_buttons", &v)) { const int k = std::atoi(v.c_str()); if (k >= 0 && k <= ComboCustom) { s.shot_combo = k; } }
        if (IniGet(ini, "screenshot_custom", &v)) { s.shot_mask = sft::shotbuttons::Parse(v.c_str(), &s.shot_capture); }
        return s;
    }

    bool SaveSettings(const Settings &s) {
        mkdir("sdmc:/config", 0777);
        mkdir(ConfigDir, 0777);
        FILE *f = std::fopen(ConfigIni, "wb");
        if (f == nullptr) { return false; }
        char custom[96];
        sft::shotbuttons::Format(s.shot_mask, s.shot_capture, custom, sizeof(custom));
        std::fprintf(f,
            "# Switch Frame Tap settings - written by the manager app; editing by hand is fine.\n"
            "# quality: high | medium | low  (high: the best picture and the most USB bandwidth)\n"
            "quality = %s\n"
            "# keyframe_interval: frames between keyframes (60 = once a second at 60 fps)\n"
            "keyframe_interval = %d\n"
            "# usb_keyframe_interval: over USB, frames between keyframes when longer than keyframe_interval (at most 250;\n"
            "#                        the viewer asks for one when it needs it) | 0 = keyframe_interval over USB too\n"
            "usb_keyframe_interval = %d\n"
            "# max_resolution: 1080 (native) | 720\n"
            "max_resolution = %s\n"
            "# scale_to_1080: 1 = a docked picture between 720p and 1080p (Super Mario Odyssey's 1600x900) is scaled up\n"
            "#                to 1080p instead of down to 720p - more bandwidth and a frame more latency. Next stream.\n"
            "scale_to_1080 = %d\n"
            "# memory_boost: 1 = the memory runs at 1600 MHz (the docked speed) or more while a stream runs - heavy games\n"
            "#               copy faster at stock clocks; a little more battery. Next stream.\n"
            "memory_boost = %d\n"
            "# start_delay: seconds after boot before streaming can start\n"
            "start_delay = %d\n"
            "# allow_untested_firmware: 1 streams on firmware this version was not tested on\n"
            "allow_untested_firmware = %d\n"
            "# screenshot: 1 = a button combo saves a native-resolution PNG to /switch/switch-frame-tap/screenshots\n"
            "screenshot = %d\n"
            "# screenshot_buttons: 0 = L3 + R3, 1 = L + R + D-pad Down, 2 = ZL + ZR + D-pad Down, 3 = Minus + D-pad Down,\n"
            "#                     4 = screenshot_custom (A B X Y L R ZL ZR L3 R3 Plus Minus Up Down Left Right Capture)\n"
            "screenshot_buttons = %d\n"
            "screenshot_custom = %s\n"
            "# screenshot_location: app (/switch/switch-frame-tap/screenshots) | album (next to the system's screenshots,\n"
            "#                      Nintendo/Album/YYYY/MM/DD - under emuMMC its own Nintendo folder)\n"
            "screenshot_location = %s\n"
            "# detach_on_home: 1 = pressing HOME ends the stream and frees its memory | 0 = keep showing the paused game\n"
            "detach_on_home = %d\n"
            "# screen_off: 1 = the console's screen goes dark while a stream runs (back on when it stops, or with HOME)\n"
            "screen_off = %d\n"
            "# early_collect: 1 = send a finished 1080p frame without waiting for the next one | 0 = the old way | ab = test both\n"
            "early_collect = %s\n"
            "# audio: 1 = send the game's sound to the PC viewer\n"
            "audio = %d\n"
            "# connection: usb (the PC viewer over USB) | webcam (a USB camera for OBS; no sound) |\n"
            "#             webcam-any (an uncompressed USB camera for any camera app; no sound) |\n"
            "#             network (the PC viewer over Wi-Fi or the dock's LAN port - play docked). Read at boot.\n"
            "connection = %s\n"
            "# home_menu: 1 = also stream the HOME menu and the system applets when no game is on screen (beta). Read at boot.\n"
            "home_menu = %d\n"
            "# overlays: 1 = the volume bar, quick settings, power menu and Tesla/Ultrahand menus in the stream (beta). Read at boot.\n"
            "overlays = %d\n"
            "# homebrew_docked: 1 = homebrew started from the Homebrew Menu is told the console is docked, so apps that\n"
            "#                  choose their resolution from that draw at 1080p (beta). From the next app started.\n"
            "homebrew_docked = %d\n"
            "# viewer_input: 1 = a controller, keyboard or mouse on the PC plays the Switch through the PC viewer (an extra Pro Controller, beta)\n"
            "viewer_input = %d\n"
            "# update_betas: 1 = Check for updates in the app also offers betas (pre-releases)\n"
            "update_betas = %d\n",
            QualityKeys[s.quality], KeyframeFrames[s.keyframe], s.usb_gop, s.cap720 ? "720" : "1080", s.scale1080 ? 1 : 0, s.mem_boost ? 1 : 0,
            DelaySeconds[s.delay], s.any_fw ? 1 : 0, s.shot ? 1 : 0, s.shot_combo, custom, s.shot_album ? "album" : "app", s.detach_home ? 1 : 0, s.screen_off ? 1 : 0, s.early.c_str(),
            s.audio ? 1 : 0, ConnKeys[s.conn], s.home_menu ? 1 : 0, s.overlays ? 1 : 0, s.hb_docked ? 1 : 0, s.viewer_input ? 1 : 0, s.update_betas ? 1 : 0);
        std::fclose(f);
        return true;
    }


    /* ---- the pages ------------------------------------------------------- */

    /* Section: a group's title. Hero and Tiles: the home page's status card
     * and its small facts. The rest are cards: Info (a fact), Toggle (a
     * switch), Choice (< value >), Action (opens or does something). A card
     * with focus shows its help under it. */
    enum class Kind { Section, Hero, Tiles, Info, Toggle, Choice, Action };

    struct Tile {
        std::string label;
        std::function<std::string()> value;
        std::function<SDL_Color()> color;
    };

    struct Row {
        Kind kind;
        std::string label;
        std::function<std::string()> value;   /* Info, Choice, Action: the text on the right; Toggle: a note beside the switch */
        std::function<SDL_Color()> color;     /* that text's colour (nullptr: the default) */
        std::function<void(int)> change;      /* +1 / -1 (a Toggle flips); nullptr: nothing to change */
        std::string help;
        std::function<bool()> on;             /* Toggle */
        std::vector<Tile> tiles;              /* Tiles */
    };

    bool Selectable(const Row &r) { return r.kind != Kind::Section && r.kind != Kind::Hero && r.kind != Kind::Tiles; }

    Row Section(std::string label) { return { Kind::Section, std::move(label), nullptr, nullptr, nullptr, "", nullptr, {} }; }
    Row Info(std::string label, std::function<std::string()> value, std::function<SDL_Color()> color, std::string help) {
        return { Kind::Info, std::move(label), std::move(value), std::move(color), nullptr, std::move(help), nullptr, {} };
    }
    Row Toggle(std::string label, std::function<bool()> on, std::function<void(int)> change, std::string help,
               std::function<std::string()> note = nullptr, std::function<SDL_Color()> note_color = nullptr) {
        return { Kind::Toggle, std::move(label), std::move(note), std::move(note_color), std::move(change), std::move(help), std::move(on), {} };
    }
    Row Choice(std::string label, std::function<std::string()> value, std::function<void(int)> change, std::string help,
               std::function<SDL_Color()> color = nullptr) {
        return { Kind::Choice, std::move(label), std::move(value), std::move(color), std::move(change), std::move(help), nullptr, {} };
    }
    Row Action(std::string label, std::function<std::string()> value, std::function<void(int)> change, std::string help,
               std::function<SDL_Color()> color = nullptr) {
        return { Kind::Action, std::move(label), std::move(value), std::move(color), std::move(change), std::move(help), nullptr, {} };
    }

    struct Page {
        const char *name;
        std::vector<Row> rows;
        int sel = -1;
        float scroll = 0, target = 0;
        int seen_sel = -2;          /* the selection the scroll last followed */
    };

    bool g_open_gallery = false;
    bool g_record_combo = false;   /* v0.7.11: the custom screenshot combo's recording screen */
    const extras::Tool *g_pending_install = nullptr;   /* v0.7.5: Get extras - run from the main loop */
    int g_pending_update = 0;      /* v0.7.14: 1 check for an update, 2 install it - run from the main loop */

    struct App {
        Settings set = LoadSettings();
        StreamStatus st = {};
        bool running = false, have_status = false, installed = false, boot = false, test_install = false;
        bool stream_file_on = true;
        size_t shot_files = 0, excluded = 0;
        bool confirm_clear = false;
        sftsound::State sound = sftsound::State::Missing;   /* v0.7.4: the bundled dvr-patches */
        int confirm_extra = -1;                              /* v0.7.5: the Get extras row waiting for its second A */
        bool sound_changed = false;                          /* toggled since this app started: a restart applies it */
        u32 fw = 0;
        u64 ams = 0;
        std::string free_mem, others;                        /* v0.7.13: launcher.txt */
        std::string toast;
        u32 toast_frames = 0;
        /* v0.7.14: Switch Frame Tap's own updates (extras::CheckUpdate) */
        enum class Upd { Unknown, Latest, Found, Installed, Failed } upd = Upd::Unknown;
        extras::Release upd_rel;
        std::string upd_msg;
        bool confirm_update = false;
        std::vector<Page> pages;
        int page = 0;
        bool in_content = false;    /* the keys move in the page (else in the sidebar) */

        void Refresh() {
            installed = FileExists(ModuleNsp);
            boot = FileExists(BootFlag) || FileExists(OldBootFlag);
            test_install = FileExists(TestArmFile);
            stream_file_on = !FileExists(StreamOff);
            shot_files = ListShots().size();
            excluded = CountExcluded();
            sound = sftsound::Get();
            running = ModuleRunning();
            have_status = running && SftapStatus(&st);
        }

        /* v0.7.13: the launcher's view of this boot - the memory sysmodules
         * share, and which sysmodules use it */
        void ReadLauncher() {
            free_mem.clear();
            others.clear();
            if (FILE *f = std::fopen(LauncherTxt, "r")) {
                char b[2048] = {};
                const size_t n = std::fread(b, 1, sizeof(b) - 1, f);
                std::fclose(f);
                b[n] = 0;
                if (const char *m = std::strstr(b, "system memory ")) {
                    const long kb = std::strtol(m + 14, nullptr, 10);
                    char t[32];
                    std::snprintf(t, sizeof(t), "%.1f MB", kb / 1024.0);
                    free_mem = t;
                }
                if (const char *o = std::strstr(b, "others=")) {
                    others = o + 7;
                    if (const size_t nl = others.find('\n'); nl != std::string::npos) { others.resize(nl); }
                    for (size_t i = 0; (i = others.find("; ", i)) != std::string::npos; ) { others.replace(i, 2, "\n"); }
                }
            }
        }

        bool LowMemoryBoot() {
            if (FILE *f = std::fopen(LauncherTxt, "r")) {
                char b[1024] = {};
                const size_t n = std::fread(b, 1, sizeof(b) - 1, f);
                std::fclose(f);
                b[n] = 0;
                return std::strstr(b, "result=low-memory") != nullptr;
            }
            return false;
        }

        void Toast(const std::string &s) { toast = s; toast_frames = 180; }

        std::string CustomComboText() const {
            char b[96];
            sft::shotbuttons::Format(set.shot_mask, set.shot_capture, b, sizeof(b));
            if (b[0] == 0) { return "自定义（尚未录入）"; }
            std::string t = b;
            for (size_t i = 0; (i = t.find('+', i)) != std::string::npos; i += 3) { t.replace(i, 1, " + "); }
            return t;
        }

        void Saved() {
            if (!SaveSettings(set)) { Toast("无法写入 config.ini"); return; }
            if (test_install) { Toast("已保存，但存在测试启用文件，后台模块会忽略 config.ini"); return; }
            if (have_status) { SftapReload(); Toast("已保存，立即生效"); }
            else { Toast("已保存，下次开机生效"); }
        }

        /* the settings the sysmodule reads at boot only */
        void SavedForBoot(const char *what) {
            if (!SaveSettings(set)) { Toast("无法写入 config.ini"); return; }
            Toast(test_install ? std::string("已保存，但存在测试启用文件，后台模块会忽略 config.ini")
                               : std::string("已保存，重启主机以") + what);
        }

        /* v0.4: the mode this boot is in (the sysmodule's word), else config.ini */
        int RunningConn() { return have_status ? ((st.reserved3 & 2) ? ConnNetwork : (st.reserved3 & 1) ? ((st.reserved3 & 8) ? 2 : 1) : 0) : set.conn; }
        bool RunningWebcam() { return RunningConn() == 1 || RunningConn() == 2; }
        bool RunningHomeMenu() { return have_status ? (st.reserved3 & 16) != 0 : set.home_menu; }   /* v0.7.14 */
        bool RunningOverlays() { return have_status ? (st.reserved3 & 32) != 0 : set.overlays; }     /* v0.7.14 */
        /* v0.7: the network transport this boot, and whether a viewer is on it */
        bool RunningNetwork() { return RunningConn() == ConnNetwork; }
        bool NetworkViewer() { return have_status && (st.reserved3 & 4) != 0; }
        std::string IpString() {
            u32 ip = 0;
            if (R_FAILED(nifmGetCurrentIpAddress(&ip)) || ip == 0) { return "离线"; }
            char b[20];
            std::snprintf(b, sizeof(b), "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24);
            return b;
        }

        /* the status card: what the sysmodule is doing, and what to do next */
        std::string StateTitle() {
            if (!installed) { return "未安装"; }
            if (!running) {
                if (!boot) { return "未设置开机启动"; }
                return LowMemoryBoot() ? "未启动" : "即将启动";
            }
            if (!have_status) { return "运行中"; }
            switch (st.state) {
                case 0: return "启动中";
                case 1: return "关闭";
                case 2: return RunningWebcam() ? "等待摄像头应用连接" : "等待客户端连接";
                case 3: return "等待游戏启动";
                case 4: return "串流中";
                case 5: return "编码器无响应";
                case 6: return "测试安装";
                case 7: return "固件未经测试";
                case 8: return "当前应用不参与串流";
                default: return "未知";
            }
        }

        std::string StateDetail() {
            if (!installed) { return "将 Switch Frame Tap 压缩包解压至 SD 卡根目录，然后重启主机。"; }
            if (!running) {
                if (!boot) { return "开启下方“开机启动”，然后重启主机。"; }
                return LowMemoryBoot() ? "其他后台模块占用了所需内存，请查看“安装检查”。"
                                       : "进入 HOME 菜单后，约在开机 30 秒时启动。";
            }
            if (!have_status) { return "检测到旧版后台模块，请更新。"; }
            char b[160];
            switch (st.state) {
                case 0: return "等待游戏启动。";
                case 1: return "开启下方“串流到电脑”即可恢复串流。";
                case 2:
                    if (RunningWebcam()) { return "在电脑上打开 OBS、相机或浏览器等摄像头应用，选择 Switch Frame Tap。"; }
                    if (RunningNetwork()) {
                        std::snprintf(b, sizeof(b), "打开电脑客户端，选择网络连接，连接至 %s。", IpString().c_str());
                        return b;
                    }
                    return "打开电脑客户端，并连接 USB-C 数据线。";
                case 3: return "启动游戏即可开始串流。";
                case 4:
                    std::snprintf(b, sizeof(b), "%u x %u%s", st.width, st.height,
                                  NetworkViewer() ? " · 网络" : RunningWebcam() ? " · 摄像头应用" : " · USB");
                    return b;
                case 5: return "请重启主机以恢复串流。";
                case 6: return "测试启用文件中未启用实时串流。";
                case 7: return "当前固件未启用串流，请查看“安装检查 → 允许未经测试的固件”。";
                case 8: return "当前应用已排除，请查看“串流 → 排除的应用”。";
                default: return "";
            }
        }

        SDL_Color StreamColor() {
            if (!installed || (running && have_status && (st.state == 5 || st.state == 7))) { return ui::Bad; }
            if (!running || !have_status) { return ui::Warn; }
            if (st.state == 4) { return ui::Good; }
            return st.state == 1 ? ui::Faint : ui::Acc;
        }

        std::string ConnShort() {
            switch (RunningConn()) {
                case 1: return "摄像头（OBS）";
                case 2: return "摄像头（通用）";
                case ConnNetwork: return "网络  " + IpString();
                default: return "USB";
            }
        }

        bool StreamOn() { return have_status ? st.enabled != 0 : stream_file_on; }

        void SetStream(bool on) {
            if (have_status) {
                if (!SftapSetEnabled(on)) { Toast("后台模块未响应"); }
                else { Toast(on ? "已开启串流" : "已关闭串流"); }
            } else {
                mkdir("sdmc:/config", 0777);
                mkdir(ConfigDir, 0777);
                if (on) { std::remove(StreamOff); } else { TouchFile(StreamOff); }
                Toast(on ? "下次开机将开启串流" : "下次开机将关闭串流");
            }
        }

        void SetBoot(bool on) {
            if (!installed) { Toast("未安装后台模块"); return; }
            std::remove(OldBootFlag);
            if (on) {
                mkdir(LauncherDir, 0777);
                mkdir((std::string(LauncherDir) + "/flags").c_str(), 0777);
                if (!TouchFile(BootFlag)) { Toast("无法写入开机启动标记"); return; }
            } else {
                std::remove(BootFlag);
            }
            Toast(on ? "下次开机将自动启动" : "下次开机将不再自动启动");
        }

        std::string FwText() {
            char b[64];
            std::snprintf(b, sizeof(b), "%u.%u.%u%s", HOSVER_MAJOR(fw), HOSVER_MINOR(fw), HOSVER_MICRO(fw), FwTested() ? "" : " · 未经测试");
            return b;
        }
        bool FwTested() { return HOSVER_MAJOR(fw) >= 21 && HOSVER_MAJOR(fw) <= 23; }
        std::string AmsText() {
            if (ams == 0) { return "未知"; }
            char b[48];
            std::snprintf(b, sizeof(b), "%u.%u.%u", static_cast<u32>((ams >> 56) & 0xFF), static_cast<u32>((ams >> 48) & 0xFF), static_cast<u32>((ams >> 40) & 0xFF));
            return b;
        }

        static std::string Check(bool ok) { return ok ? "已安装" : "未找到"; }

        void Build() {
            pages.clear();
            ReadLauncher();
            const std::string boot_help =
                "是否随主机开机启动：进入 HOME 菜单后，约在开机 30 秒时启动，且需要"
                "其他后台模块留有足够内存。下次开机生效。"
                "关闭此项可彻底停用后台模块。";

            /* ---- Home ---- */
            {
                Page p{ "主页", {} };
                p.rows.push_back({ Kind::Hero, "", nullptr, nullptr, nullptr, "", nullptr, {} });
                p.rows.push_back(Toggle("串流到电脑", [this] { return StreamOn(); }, [this](int) { SetStream(!StreamOn()); },
                    "关闭后，后台模块不再附加游戏（不调试，金手指可正常使用），也不发送数据。"
                    "与悬浮菜单中的开关同步，重启后仍保留设置。"));
                p.rows.push_back(Toggle("开机启动", [this] { return installed && boot; }, [this](int) { SetBoot(!boot); }, boot_help,
                    [this] { return installed ? std::string() : std::string("未安装"); }, [] { return ui::Bad; }));
                p.rows.push_back({ Kind::Tiles, "", nullptr, nullptr, nullptr, "", nullptr, {
                    { "系统固件", [this] { return FwText(); }, [this] { return FwTested() ? ui::Ink : ui::Warn; } },
                    { "Atmosphere", [this] { return AmsText(); }, nullptr },
                    { "启动时可用内存", [this] { return free_mem.empty() ? std::string("-") : free_mem; }, nullptr },
                    { "截图", [this] {
                          char b[32];
                          std::snprintf(b, sizeof(b), "已保存 %zu 张", shot_files);
                          return std::string(b);
                      }, nullptr },
                } });
                pages.push_back(std::move(p));
            }

            /* ---- Stream ---- */
            {
                Page p{ "串流", {} };
                p.rows.push_back(Choice("连接方式", [this] {
                        std::string v = ConnNames[set.conn];
                        if (have_status && RunningConn() != set.conn) { v += " · 重启后生效"; }
                        else if (set.conn == ConnNetwork) { v += "  " + IpString(); }
                        return v;
                    },
                    [this](int d) {
                        set.conn = (set.conn + (d > 0 ? 1 : ConnCount - 1)) % ConnCount;
                        SavedForBoot("切换连接方式");
                    },
                    "选择画面传输方式，一次只能使用一种。USB - 电脑客户端：通过 USB-C 数据线连接，限掌机模式，支持声音且延迟最低。USB - 摄像头（OBS）：主机作为 USB 摄像头，提供最高 1080p60 的 H.264 画面，不含声音。USB - 摄像头（通用）：提供未压缩画面，可供 Windows 相机、浏览器、Discord、Zoom 等使用，支持 720p25 或 432p60。网络：通过 Wi-Fi 或底座 LAN 接口连接，支持底座模式；电脑客户端也需选择网络。建议使用有线网络，或信号良好的 5 GHz Wi-Fi 配合中等画质。更改后需重启主机。",
                    [this] { return (have_status && RunningConn() != set.conn) ? ui::Warn : ui::Ink; }));
                p.rows.push_back(Toggle("游戏音频", [this] { return set.audio; }, [this](int) { set.audio = !set.audio; Saved(); },
                    "将游戏声音传输至电脑客户端（客户端按 M 静音）。与 SysDVR 一样，音频来自主机的系统录制功能。禁止视频录制的游戏需要开启“采集禁用录制游戏的声音”才有声音。摄像头模式不支持声音，因为 Switch 的 USB 摄像头接口无法同时传输音频。",
                    [this] {
                        if (!set.audio || !have_status || st.version < 4) { return std::string(); }
                        switch (st.reserved & 0xFF) {
                            case 1: return std::string("播放中");
                            case 2: return std::string("系统录制不可用");
                            case 3: return std::string("无法启动");
                            default: return std::string();
                        }
                    },
                    [this] { return (st.reserved & 0xFF) >= 2 ? ui::Warn : ui::Good; }));
                p.rows.push_back(Toggle("采集禁用录制游戏的声音", [this] { return sound == sftsound::State::On; },
                    [this](int) {
                        if (sound == sftsound::State::Missing) { Toast("SD 卡中缺少补丁，请重新安装 Switch Frame Tap 压缩包"); return; }
                        const bool on = sound != sftsound::State::On;
                        if (sftsound::Set(on)) { sound_changed = !sound_changed; Toast(on ? "已开启，重启主机生效" : "已关闭，重启主机生效"); }
                        else { Toast("修改失败，请检查 SD 卡"); }
                    },
                    "部分游戏（例如《任天堂明星大乱斗 特别版》）会禁用系统录制，同时导致串流无法获得声音。开启后将启用内置的 dvr-patches（作者 exelix11），允许系统录制在所有游戏中运行以获取音频。默认关闭，重启后生效。若开启后游戏崩溃（崩溃报告为 0100000000000023），请关闭此项。",
                    [this] { return sound == sftsound::State::Missing ? std::string("未安装") : sound_changed ? std::string("重启后生效") : std::string(); },
                    [] { return ui::Warn; }));
                p.rows.push_back(Toggle("串流时关闭主机屏幕", [this] { return set.screen_off; }, [this](int) { set.screen_off = !set.screen_off; Saved(); },
                    "串流时关闭 Switch 自身屏幕，节省在电脑上游玩时的电量。串流停止后立即恢复亮屏，例如关闭客户端、按 HOME 或关闭串流。仅限掌机模式，不影响电视画面。"));
                p.rows.push_back(Action("排除的应用", [this] {
                        char b[32];
                        std::snprintf(b, sizeof(b), confirm_clear ? "按 A 清空" : excluded == 1 ? "1 个应用" : "%zu 个应用", excluded);
                        return std::string(b);
                    },
                    [this](int) {
                        if (excluded == 0) { Toast("没有排除的应用"); return; }
                        if (!confirm_clear) { confirm_clear = true; return; }
                        confirm_clear = false;
                        std::remove(ExclFile);
                        if (have_status) { SftapReload(); }
                        Toast("已清空排除列表");
                    },
                    "部分应用（例如 TiCo 加保护的版本）无法在调试器附加时运行，需要后台模块避开它们。运行此类应用时，在悬浮菜单中关闭“串流当前应用”，之后将不再附加该应用，也不提供串流或截图。此处按 A 可清空排除列表。",
                    [this] { return confirm_clear ? ui::Warn : ui::Dim; }));
                pages.push_back(std::move(p));
            }

            /* ---- Picture ---- */
            {
                Page p{ "画面", {} };
                p.rows.push_back(Choice("画质", [this] { return std::string(QualityNames[set.quality]); },
                    [this](int d) { set.quality = (set.quality + (d > 0 ? 1 : 2)) % 3; Saved(); },
                    "高画质最清晰：在快速运动的 1080p 赛车游戏中，码率约为 80 Mbps，电脑端录像每分钟约占 600 MB。中画质约为一半，低画质约为三分之一，仍有较好的清晰度。静态较多的游戏和 720p 所需带宽更低。若数据线或电脑性能不足导致卡顿，可降低画质。"));
                p.rows.push_back(Choice("关键帧", [this] { return std::string(KeyframeNames[set.keyframe]); },
                    [this](int d) { set.keyframe = (set.keyframe + (d > 0 ? 1 : 3)) % 4; Saved(); },
                    "关键帧包含完整画面，帧间仅传输变化部分。关键帧越频繁，异常后恢复越快；间隔越长，越节省带宽。"));
                p.rows.push_back(Choice("最高分辨率", [this] { return std::string(set.cap720 ? "720p" : "1080p（原生）"); },
                    [this](int) { set.cap720 = !set.cap720; Saved(); },
                    "1080p 传输游戏在底座模式下的画面（掌机模式需配合 ReverseNX-RT）。720p 会缩小画面，带宽需求更低，并可在默认频率下运行。"));
                p.rows.push_back(Toggle("串流时提升内存频率", [this] { return set.mem_boost; },
                    [this](int) { set.mem_boost = !set.mem_boost; Saved(); },
                    "串流时将内存频率提升至 1600 MHz（底座频率），以加快高负载游戏的画面复制，提高默认 CPU/GPU 频率下的串流 FPS，会略微增加耗电。若已有超频工具将内存设得更高，则不作调整。下次串流生效。"));
                p.rows.push_back(Toggle("将其他底座分辨率放大至 1080p", [this] { return set.scale1080; },
                    [this](int) { set.scale1080 = !set.scale1080; Saved(); },
                    "部分游戏在底座模式下的分辨率介于 720p 与 1080p 之间，例如《超级马力欧 奥德赛》的 1600x900。默认会缩小至 720p；开启后改为放大至 1080p，大屏幕上可能更清晰，但会增加带宽需求及一帧延迟。下次串流生效。"));
                pages.push_back(std::move(p));
            }

            /* ---- Screenshots ---- */
            {
                Page p{ "截图", {} };
                p.rows.push_back(Toggle("启用截图快捷键", [this] { return set.shot; }, [this](int) { set.shot = !set.shot; Saved(); },
                    "在游戏中按下组合键，以游戏原始分辨率保存 PNG 截图（底座模式或使用 ReverseNX-RT 时为 1920x1080，掌机模式为 1280x720）。无需打开电脑客户端；保存时串流会短暂停顿。安装 Ultrahand 悬浮菜单后，每次截图均有保存通知。"));
                p.rows.push_back(Choice("按键组合", [this] { return set.shot_combo == ComboCustom ? CustomComboText() : std::string(ComboNames[set.shot_combo]); },
                    [this](int d) { set.shot_combo = (set.shot_combo + (d > 0 ? 1 : ComboCustom)) % (ComboCustom + 1); Saved(); },
                    "同时按下这些按键即可截图。请选择游戏中不会用到的组合，例如较少被游戏使用的 L3 + R3（按下双摇杆）。“自定义”可使用下方录入的组合。"));
                p.rows.push_back(Action("录入自定义组合", [this] {
                        return set.shot_combo == ComboCustom ? std::string("按 A 后按住所需按键") : std::string("用于自定义组合");
                    }, [](int) { g_record_combo = true; },
                    "在此按 A，然后同时按住所需按键两秒，即可录入截图组合。若需加入截图键，请使用下一项设置；在此按下截图键会截取当前应用画面。"));
                p.rows.push_back(Toggle("自定义组合包含截图键", [this] { return set.shot_combo == ComboCustom && set.shot_capture; },
                    [this](int) { set.shot_combo = ComboCustom; set.shot_capture = !set.shot_capture; Saved(); },
                    "将截图键（方形按键）加入组合，例如 ZR + Capture。按下时系统仍会生成相册 JPEG，同时保存本应用的原生分辨率 PNG。也可仅使用截图键，此时每次系统截图都会额外保存 PNG。"));
                p.rows.push_back(Choice("保存位置", [this] { return std::string(set.shot_album ? "系统截图目录" : "Switch Frame Tap 目录"); },
                    [this](int) { set.shot_album = !set.shot_album; Saved(); },
                    "选择截图保存位置。“系统截图目录”：与系统截图存于 SD 卡同一相册目录（Nintendo/Album/年/月/日；emuMMC 使用其自身的 Nintendo 目录），按日期命名，方便在电脑中一起查看。系统相册仅显示任天堂 JPEG，无法显示这些 PNG，但本应用的截图浏览器可以查看。“Switch Frame Tap 目录”：/switch/switch-frame-tap/screenshots。"));
                p.rows.push_back(Action("查看截图", [this] {
                        char b[32];
                        std::snprintf(b, sizeof(b), "已保存 %zu 张", shot_files);
                        return std::string(b);
                    }, [](int) { g_open_gallery = true; },
                    "浏览 SD 卡上的截图：A 全屏查看，左右切换图片，Y 删除。"));
                p.rows.push_back(Info("本次开机截图", [this] {
                        if (!have_status || st.version < 2) { return std::string("-"); }
                        char b[48];
                        std::snprintf(b, sizeof(b), st.shot_fails ? "%u 张（%u 张失败）" : "%u", st.shots, st.shot_fails);
                        return std::string(b);
                    }, [this] { return (have_status && st.version >= 2 && st.shot_fails) ? ui::Warn : ui::Dim; },
                    "本次开机以来保存的截图。每张 1080p PNG 约占 6 MB，采用未压缩存储，完整保留游戏画面像素。"));
                pages.push_back(std::move(p));
            }

            /* ---- Beta ---- */
            {
                Page p{ "实验功能", {} };
                p.rows.push_back(Section("开机时读取，修改后请重启主机"));
                p.rows.push_back(Toggle("串流 HOME 菜单", [this] { return set.home_menu; },
                    [this](int) { set.home_menu = !set.home_menu; SavedForBoot("使设置生效"); },
                    "未显示游戏时，也串流 HOME 菜单和系统界面，包括系统设置、用户选择、控制器、我的主页、相册和 Homebrew Menu。目前游戏外的界面暂无声音。",
                    [this] { return (have_status && RunningHomeMenu() != set.home_menu) ? std::string("重启后生效") : std::string(); },
                    [] { return ui::Warn; }));
                p.rows.push_back(Toggle("串流悬浮界面", [this] { return set.overlays; },
                    [this](int) { set.overlays = !set.overlays; SavedForBoot("使设置生效"); },
                    "在串流游戏画面上叠加主机的悬浮界面，包括音量条、快捷设置（长按 HOME）、电源菜单（长按 POWER）和 Tesla / Ultrahand 菜单。每秒复制 10–20 次，因此流畅度低于游戏，电源菜单也会略显模糊。",
                    [this] { return (have_status && RunningOverlays() != set.overlays) ? std::string("重启后生效") : std::string(); },
                    [] { return ui::Warn; }));
                /* v0.7.18 */
                p.rows.push_back(Section("下次启动应用时生效"));
                p.rows.push_back(Toggle("自制应用使用底座模式（1080p）", [this] { return set.hb_docked; },
                    [this](int) { set.hb_docked = !set.hb_docked; Saved(); },
                    "对按住 R 启动游戏或通过 HOME 菜单前端打开的自制应用报告底座模式，使按模式选择分辨率的应用（例如 SDL2 移植和模拟器）以 1080p 绘制并串流；主机屏幕仍正常显示。固定使用 1280x720 的应用不受影响。从相册打开的自制应用不适用，因为该模式的内存不足以支持 1080p。"));
                /* v0.7.20 */
                p.rows.push_back(Section("立即生效"));
                p.rows.push_back(Toggle("允许客户端控制", [this] { return set.viewer_input; },
                    [this](int) { set.viewer_input = !set.viewer_input; Saved(); },
                    "允许电脑客户端的手柄、键盘和鼠标，以及安卓客户端连接的手柄操作 Switch。客户端打开时，它们会作为一个额外的 Pro 控制器，可通过 USB 或网络在游戏及 HOME 菜单中使用。在电脑客户端左上角“控制器”（或按 G）及安卓客户端设置中选择输入设备和按键映射。若游戏只接受一个控制器，请在“控制器 → 更改握法/顺序”中选择。需要客户端 0.7.20 或更高版本。"));
                pages.push_back(std::move(p));
            }

            /* ---- Extras (v0.7.5: the optional tools, from their authors' GitHub releases) ---- */
            {
                Page p{ "扩展工具", {} };
                p.rows.push_back(Section("从各工具的 GitHub 发布页下载"));
                auto extra = [this, &p](const extras::Tool *t, bool (*has)(), std::function<std::string()> note, std::function<std::string()> blocked, const char *help) {
                    const int idx = static_cast<int>(p.rows.size());
                    p.rows.push_back(Action(t->name, [this, idx, has, note, blocked] {
                            if (confirm_extra == idx) { return std::string(has() ? "再按 A 更新" : "再按 A 安装"); }
                            const std::string b = blocked();
                            if (!b.empty()) { return b; }
                            if (!has()) { return std::string("未安装"); }
                            const std::string n = note();
                            return n.empty() ? std::string("已安装") : n;
                        },
                        [this, idx, t, blocked](int) {
                            const std::string b = blocked();
                            if (!b.empty()) { Toast(b + " · 只能安装一个超频工具，请先移除现有工具"); return; }
                            if (confirm_extra != idx) { confirm_extra = idx; return; }
                            confirm_extra = -1;
                            g_pending_install = t;
                        }, help,
                        [this, idx, has, note, blocked] {
                            if (confirm_extra == idx) { return ui::Warn; }
                            if (!blocked().empty() || !has()) { return ui::Dim; }
                            return note().empty() ? ui::Good : ui::Warn;
                        }));
                };
                auto none = [] { return std::string(); };
                extra(&extras::Ultrahand, extras::HasUltrahand, none, none,
                      "悬浮菜单（L + 方向键下 + 右摇杆）：Switch Frame Tap 悬浮界面与截图保存通知需要此工具。包含 nx-ovlloader；若已安装 Tesla Menu，会将其替换。更新会保留 Ultrahand 设置。来源：github.com/ppkantorski/Ultrahand-Overlay。");
                extra(&extras::SaltyNX, extras::HasSaltyNX, none, none,
                      "ReverseNX-RT 所需的游戏插件加载器。可能与部分游戏 MOD 冲突（测试中使用 Skyline 的大乱斗 MOD 曾发生崩溃），若游戏崩溃请停用 SaltyNX。来源：github.com/masagrator/SaltyNX。");
                extra(&extras::ReverseNX, extras::HasReverseNX, none, none,
                      "让游戏在掌机模式下渲染底座 1080p 画面，从而支持 USB 1080p 串流。在悬浮菜单中打开 ReverseNX-RT 设置。需要 SaltyNX，部分游戏不支持。来源：github.com/masagrator/ReverseNX-RT。");
                extra(&extras::SysClk, extras::HasSysClk, none,
                      [] { return extras::HasHorizonOC() ? std::string("已安装 Horizon OC") : std::string(); },
                      "通过超频使 1080p 串流达到 60 FPS（建议 CPU 1785 MHz，在其悬浮菜单中设置）。sys-clk 与 Horizon OC 二选一，不可同时安装。来源：github.com/retronx-team/sys-clk。");
                extra(&extras::HorizonOC, extras::HasHorizonOC,
                      [] { return extras::HekateMissesHocKip() ? std::string("已安装，Hekate 需配置对应 kip") : std::string(); },
                      [] { return extras::HasSysClk() ? std::string("已安装 sys-clk") : std::string(); },
                      "提供比 sys-clk 更大调节范围的 CPU、GPU、RAM 超频工具。作者提示 RAM 超频可能损坏 NAND 或 SD 卡数据，请先备份。安装会替换 atmosphere/exosphere.bin；使用 Hekate 启动时，启动项需设置 kip1=atmosphere/kips/hoc.kip 和 secmon=atmosphere/exosphere.bin。更新会替换包含设置的 hoc.kip，之后需在其悬浮菜单中重新保存设置。sys-clk 与 Horizon OC 二选一。来源：github.com/Horizon-OC/Horizon-OC。");
                pages.push_back(std::move(p));
            }

            /* ---- Setup ---- */
            {
                Page p{ "安装检查", {} };
                p.rows.push_back(Section("SD 卡安装状态"));
                auto check = [&p](const char *label, const char *path, bool needed, const char *help) {
                    p.rows.push_back(Info(label, [path] { return Check(FileExists(path)); },
                        [path, needed] { return FileExists(path) ? ui::Good : (needed ? ui::Bad : ui::Dim); }, help));
                };
                check("后台模块", ModuleNsp, true, "atmosphere/contents/0100000000000C20/exefs.nsp · 负责串流的后台模块。");
                check("内存辅助模块", "sdmc:/atmosphere/contents/0100000000000C21/exefs.nsp", true,
                      "atmosphere/contents/0100000000000C21/exefs.nsp · 开机完成后启动后台模块，并在客户端连接时提供串流所需内存；未串流时释放内存，供游戏说明书、新闻和 eShop 等使用。");
                check("悬浮菜单", "sdmc:/switch/.overlays/switch-frame-tap.ovl", false,
                      "switch/.overlays/switch-frame-tap.ovl · 在游戏内开关串流、设置实验功能及掌机/底座模式。需要 Tesla 悬浮菜单加载器，可在“扩展工具”中安装 Ultrahand。");
                p.rows.push_back(Info("系统内存", [this] { return free_mem.empty() ? std::string("本次开机尚未测量") : free_mem + "（启动前可用）"; }, nullptr,
                    "所有后台模块与系统共享少量系统内存。剩余内存过低可能导致游戏启动黑屏；Switch Frame Tap 会在内存不足时放弃启动，主机仍可正常开机。关闭不使用的后台模块可释放内存。" +
                    (others.empty() ? std::string() : std::string(" 开机运行的后台模块（按内存占用降序）：\n") + others)));
                p.rows.push_back(Info("系统固件", [this] { return FwText(); }, [this] { return FwTested() ? ui::Dim : ui::Warn; },
                    "此版本已在固件 21.x 至 23.x 上测试。其他固件默认不启用串流，需开启“允许未经测试的固件”。"));
                p.rows.push_back(Info("Atmosphere", [this] { return AmsText(); }, nullptr, "本应用依赖的 Atmosphère 自定义固件。"));
                p.rows.push_back(Info("安装类型", [this] { return std::string(test_install ? "测试版（applet-mitm.armed）" : "正式版"); },
                    [this] { return test_install ? ui::Warn : ui::Dim; },
                    "测试安装使用 sdmc:/applet-mitm.armed 控制，并忽略 config.ini。正常使用时请删除该测试文件。"));
                p.rows.push_back(Section("高级设置"));
                p.rows.push_back(Choice("开机启动延迟", [this] { char b[32]; std::snprintf(b, sizeof(b), "%d s", DelaySeconds[set.delay]); return std::string(b); },
                    [this](int d) { set.delay = (set.delay + (d > 0 ? 1 : 4)) % 5; Saved(); },
                    "开机后等待指定秒数，再开始寻找游戏。下次开机生效。"));
                p.rows.push_back(Toggle("允许未经测试的固件", [this] { return set.any_fw; }, [this](int) { set.any_fw = !set.any_fw; Saved(); },
                    "允许在未经此版本测试的固件上串流。可能正常运行，也可能导致主机死机并需要强制关机。下次开机生效。", nullptr, nullptr));
                pages.push_back(std::move(p));
            }

            /* ---- Updates (v0.7.14) ---- */
            {
                Page p{ "更新", {} };
                p.rows.push_back(Section("来自本项目的 GitHub 发布页"));
                p.rows.push_back(Action("检查更新", [this] {
                        switch (upd) {
                            case Upd::Latest:    return std::string("已是最新版本");
                            case Upd::Found:     return upd_rel.tag + " 可更新";
                            case Upd::Installed: return upd_rel.tag + " 已安装";
                            case Upd::Failed:    return upd_msg;
                            default:             return std::string("尚未检查");
                        }
                    }, [](int) { g_pending_update = 1; },
                    "从 GitHub 检查 Switch Frame Tap 最新版本。默认仅正式版，开启下方“接收测试版更新”后也会包含测试版。主机需连接网络。",
                    [this] { return upd == Upd::Found ? ui::Good : upd == Upd::Failed ? ui::Warn : upd == Upd::Installed ? ui::Acc : ui::Dim; }));
                p.rows.push_back(Action("安装更新", [this] {
                        if (upd == Upd::Installed) { return std::string("请重启主机完成更新"); }
                        if (upd != Upd::Found) { return std::string("-"); }
                        return confirm_update ? "再按 A 安装 " + upd_rel.tag : upd_rel.tag;
                    }, [this](int) {
                        if (upd == Upd::Installed) { Toast("重启主机：长按 POWER → 电源选项 → 重新启动"); return; }
                        if (upd != Upd::Found) { Toast("请先检查更新"); return; }
                        if (!confirm_update) { confirm_update = true; return; }
                        confirm_update = false;
                        g_pending_update = 2;
                    },
                    "下载新版 Switch 安装包并解压至 SD 卡，效果与手动更新相同：更新后台模块、内存辅助模块、悬浮菜单和本应用，保留现有设置。重启主机后使用新版本。",
                    [this] { return confirm_update ? ui::Warn : upd == Upd::Found ? ui::Good : upd == Upd::Installed ? ui::Acc : ui::Dim; }));
                p.rows.push_back(Toggle("接收测试版更新", [this] { return set.update_betas; },
                    [this](int) {
                        set.update_betas = !set.update_betas;
                        if (!SaveSettings(set)) { Toast("无法写入 config.ini"); return; }
                        if (upd == Upd::Latest || upd == Upd::Found) { upd = Upd::Unknown; }
                        Toast(set.update_betas ? "已包含测试版，请重新检查" : "仅接收正式版更新");
                    },
                    "测试版会提前提供新功能，但测试覆盖较少。关闭此项可仅接收正式版更新。"));
                p.rows.push_back(Info("当前版本", [] { return std::string(AppVersion); }, nullptr,
                    "本应用的版本。后台模块与悬浮菜单来自同一安装包，版本应保持一致。"));
                pages.push_back(std::move(p));
            }

            /* ---- About ---- */
            {
                Page p{ "关于", {} };
                p.rows.push_back(Info("版本", [] { return std::string(AppVersion); }, nullptr,
                    "Switch Frame Tap · 通过 USB 或网络向电脑串流原生 1080p60 游戏画面。"));
                p.rows.push_back(Info("源代码", [] { return std::string("github.com/TomasUribe/switch-frame-tap"); }, nullptr,
                    "免费开源（GPL-2.0）。问题反馈与建议请提交至 GitHub Issues 或 GBAtemp 讨论帖。"));
                p.rows.push_back(Info("故障恢复", [] { return std::string("开机时按住音量加键"); }, nullptr,
                    "Atmosphère 启动时按住音量加键可跳过所有后台模块。也可在“主页”关闭“开机启动”，或通过电脑删除 atmosphere/contents/0100000000000C20 和 0100000000000C21。"));
                p.rows.push_back(Info("致谢", [] { return std::string("内置与可选工具"); }, nullptr,
                    "感谢 exelix11 的 dvr-patches（采集禁用录制游戏的声音），masagrator 的 SaltyNX 和 ReverseNX-RT，ppkantorski 的 Ultrahand，RetroNX 团队的 sys-clk，Horizon OC 团队，以及 SciresM 和贡献者开发的 Atmosphère。"));
                pages.push_back(std::move(p));
            }

            for (auto &pg : pages) { pg.sel = FirstSelectable(pg); }
            page = 0;
        }

        static int FirstSelectable(const Page &p) {
            for (size_t i = 0; i < p.rows.size(); ++i) { if (Selectable(p.rows[i])) { return static_cast<int>(i); } }
            return -1;
        }

        Page &Cur() { return pages[page]; }
        const Row *CurRow() { Page &p = Cur(); return (in_content && p.sel >= 0) ? &p.rows[p.sel] : nullptr; }

        void Unconfirm() { confirm_clear = false; confirm_extra = -1; confirm_update = false; }

        void GoPage(int i) {
            Unconfirm();
            page = std::clamp(i, 0, static_cast<int>(pages.size()) - 1);
            if (Cur().sel < 0) { in_content = false; }
        }

        void Move(int d) {
            Unconfirm();
            if (!in_content) { GoPage(page + d); return; }
            Page &p = Cur();
            int i = p.sel;
            do { i += d; } while (i >= 0 && i < static_cast<int>(p.rows.size()) && !Selectable(p.rows[i]));
            if (i >= 0 && i < static_cast<int>(p.rows.size())) { p.sel = i; }
        }

        void Activate(int d) {
            Page &p = Cur();
            if (p.sel < 0) { return; }
            Row &r = p.rows[p.sel];
            if (r.change) { r.change(d); Refresh(); }
        }
    };

    /* ---- drawing --------------------------------------------------------- */

    constexpr int SideW = 304;                 /* the sidebar */
    constexpr int CX = 336, CW = 912;          /* the page's column */
    constexpr int ViewTop = 92, ViewBottom = 650, BarY = 662;
    constexpr int CardH = 72, Gap = 10, Pad = 24;
    constexpr int NavY = 116, NavH = 50, NavStep = 56;

    /* where a tap lands: drawn this frame */
    enum class HitKind { Nav, Row, Left, Right, Switch };
    struct Hit { SDL_Rect r; HitKind what; int index; };
    std::vector<Hit> g_hits;
    void AddHit(int x, int y, int w, int h, HitKind k, int i) { g_hits.push_back({ { x, y, w, h }, k, i }); }

    int RowHeight(App &a, const Page &p, int i) {
        const Row &r = p.rows[i];
        switch (r.kind) {
            case Kind::Section: return 46;
            case Kind::Hero:    return 168 + Gap;
            case Kind::Tiles:   return 88 + Gap;
            default: break;
        }
        int h = CardH;
        if (a.in_content && p.sel == i && !r.help.empty()) { h = 62 + ui::TextH(r.help, ui::Body, CW - 2 * Pad) + 20; }
        return h + Gap;
    }

    void DrawHero(App &a, int x, int y, int w) {
        const int h = 168;
        ui::RRect(x, y, w, h, 20, ui::Surf);
        const SDL_Color c = a.StreamColor();
        ui::Disc(x + 40, y + 54, 16, ui::Alpha(c, 46));
        ui::Disc(x + 40, y + 54, 8, c);
        ui::Text(a.StateTitle(), x + 70, y + 30, ui::Big, ui::Ink);
        const bool streaming = a.have_status && a.st.state == 4;
        ui::Text(a.StateDetail(), x + 28, y + 88, ui::Body, ui::Dim, 0, w - 56 - (streaming ? 170 : 0));
        int cx = x + 28;
        const int cy = y + h - 46;
        cx += ui::Chip(a.ConnShort(), cx, cy, ui::Acc) + 8;
        if (a.RunningHomeMenu()) { cx += ui::Chip("HOME 菜单", cx, cy, ui::Good) + 8; }
        if (a.RunningOverlays()) { cx += ui::Chip("悬浮界面", cx, cy, ui::Good) + 8; }
        if (!a.StreamOn() && a.installed) { cx += ui::Chip("串流已关闭", cx, cy, ui::Faint) + 8; }
        if (a.upd == App::Upd::Found) { ui::Chip("更新 " + a.upd_rel.tag + " 可用", cx, cy, ui::Warn); }
        if (streaming) {
            char b[32];
            std::snprintf(b, sizeof(b), "%u.%u", a.st.fps_x10 / 10, a.st.fps_x10 % 10);
            ui::Text(b, x + w - 30, y + 26, ui::Big, ui::Ink, 2);
            ui::Text("FPS", x + w - 30, y + 76, ui::Small, ui::Dim, 2);
            if (a.st.game_fps_x10 != 0) {
                std::snprintf(b, sizeof(b), "游戏 %u.%u", a.st.game_fps_x10 / 10, a.st.game_fps_x10 % 10);
                ui::Text(b, x + w - 30, y + 100, ui::Small, ui::Faint, 2);
            }
        }
    }

    void DrawTiles(const Row &r, int x, int y, int w) {
        const int n = static_cast<int>(r.tiles.size());
        if (n == 0) { return; }
        const int tw = (w - (n - 1) * 12) / n;
        for (int i = 0; i < n; ++i) {
            const Tile &t = r.tiles[i];
            const int tx = x + i * (tw + 12);
            ui::RRect(tx, y, tw, 88, 16, ui::Surf);
            ui::Text(t.label, tx + 20, y + 16, ui::Small, ui::Faint);
            ui::Text(t.value ? t.value() : std::string(), tx + 20, y + 44, ui::Label, t.color ? t.color() : ui::Ink);
        }
    }

    void DrawCard(App &a, Page &p, int i, int x, int y, int w, int h) {
        const Row &r = p.rows[i];
        const bool focus = a.in_content && p.sel == i;
        AddHit(x, y, w, h, HitKind::Row, i);
        if (focus) { ui::RRectRing(x - 3, y - 3, w + 6, h + 6, 19, 3, ui::Acc, ui::Surf2); }
        else { ui::RRect(x, y, w, h, 16, ui::Surf); }
        ui::Text(r.label, x + Pad, y + 22, ui::Label, ui::Ink);
        const int xr = x + w - Pad, cy = y + CardH / 2;
        const std::string v = r.value ? r.value() : std::string();
        switch (r.kind) {
            case Kind::Toggle: {
                ui::Toggle(xr, cy, r.on && r.on());
                AddHit(xr - 76, y, 76 + Pad, CardH, HitKind::Switch, i);
                if (!v.empty()) { ui::Text(v, xr - 58 - 18, y + 25, ui::Body, r.color ? r.color() : ui::Dim, 2); }
                break;
            }
            case Kind::Choice: {
                const SDL_Color cc = focus ? ui::Acc : ui::Faint;
                const int vw = ui::TextW(v, ui::Body);
                ui::Chevron(xr - 6, cy, 16, +1, cc);
                ui::Text(v, xr - 28, y + 25, ui::Body, r.color ? r.color() : ui::Ink, 2);
                ui::Chevron(xr - 28 - vw - 22, cy, 16, -1, cc);
                AddHit(xr - 28 - vw - 44, y, 44, CardH, HitKind::Left, i);
                AddHit(xr - 26, y, 26 + Pad, CardH, HitKind::Right, i);
                break;
            }
            case Kind::Action:
                ui::Chevron(xr - 6, cy, 16, +1, focus ? ui::Acc : ui::Faint);
                ui::Text(v, xr - 28, y + 25, ui::Body, r.color ? r.color() : ui::Dim, 2);
                break;
            default:
                ui::Text(v, xr, y + 25, ui::Body, r.color ? r.color() : ui::Dim, 2);
                break;
        }
        if (focus && !r.help.empty()) { ui::Text(r.help, x + Pad, y + 62, ui::Body, ui::Dim, 0, w - 2 * Pad); }
    }

    void DrawSidebar(App &a) {
        ui::Fill(0, 0, SideW, ui::H, ui::Surf);
        ui::Logo(26, 30, 40);
        ui::Text("Switch Frame Tap", 100, 26, ui::Label, ui::Ink);
        ui::Text(std::string("v") + AppVersion, 100, 54, ui::Small, ui::Faint);
        for (int i = 0; i < static_cast<int>(a.pages.size()); ++i) {
            const int y = NavY + i * NavStep;
            const bool cur = i == a.page;
            if (cur) {
                if (!a.in_content) { ui::RRectRing(14, y, SideW - 28, NavH, 12, 3, ui::Acc, ui::Surf3); }
                else { ui::RRect(14, y, SideW - 28, NavH, 12, ui::Surf3); }
            }
            ui::Text(a.pages[i].name, 36, y + 12, ui::Label, cur ? ui::Ink : ui::Dim);
            AddHit(14, y, SideW - 28, NavH, HitKind::Nav, i);
        }
        /* the stream, at a glance */
        const SDL_Color c = a.StreamColor();
        ui::Disc(36, ui::H - 38, 6, c);
        ui::Text(a.StateTitle(), 52, ui::H - 50, ui::Small, ui::Dim);
    }

    /* button hints, right to left: glyph, the letter the PC preview draws, label */
    struct Hint { const char *glyph; char letter; const char *label; };
    void DrawHints(std::initializer_list<Hint> list, int x0) {
        int x = ui::W - 32;
        for (const Hint &h : list) {
            x -= ui::TextW(h.label, ui::Body);
            ui::Text(h.label, x, BarY + 17, ui::Body, ui::Dim);
            x -= 8;
            int gw = 26;
            if (ui::g_glyphs != nullptr) { if (const ui::CachedText *t = ui::Render(h.glyph, 0, ui::Ink, 0, true)) { gw = t->w; } }
            x -= gw;
            ui::Button(h.glyph, h.letter, x, BarY + 29);
            x -= 30;
        }
        ui::Fill(x0, BarY, ui::W - x0, 1, ui::Line);
    }

    void DrawToast(App &a, int cx) {
        if (a.toast_frames == 0) { return; }
        --a.toast_frames;
        const Uint8 al = a.toast_frames < 20 ? static_cast<Uint8>(a.toast_frames * 255 / 20) : 255;
        const int tw = std::min(CW, ui::TextW(a.toast, ui::Body) + 44); /* Keep translated notices inside the content column. */
        const int th = ui::TextH(a.toast, ui::Body, tw - 44) + 24; /* Wrapped notice height, including vertical padding. */
        ui::RRect(cx - tw / 2, 640 - th, tw, th, 18, ui::Alpha(ui::Surf3, al));
        ui::Text(a.toast, cx, 652 - th, ui::Body, ui::Alpha(ui::Ink, al), 1, tw - 44);
    }

    void Draw(App &a) {
        g_hits.clear();
        SDL_SetRenderDrawColor(ui::g_ren, ui::Bg.r, ui::Bg.g, ui::Bg.b, 255);
        SDL_RenderClear(ui::g_ren);
        DrawSidebar(a);

        Page &p = a.Cur();
        ui::Text(p.name, CX, 26, ui::Title, ui::Ink);

        /* the page's rows, scrolled to keep the selection in view */
        std::vector<int> ys(p.rows.size());
        int total = 0;
        for (size_t i = 0; i < p.rows.size(); ++i) { ys[i] = total; total += RowHeight(a, p, static_cast<int>(i)); }
        const int view = ViewBottom - ViewTop;
        const int max_scroll = std::max(0, total - view);
        const int follow = a.in_content ? p.sel : -1;
        if (follow != p.seen_sel) {
            p.seen_sel = follow;
            if (follow >= 0) {
                int top = ys[follow];
                if (follow > 0 && p.rows[follow - 1].kind == Kind::Section) { top = ys[follow - 1]; }
                const int bot = ys[follow] + RowHeight(a, p, follow);
                if (top < p.target) { p.target = static_cast<float>(top); }
                if (bot - p.target > view) { p.target = static_cast<float>(bot - view); }
            } else {
                p.target = 0;
            }
        }
        p.target = std::clamp(p.target, 0.0f, static_cast<float>(max_scroll));
        p.scroll += (p.target - p.scroll) * 0.35f;
        if (std::fabs(p.target - p.scroll) < 0.5f) { p.scroll = p.target; }

        SDL_Rect clip = { CX - 8, ViewTop - 6, CW + 16, view + 12 };
        SDL_RenderSetClipRect(ui::g_ren, &clip);
        for (size_t i = 0; i < p.rows.size(); ++i) {
            const int idx = static_cast<int>(i);
            const int y = ViewTop + ys[i] - static_cast<int>(p.scroll);
            const int h = RowHeight(a, p, idx) - (p.rows[i].kind == Kind::Section ? 0 : Gap);
            if (y + h < ViewTop - 6 || y > ViewBottom + 6) { continue; }
            const Row &r = p.rows[i];
            switch (r.kind) {
                case Kind::Section: ui::Text(r.label, CX + 4, y + 14, ui::Small, ui::Faint); break;
                case Kind::Hero:    DrawHero(a, CX, y, CW); break;
                case Kind::Tiles:   DrawTiles(r, CX, y, CW); break;
                default:            DrawCard(a, p, idx, CX, y, CW, h); break;
            }
        }
        SDL_RenderSetClipRect(ui::g_ren, nullptr);
        if (total > view) {   /* a thin scroll bar */
            const int th = std::max(40, view * view / total);
            const int ty = ViewTop + static_cast<int>((view - th) * (p.scroll / std::max(1, max_scroll)));
            ui::RRect(ui::W - 18, ViewTop, 4, view, 2, ui::Line);
            ui::RRect(ui::W - 18, ty, 4, th, 2, ui::Surf3);
        }

        const Row *cur = a.CurRow();
        if (a.in_content) {
            const char *act = cur == nullptr || !cur->change ? nullptr : cur->kind == Kind::Action ? "打开" : "更改";
            if (act != nullptr) { DrawHints({ { "", '+', "退出" }, { "", 'B', "返回" }, { "", 'A', act } }, SideW); }
            else { DrawHints({ { "", '+', "退出" }, { "", 'B', "返回" } }, SideW); }
        } else {
            DrawHints({ { "", '+', "退出" }, { "", 'A', "打开" } }, SideW);
        }
        DrawToast(a, CX + CW / 2);
        SDL_RenderPresent(ui::g_ren);
    }

    /* ---- the screenshot gallery --------------------------------------------- */

    struct Gallery {
        std::vector<std::string> files;
        int sel = 0, scroll = 0;
        bool full = false;
        SDL_Texture *tex = nullptr;
        int tex_idx = -1, tw = 0, th = 0;
        long long bytes = 0;
        u32 settle = 0;             /* frames since the selection moved: load when it rests */
        int confirm = -1;           /* Y pressed once on this index */
        std::string note;

        void Open() { files = ListShots(); sel = 0; scroll = 0; full = false; confirm = -1; note.clear(); Drop(); settle = 6; }
        void Drop() { if (tex) { SDL_DestroyTexture(tex); tex = nullptr; } tex_idx = -1; }
        void Select(int i) {
            if (files.empty()) { return; }
            i = std::clamp(i, 0, static_cast<int>(files.size()) - 1);
            if (i != sel) { sel = i; settle = 0; confirm = -1; note.clear(); }
        }
        void Load() {
            if (files.empty() || tex_idx == sel || (settle < 6 && !full)) { return; }
            Drop();
            const std::string &path = files[sel];
            struct stat st;
            bytes = stat(path.c_str(), &st) == 0 ? st.st_size : 0;
            if (SDL_Surface *surf = IMG_Load(path.c_str())) {
                tex = SDL_CreateTextureFromSurface(ui::g_ren, surf);
                tw = surf->w; th = surf->h;
                SDL_FreeSurface(surf);
            } else {
                tw = th = 0;
            }
            tex_idx = sel;
        }
        void Delete() {
            if (files.empty()) { return; }
            if (confirm != sel) { confirm = sel; note = "再按一次 Y 删除这张截图"; return; }
            const std::string &path = files[sel];
            note = std::remove(path.c_str()) == 0 ? "已删除" : "删除失败";
            files.erase(files.begin() + sel);
            if (sel >= static_cast<int>(files.size()) && sel > 0) { --sel; }
            confirm = -1;
            full = false;
            Drop();
            settle = 0;
        }
    };

    Gallery g_gal;

    constexpr int GalRowH = 58, GalListX = 32, GalListW = 440, GalTop = 92, GalBottom = 650;
    constexpr int PrevX = 500, PrevW = 748;

    void DrawImageFit(SDL_Texture *tex, int tw, int th, int x, int y, int w, int h) {
        if (tex == nullptr || tw == 0 || th == 0) { return; }
        int dw = w, dh = w * th / tw;
        if (dh > h) { dh = h; dw = h * tw / th; }
        SDL_Rect r = { x + (w - dw) / 2, y + (h - dh) / 2, dw, dh };
        SDL_RenderCopy(ui::g_ren, tex, nullptr, &r);
    }

    /* a screen of its own: the page title, the bar */
    void ScreenTop(const char *title, const std::string &count = "") {
        SDL_SetRenderDrawColor(ui::g_ren, ui::Bg.r, ui::Bg.g, ui::Bg.b, 255);
        SDL_RenderClear(ui::g_ren);
        ui::Logo(32, 28, 36);
        ui::Text(title, 108, 24, ui::Title, ui::Ink);
        if (!count.empty()) { ui::Chip(count, ui::W - 32, 30, ui::Dim, 2); }
    }

    void DrawGallery(Gallery &g) {
        g.Load();
        ++g.settle;
        if (g.full && g.tex) {
            SDL_SetRenderDrawColor(ui::g_ren, 0, 0, 0, 255);
            SDL_RenderClear(ui::g_ren);
            DrawImageFit(g.tex, g.tw, g.th, 0, 0, ui::W, ui::H);
            ui::Fill(0, BarY, ui::W, ui::H - BarY, ui::Alpha(ui::Bg, 200));
            char b[96];
            std::snprintf(b, sizeof(b), "%s   （%d / %zu）", ShotLabel(g.files[g.sel]).c_str(), g.sel + 1, g.files.size());
            ui::Text(b, 32, BarY + 17, ui::Body, ui::Ink);
            DrawHints({ { "", 'B', "返回" }, { "", 'Y', "删除" }, { "", 'R', "下一张" }, { "", 'L', "上一张" } }, 0);
            if (!g.note.empty()) { ui::Text(g.note, ui::W / 2, 600, ui::Body, ui::Warn, 1); }
            SDL_RenderPresent(ui::g_ren);
            return;
        }
        char cnt[48];
        std::snprintf(cnt, sizeof(cnt), g.files.size() == 1 ? "%zu 张截图" : "%zu 张截图", g.files.size());
        ScreenTop("截图", cnt);
        if (g.files.empty()) {
            ui::RRect(240, 200, 800, 220, 24, ui::Surf);
            ui::Text("暂无截图", ui::W / 2, 250, ui::Title, ui::Ink, 1);
            ui::Text("启用截图快捷键后，在游戏中按下对应组合即可截图。", ui::W / 2, 310, ui::Body, ui::Dim, 1);
            DrawHints({ { "", 'B', "返回" } }, 0);
            if (!g.note.empty()) { ui::Text(g.note, 32, BarY + 17, ui::Body, ui::Warn); }
            SDL_RenderPresent(ui::g_ren);
            return;
        }
        /* the list */
        const int view = GalBottom - GalTop;
        if (g.sel * GalRowH - g.scroll < 0) { g.scroll = g.sel * GalRowH; }
        if (g.sel * GalRowH + GalRowH - g.scroll > view) { g.scroll = g.sel * GalRowH + GalRowH - view; }
        SDL_Rect clip = { GalListX - 4, GalTop - 4, GalListW + 8, view + 8 };
        SDL_RenderSetClipRect(ui::g_ren, &clip);
        for (int i = 0; i < static_cast<int>(g.files.size()); ++i) {
            const int y = GalTop + i * GalRowH - g.scroll;
            if (y + GalRowH < GalTop || y > GalBottom) { continue; }
            if (i == g.sel) { ui::RRectRing(GalListX, y, GalListW, GalRowH - 8, 14, 3, ui::Acc, ui::Surf2); }
            else { ui::RRect(GalListX, y, GalListW, GalRowH - 8, 14, ui::Surf); }
            ui::Text(ShotLabel(g.files[i]), GalListX + 20, y + 12, ui::Body, i == g.sel ? ui::Ink : ui::Dim);
        }
        SDL_RenderSetClipRect(ui::g_ren, nullptr);
        /* the preview */
        ui::RRect(PrevX, GalTop, PrevW, view - 8, 20, ui::Surf);
        if (g.tex_idx == g.sel && g.tex) {
            DrawImageFit(g.tex, g.tw, g.th, PrevX + 20, GalTop + 20, PrevW - 40, 396);
            char info[96];
            std::snprintf(info, sizeof(info), "%d x %d    %.1f MB", g.tw, g.th, g.bytes / 1048576.0);
            ui::Text(info, PrevX + 24, GalTop + 432, ui::Label, ui::Ink);
            ui::Text(g.files[g.sel].substr(g.files[g.sel].rfind("sdmc:", 0) == 0 ? 5 : 0), PrevX + 24, GalTop + 468, ui::Small, ui::Faint, 0, PrevW - 48);
        } else if (g.tex_idx == g.sel) {
            ui::Text("无法打开此文件。", PrevX + PrevW / 2, GalTop + 220, ui::Body, ui::Warn, 1);
        } else {
            ui::Text("加载中…", PrevX + PrevW / 2, GalTop + 220, ui::Body, ui::Dim, 1);
        }
        DrawHints({ { "", 'B', "返回" }, { "", 'Y', "删除" }, { "", 'A', "查看" } }, 0);
        if (!g.note.empty()) { ui::Text(g.note, 32, BarY + 17, ui::Body, ui::Warn); }
        SDL_RenderPresent(ui::g_ren);
    }

    /* false: leave the gallery */
    bool GalleryInput(Gallery &g, u64 down, u64 held, u32 &hold) {
        if (down & HidNpadButton_B) {
            if (g.full) { g.full = false; return true; }
            g.Drop();
            return false;
        }
        if (down & HidNpadButton_Y) { g.Delete(); return true; }
        if (g.full) {
            if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_L)) { g.Select(g.sel - 1); }
            if (down & (HidNpadButton_Right | HidNpadButton_StickLRight | HidNpadButton_R)) { g.Select(g.sel + 1); }
            return true;
        }
        if (down & HidNpadButton_A) { if (!g.files.empty()) { g.full = true; } return true; }
        const u64 up_mask = HidNpadButton_Up | HidNpadButton_StickLUp;
        const u64 dn_mask = HidNpadButton_Down | HidNpadButton_StickLDown;
        if (down & up_mask) { g.Select(g.sel - 1); hold = 0; }
        else if (down & dn_mask) { g.Select(g.sel + 1); hold = 0; }
        else if (held & (up_mask | dn_mask)) {
            if (++hold > 24 && hold % 6 == 0) { g.Select(g.sel + ((held & up_mask) ? -1 : 1)); }
        } else { hold = 0; }
        return true;
    }

    /* touch in the gallery: tap a row to select it, tap it again (or the
     * preview) to view it full screen, tap a full-screen picture to go back */
    void GalleryTouch(Gallery &g, int tx, int ty) {
        if (g.full) { g.full = false; return; }
        if (tx >= GalListX && tx < GalListX + GalListW && ty >= GalTop && ty < GalBottom) {
            const int i = (ty - GalTop + g.scroll) / GalRowH;
            if (i >= 0 && i < static_cast<int>(g.files.size())) {
                if (i == g.sel) { g.full = true; } else { g.Select(i); }
            }
        } else if (tx >= PrevX && ty >= GalTop && ty < GalBottom && !g.files.empty()) {
            g.full = true;
        }
    }

    /* a progress screen: a card in the middle with a bar */
    void DrawProgressCard(const char *title, const std::string &line1, const std::string &line2, double frac, const char *foot) {
        ScreenTop(title);
        ui::RRect(240, 170, 800, 320, 24, ui::Surf);
        ui::Text(line1, ui::W / 2, 218, ui::Title, ui::Ink, 1);
        ui::Text(line2.empty() ? std::string(" ") : line2, ui::W / 2, 280, ui::Label, ui::Acc, 1);
        const int bx = 320, bw = 640, by = 352, bh = 14;
        ui::RRect(bx, by, bw, bh, bh / 2, ui::Surf3);
        if (frac > 0.0) { ui::RRect(bx, by, std::max(bh, static_cast<int>(bw * (frac > 1.0 ? 1.0 : frac))), bh, bh / 2, ui::Acc); }
        ui::Text(foot, ui::W / 2, 400, ui::Body, ui::Dim, 1, 700);
    }

}

namespace {

    /* v0.7.11: hold the buttons for the custom screenshot combo: the same set
     * held for two seconds is recorded; touch cancels, 20 s gives up */
    void RecordCombo(App &app, PadState &pad) {
        auto draw = [](const char *line1, const std::string &names, double frac) {
            DrawProgressCard("截图按键组合", line1, names.empty() ? std::string("-") : names, frac,
                             "截图键需单独设置（此处按下会截取当前应用画面）。");
            ui::Fill(0, BarY, ui::W, 1, ui::Line);
            ui::Text("触摸屏幕取消", ui::W - 32, BarY + 17, ui::Body, ui::Dim, 2);
            SDL_RenderPresent(ui::g_ren);
        };
        auto names_of = [](uint32_t m) {
            char b[96];
            sft::shotbuttons::Format(m, false, b, sizeof(b));
            std::string t = b;
            for (size_t i = 0; (i = t.find('+', i)) != std::string::npos; i += 3) { t.replace(i, 1, " + "); }
            return t;
        };
        uint32_t last = 0, steady = 0;
        bool released = false, touch_free = false;   /* the tap that opened this must lift first */
        for (u32 frame = 0; frame < 60 * 20 && appletMainLoop(); ++frame) {
            padUpdate(&pad);
            HidTouchScreenState ts = {};
            const bool touched = hidGetTouchScreenStates(&ts, 1) && ts.count > 0;
            if (!touched) { touch_free = true; } else if (touch_free) { app.Toast("未更改"); return; }
            const uint32_t held = static_cast<uint32_t>(padGetButtons(&pad)) & sft::shotbuttons::AllButtons;
            if (!released) {                      /* the A that opened this first */
                if (held == 0) { released = true; }
                draw("请松开所有按键…", "", 0.0);
                continue;
            }
            if (held != last) { last = held; steady = 0; } else if (held != 0) { ++steady; }
            if (held != 0 && steady >= 120) {
                app.set.shot_mask = held;
                app.set.shot_combo = ComboCustom;
                app.Saved();
                app.Toast("截图按键组合：" + app.CustomComboText());
                return;
            }
            draw(held ? "请继续按住…" : "请同时按住所需按键", names_of(held), held ? steady / 120.0 : 0.0);
        }
        app.Toast("未更改");
    }

    /* v0.7.5: a Get extras download, with its progress on screen; B cancels */
    void RunInstall(App &app, PadState &pad) {
        const extras::Tool &t = *g_pending_install;
        g_pending_install = nullptr;
        auto draw = [&t](const char *what, double frac) {
            DrawProgressCard("安装扩展工具", t.name, what, frac, "来自官方 GitHub 发布页");
            DrawHints({ { "", 'B', "取消" } }, 0);
            SDL_RenderPresent(ui::g_ren);
        };
        u32 tick = 0;
        extras::Progress progress = [&](const char *what, double frac) {
            if (!appletMainLoop()) { return false; }
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_B) { return false; }
            if ((++tick % 4) == 0 || frac < 0.0) { draw(what, frac); }
            return true;
        };
        draw("连接中…", -1.0);
        std::string done, err;
        if (extras::Install(t, progress, &done, &err)) { app.Toast(done); }
        else { app.Toast(std::string(t.name) + ": " + err); }
        app.Refresh();
    }

    /* v0.7.14: Check for updates / Install the update, with its progress on
     * screen; B cancels */
    void RunUpdate(App &app, PadState &pad, int mode) {
        const char *title = mode == 1 ? "检查更新" : "更新";
        auto draw = [title](const char *what, double frac) {
            DrawProgressCard(title, "Switch Frame Tap", what, frac, "来自本项目的 GitHub 发布页");
            DrawHints({ { "\uE0A1", 'B', "取消" } }, 0);
            SDL_RenderPresent(ui::g_ren);
        };
        u32 tick = 0;
        extras::Progress progress = [&](const char *what, double frac) {
            if (!appletMainLoop()) { return false; }
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_B) { return false; }
            if ((++tick % 4) == 0 || frac < 0.0) { draw(what, frac); }
            return true;
        };
        draw("连接中…", -1.0);
        std::string err, done;
        if (mode == 1) {
            const int r = extras::CheckUpdate(AppVersion, app.set.update_betas, &app.upd_rel, progress, &err);
            if (r < 0) { app.upd = App::Upd::Failed; app.upd_msg = err; app.Toast("检查失败：" + err); }
            else if (r == 0) { app.upd = App::Upd::Latest; app.Toast(std::string("Switch Frame Tap ") + AppVersion + " 已是最新版本"); }
            else { app.upd = App::Upd::Found; app.Toast(app.upd_rel.tag + " 可更新，请选择下方“安装更新”"); }
        } else if (extras::InstallUpdate(app.upd_rel, progress, &done, &err)) {
            app.upd = App::Upd::Installed;
            app.Toast(done);
        } else {
            app.upd_msg = err;
            app.Toast("更新未完成：" + err);
        }
        app.Refresh();
    }

    /* touch on the main screen: a tap (lifted where it went down) acts; a
     * drag scrolls the page */
    struct Touch {
        bool down = false, dragging = false;
        int x0 = 0, y0 = 0, y_last = 0;
        float scroll0 = 0;
    };

    void TouchTap(App &app, int tx, int ty) {
        for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it) {
            const SDL_Rect &r = it->r;
            if (tx < r.x || tx >= r.x + r.w || ty < r.y || ty >= r.y + r.h) { continue; }
            if (it->what == HitKind::Nav) { app.GoPage(it->index); app.in_content = false; return; }
            Page &p = app.Cur();
            const bool was = app.in_content && p.sel == it->index;
            app.in_content = true;
            p.sel = it->index;
            switch (it->what) {
                case HitKind::Switch: app.Activate(+1); break;
                case HitKind::Left:   app.Activate(-1); break;
                case HitKind::Right:  app.Activate(+1); break;
                default:              if (was) { app.Activate(+1); } break;
            }
            return;
        }
    }

    bool InContentColumn(int tx, int ty) { return tx >= CX - 8 && tx < CX + CW + 8 && ty >= ViewTop && ty < ViewBottom; }

}

#ifndef SFT_PREVIEW
int main(int, char **) {
    plInitialize(PlServiceType_User);
    pmdmntInitialize();
    nifmInitialize(NifmServiceType_User);   /* v0.7: the console's IP, shown with Network streaming */
    splInitialize();

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();

    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG);
    SDL_Window *win = SDL_CreateWindow("Switch Frame Tap", 0, 0, ui::W, ui::H, 0);
    ui::g_ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(ui::g_ren, SDL_BLENDMODE_BLEND);

    PlFontData std_font = {}, ext_font = {}; /* System text font and Nintendo button symbols, valid until plExit. */
    /* Simplified Chinese includes the Latin characters used by the UI. Fall
     * back to Standard if the console cannot supply the Chinese shared font. */
    if (R_FAILED(plGetSharedFontByType(&std_font, PlSharedFontType_ChineseSimplified))) {
        plGetSharedFontByType(&std_font, PlSharedFontType_Standard);
    }
    plGetSharedFontByType(&ext_font, PlSharedFontType_NintendoExt);
    ui::LoadFonts(std_font.address, std_font.size, ext_font.address, ext_font.size);

    App app;
    app.fw = hosversionGet();
    if (R_FAILED(splGetConfig(static_cast<SplConfigItem>(65000), &app.ams))) { app.ams = 0; }
    app.Refresh();
    app.Build();

    u32 frame = 0, hold = 0;
    Touch touch;
    bool gallery = false, gal_touching = false;
    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);
        const u64 held = padGetButtons(&pad);

        if (gallery) {
            if (down & HidNpadButton_Plus) { break; }
            HidTouchScreenState ts = {};
            if (hidGetTouchScreenStates(&ts, 1) && ts.count > 0) {
                if (!gal_touching) { gal_touching = true; GalleryTouch(g_gal, ts.touches[0].x, ts.touches[0].y); }
            } else {
                gal_touching = false;
            }
            if (!GalleryInput(g_gal, down, held, hold)) { gallery = false; app.Refresh(); continue; }
            DrawGallery(g_gal);
            continue;
        }
        if (down & HidNpadButton_Plus) { break; }
        if (down & HidNpadButton_B) {
            if (app.in_content) { app.in_content = false; app.Unconfirm(); } else { break; }
        }

        /* the pages: L / R from anywhere */
        if (down & (HidNpadButton_L | HidNpadButton_ZL)) { app.GoPage(app.page - 1); }
        if (down & (HidNpadButton_R | HidNpadButton_ZR)) { app.GoPage(app.page + 1); }

        /* D-pad / stick with auto-repeat */
        const u64 up_mask = HidNpadButton_Up | HidNpadButton_StickLUp;
        const u64 dn_mask = HidNpadButton_Down | HidNpadButton_StickLDown;
        if (down & up_mask) { app.Move(-1); hold = 0; }
        else if (down & dn_mask) { app.Move(1); hold = 0; }
        else if (held & (up_mask | dn_mask)) {
            if (++hold > 24 && hold % 6 == 0) { app.Move((held & up_mask) ? -1 : 1); }
        } else { hold = 0; }

        const u64 left = HidNpadButton_Left | HidNpadButton_StickLLeft, right = HidNpadButton_Right | HidNpadButton_StickLRight;
        if (!app.in_content) {
            if ((down & (HidNpadButton_A | right)) && app.Cur().sel >= 0) { app.in_content = true; }
        } else {
            const Row *r = app.CurRow();
            const bool choice = r != nullptr && r->kind == Kind::Choice;
            if (down & HidNpadButton_A) { app.Activate(+1); }
            else if (down & right) { if (choice) { app.Activate(+1); } }
            else if (down & left) { if (choice) { app.Activate(-1); } else { app.in_content = false; app.Unconfirm(); } }
        }

        /* touch: a tap acts on what it lands on; a drag in the page scrolls it */
        HidTouchScreenState ts = {};
        if (hidGetTouchScreenStates(&ts, 1) && ts.count > 0) {
            const int tx = ts.touches[0].x, ty = ts.touches[0].y;
            if (!touch.down) {
                touch = { true, false, tx, ty, ty, app.Cur().scroll };
            } else if (!touch.dragging && std::abs(ty - touch.y0) > 14 && InContentColumn(touch.x0, touch.y0)) {
                touch.dragging = true;
            }
            if (touch.dragging) {
                Page &p = app.Cur();
                p.target = touch.scroll0 - (ty - touch.y0);
                p.scroll = p.target;
            }
            touch.y_last = ty;
        } else if (touch.down) {
            if (!touch.dragging) { TouchTap(app, touch.x0, touch.y0); }
            touch = {};
        }

        if (g_pending_install != nullptr) { RunInstall(app, pad); touch = {}; continue; }
        if (g_pending_update != 0) { const int m = g_pending_update; g_pending_update = 0; RunUpdate(app, pad, m); touch = {}; continue; }
        if (g_record_combo) { g_record_combo = false; RecordCombo(app, pad); touch = {}; continue; }
        if (g_open_gallery) {
            g_open_gallery = false;
            g_gal.Open();
            gallery = true;
            gal_touching = true;      /* the tap that opened it must not also select in it */
            continue;
        }
        if (++frame % 30 == 0) { app.Refresh(); }
        Draw(app);
    }

    g_gal.Drop();
    extras::NetDown();
    IMG_Quit();
    ui::FreeAll();
    TTF_Quit();
    SDL_DestroyRenderer(ui::g_ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (g_sftap_open) { serviceClose(&g_sftap); }
    splExit();
    nifmExit();
    pmdmntExit();
    plExit();
    return 0;
}
#endif
