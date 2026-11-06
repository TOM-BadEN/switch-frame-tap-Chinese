/*
 * Switch Frame Tap overlay (Tesla): turn the stream on and off, see what it
 * is doing, and switch the game between handheld and docked mode through
 * ReverseNX-RT (which needs SaltyNX).
 *
 *  - The stream: the applet-mitm sysmodule's "sftap" service
 *    (tier4/applet-mitm/source/applet_mitm_control.hpp).
 *  - The display mode: SaltyNX's shared memory, exactly as ReverseNX-RT's own
 *    overlay does it (github.com/masagrator/ReverseNX-RT, MIT): ask the
 *    "SaltySD" port for the handle (command 7), map 0x1000 bytes, find the
 *    block whose magic is "NXRT", and write its def/isDocked flags. SaltyNX
 *    applies them to the running game.
 *  - v0.7.2: sound in games that block recording, through dvr-patches
 *    (github.com/exelix11/dvr-patches, BSD-3): patches to `am` that let the
 *    console's recorder run for every game, so grc:d has their sound.
 *    v0.7.4: bundled, off; the toggle is common/sound_patch.hpp (shared with
 *    the manager). am reads its patches at boot: a change needs a restart.
 *  - v0.7.14: the beta features (HOME menu, overlays in the stream) - config.ini
 *    keys the sysmodule reads at boot, as the manager's rows; the status says
 *    what this boot runs (reserved3 bits 4 and 5), so a change shows
 *    "restart to apply" until it is.
 */
#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <cstdio>
#include <cstring>
#include "../../common/sound_patch.hpp"
#include "../../common/config_ini.hpp"

namespace {

    /* ---- sftap ---------------------------------------------------------- */

    struct StreamStatus {
        u32 version, state, enabled, width, height, fps_x10, game_fps_x10, sessions;
        u32 shot_enabled, shots, shot_fails, reserved;     /* version 2 (M99) */
        u64 app_tid; u32 app_excluded, reserved3;          /* version 3 (v0.1.1b) */
    };
    static_assert(sizeof(StreamStatus) == 64);

    enum : u32 {
        LiveState_Starting = 0, LiveState_Off, LiveState_WaitViewer, LiveState_WaitGame,
        LiveState_Streaming, LiveState_Wedged, LiveState_NotArmed,
    };

    Service g_sftap;
    bool g_sftap_open = false;

    /* sm defers GetService for a name nobody registered - the overlay would
     * hang without the sysmodule. Registering the name ourselves fails when
     * it is already registered, which is the answer. */
    bool IsServiceRunning(const char *name) {
        Handle h;
        const SmServiceName sn = smEncodeName(name);
        if (R_FAILED(smRegisterService(&h, sn, false, 1))) { return true; }
        svcCloseHandle(h);
        smUnregisterService(sn);
        return false;
    }

    /* libtesla keeps no sm session open after start-up (Run Y: every sm call
     * from the GUI failed, so the toggle never reached the sysmodule): open
     * one for the lookup. Once connected the session to sftap stays open. */
    bool SftapConnect() {
        if (g_sftap_open) { return true; }
        tsl::hlp::doWithSmSession([] {
            if (IsServiceRunning("sftap")) {
                g_sftap_open = R_SUCCEEDED(smGetService(&g_sftap, "sftap"));
            }
        });
        return g_sftap_open;
    }

    bool SftapStatus(StreamStatus *st) {
        if (!SftapConnect()) { return false; }
        if (R_FAILED(serviceDispatchOut(&g_sftap, 0, *st))) {
            serviceClose(&g_sftap);
            g_sftap_open = false;
            return false;
        }
        return true;
    }

    /* v0.1.1b: the never-attach list - for apps that refuse to run under a
     * debugger (TiCo's protected builds) */
    void SftapSetAppExcluded(u64 tid, bool excluded) {
        if (!SftapConnect()) { return; }
        const struct { u64 tid; u8 excluded; u8 pad[7]; } in = { tid, static_cast<u8>(excluded ? 1 : 0), {} };
        serviceDispatchIn(&g_sftap, 3, in);
    }

    /* v0.7.18: the module reads config.ini again (sftap 2, as the app does after saving) */
    void SftapReload() {
        if (!SftapConnect()) { return; }
        serviceDispatch(&g_sftap, 2);
    }

    /* v0.7.12: screen off while streaming - now (sftap 4); config.ini keeps it */
    void SftapSetScreenOff(bool on) {
        if (!SftapConnect()) { return; }
        const u8 v = on ? 1 : 0;
        serviceDispatchIn(&g_sftap, 4, v);
    }

    void SftapSetEnabled(bool on) {
        if (!SftapConnect()) { return; }
        const u8 v = on ? 1 : 0;
        serviceDispatchIn(&g_sftap, 1, v);
    }

    /* ---- ReverseNX-RT, through SaltyNX ---------------------------------- */

    constexpr u32 NxrtMagic = 0x5452584E;   /* "NXRT" */

    struct RnxShared {
        u32 magic;
        bool isDocked;       /* forced mode, when def is false */
        bool def;            /* true: the game's own (system) mode */
        bool pluginActive;   /* the game asked for its mode through SaltyNX's hooks */
        u8 res;
        bool wasDDRused;
    } NX_PACKED;

    SharedMemory g_shmem;
    bool g_shmem_ok = false;
    u64 g_rnx_pid = 0;
    volatile RnxShared *g_rnx = nullptr;

    /* one old-style request on the SaltySD port (SaltyNX.h: SFCI magic,
     * u64 command id, zero payload, the pid sent along) */
    Result SaltyCall(Handle port, u32 cmd, u32 payload, Handle *out) {
        void *tls = armGetTls();
        CmifRequest req = cmifMakeRequest(tls, CmifRequestFormat{ .request_id = cmd, .data_size = payload, .send_pid = 1 });
        std::memset(req.data, 0, payload);
        Result rc = svcSendSyncRequest(port);
        if (R_FAILED(rc)) { return rc; }
        HipcResponse resp = hipcParseResponse(tls);
        const auto *hdr = reinterpret_cast<const CmifOutHeader *>((reinterpret_cast<uintptr_t>(resp.data_words) + 15) & ~uintptr_t(15));
        if (hdr->magic != CMIF_OUT_HEADER_MAGIC) { return MAKERESULT(Module_Libnx, LibnxError_InvalidCmifOutHeader); }
        if (hdr->result != 0) { return hdr->result; }
        if (out != nullptr) {
            if (resp.num_copy_handles > 0)      { *out = resp.copy_handles[0]; }
            else if (resp.num_move_handles > 0) { *out = resp.move_handles[0]; }
            else { return MAKERESULT(Module_Libnx, LibnxError_BadInput); }
        }
        return 0;
    }

    bool MapSaltyShared() {
        if (g_shmem_ok) { return true; }
        Handle port = INVALID_HANDLE;
        if (R_FAILED(svcConnectToNamedPort(&port, "SaltySD"))) { return false; }
        Handle sh = INVALID_HANDLE;
        const Result rc = SaltyCall(port, 7, 16, &sh);
        SaltyCall(port, 0, 24, nullptr);    /* Term: SaltySD answers 0xf601 and closes */
        svcCloseHandle(port);
        if (R_FAILED(rc)) { return false; }
        shmemLoadRemote(&g_shmem, sh, 0x1000, Perm_Rw);
        g_shmem_ok = R_SUCCEEDED(shmemMap(&g_shmem));
        return g_shmem_ok;
    }

    /* the game's block, found again whenever the game changes */
    volatile RnxShared *FindRnx(u64 pid) {
        if (!g_shmem_ok) { return nullptr; }
        if (pid == g_rnx_pid && g_rnx != nullptr && g_rnx->magic == NxrtMagic) { return g_rnx; }
        g_rnx = nullptr;
        g_rnx_pid = pid;
        const uintptr_t base = reinterpret_cast<uintptr_t>(shmemGetAddr(&g_shmem));
        for (uintptr_t off = 0; off + sizeof(RnxShared) <= 0x1000; off += 4) {
            if (*reinterpret_cast<volatile u32 *>(base + off) == NxrtMagic) {
                g_rnx = reinterpret_cast<volatile RnxShared *>(base + off);
                break;
            }
        }
        return g_rnx;
    }

    enum class Mode { Unavailable, Default, Handheld, Docked };

    std::string g_rnx_note;

    volatile RnxShared *CurrentRnx() {
        u64 pid = 0;
        if (R_FAILED(pmdmntGetApplicationProcessId(&pid))) { g_rnx_note = "没有正在运行的游戏"; return nullptr; }
        if (!MapSaltyShared()) { g_rnx_note = "未找到 SaltyNX"; return nullptr; }
        volatile RnxShared *r = FindRnx(pid);
        if (r == nullptr) { g_rnx_note = "当前游戏未加载 ReverseNX-RT"; return nullptr; }
        if (!r->pluginActive) { g_rnx_note = "当前游戏未使用 ReverseNX-RT"; return nullptr; }
        g_rnx_note.clear();
        return r;
    }

    Mode CurrentMode() {
        volatile RnxShared *r = CurrentRnx();
        if (r == nullptr) { return Mode::Unavailable; }
        if (r->def) { return Mode::Default; }
        return r->isDocked ? Mode::Docked : Mode::Handheld;
    }

    void SetMode(Mode m) {
        volatile RnxShared *r = CurrentRnx();
        if (r == nullptr || m == Mode::Unavailable) { return; }
        if (m == Mode::Default) { r->def = true; return; }
        r->isDocked = (m == Mode::Docked);
        r->def = false;
    }

    /* ---- dvr-patches: sound in games that block recording --------------- */

    /* the shared logic (common/sound_patch.hpp); libtesla keeps no sdmc:
     * mounted, so each call gets it for its duration */
    using PatchState = sftsound::State;

    PatchState GetPatchState() {
        PatchState st = PatchState::Missing;
        tsl::hlp::doWithSDCardHandle([&] { st = sftsound::Get(); });
        return st;
    }

    bool SetPatch(bool on) {
        bool ok = false;
        tsl::hlp::doWithSDCardHandle([&] { ok = sftsound::Set(on); });
        return ok;
    }

    /* ---- the GUI -------------------------------------------------------- */

    std::string StatusText(bool have, const StreamStatus &st) {
        char b[96];
        if (!have) { return "后台模块未运行"; }
        switch (st.state) {
            case LiveState_Starting:   return "启动中（等待游戏）";
            case LiveState_Off:        return "关闭";
            case LiveState_WaitViewer: return (st.reserved3 & 1) ? "等待摄像头应用连接" : (st.reserved3 & 2) ? "等待客户端（USB/网络）" : "等待电脑客户端";   /* v0.4 webcam, v0.7 network */
            case LiveState_WaitGame:   return "等待游戏启动";
            case LiveState_Streaming:
                std::snprintf(b, sizeof(b), "串流 %ux%u，%u.%u FPS", st.width, st.height, st.fps_x10 / 10, st.fps_x10 % 10);
                return b;
            case LiveState_Wedged:     return "编码器无响应，请重启主机";
            case LiveState_NotArmed:   return "测试文件未启用实时串流";
            case 7:                    return "固件未经测试，请查看管理应用";
            case 8:                    return "当前应用已排除串流";
            default:                   return "未知";
        }
    }

    class MainGui : public tsl::Gui {
        public:
            tsl::elm::Element *createUI() override {
                auto *frame = new tsl::elm::OverlayFrame("Switch Frame Tap", "v0.7.21.1");
                auto *list = new tsl::elm::List();

                list->addItem(new tsl::elm::CategoryHeader("串流到电脑"));
                m_have = SftapStatus(&m_st);
                m_toggle = new tsl::elm::ToggleListItem("串流", m_have && m_st.enabled != 0);
                m_toggle->setStateChangedListener([this](bool on) {
                    SftapSetEnabled(on);
                    m_tick = 0;
                });
                list->addItem(m_toggle);
                m_app = new tsl::elm::ListItem("串流当前应用");
                m_app->setClickListener([this](u64 keys) {
                    if ((keys & HidNpadButton_A) && m_have && m_st.version >= 3 && m_st.app_tid != 0) {
                        SftapSetAppExcluded(m_st.app_tid, m_st.app_excluded == 0);
                        m_tick = 0;
                        return true;
                    }
                    return false;
                });
                list->addItem(m_app);
                {
                    /* libtesla keeps sdmc unmounted: mount it around each access
                     * (v0.7.13-test2: the setting was applied but never saved) */
                    std::string v;
                    bool on = false;
                    tsl::hlp::doWithSDCardHandle([&] { on = sftini::Get("screen_off", &v) && v == "1"; });
                    auto *scr = new tsl::elm::ToggleListItem("串流时关闭主机屏幕", on);
                    scr->setStateChangedListener([](bool on) {
                        tsl::hlp::doWithSDCardHandle([&] { sftini::Set("screen_off", on ? "1" : "0"); });
                        SftapSetScreenOff(on);
                    });
                    list->addItem(scr);
                }
                list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *r, s32 x, s32 y, s32 w, s32 h) {
                    r->drawString(m_status.c_str(), false, x + 15, y + 22, 18, r->a(tsl::style::color::ColorText));
                    r->drawString(m_game.c_str(), false, x + 15, y + 46, 16, r->a(tsl::style::color::ColorDescription));
                }), 60);

                list->addItem(new tsl::elm::CategoryHeader("游戏声音"));
                m_patch_state = GetPatchState();
                m_patch = new tsl::elm::ToggleListItem("采集禁用录制游戏的声音", m_patch_state == PatchState::On);
                m_patch->setStateChangedListener([this](bool on) {
                    if (m_patch_state == PatchState::Missing) { m_patch->setState(false); return; }
                    if (SetPatch(on)) { m_patch_changed = !m_patch_changed; }
                    m_patch_state = GetPatchState();
                    m_patch->setState(m_patch_state == PatchState::On);
                });
                list->addItem(m_patch);
                list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *r, s32 x, s32 y, s32 w, s32 h) {
                    const char *l1 = m_patch_state == PatchState::Missing ? "未安装，请查看说明文档"
                                   : m_patch_changed ? "重启主机后生效"
                                   : "适用于大乱斗等游戏。若崩溃请关闭。";
                    r->drawString(l1, false, x + 15, y + 22, 16, r->a(m_patch_changed ? tsl::style::color::ColorHighlight : tsl::style::color::ColorDescription));
                }), 36);

                list->addItem(new tsl::elm::CategoryHeader("实验功能（重启后生效）"));
                {
                    std::string v;
                    tsl::hlp::doWithSDCardHandle([&] {
                        m_cfg_home = sftini::Get("home_menu", &v) && v == "1";
                        m_cfg_ovl = sftini::Get("overlays", &v) && v == "1";
                    });
                    auto *home = new tsl::elm::ToggleListItem("串流 HOME 菜单", m_cfg_home);
                    home->setStateChangedListener([this](bool on) {
                        tsl::hlp::doWithSDCardHandle([&] { sftini::Set("home_menu", on ? "1" : "0"); });
                        m_cfg_home = on;
                    });
                    list->addItem(home);
                    auto *ovl = new tsl::elm::ToggleListItem("串流悬浮界面", m_cfg_ovl);
                    ovl->setStateChangedListener([this](bool on) {
                        tsl::hlp::doWithSDCardHandle([&] { sftini::Set("overlays", on ? "1" : "0"); });
                        m_cfg_ovl = on;
                    });
                    list->addItem(ovl);
                }
                list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *r, s32 x, s32 y, s32 w, s32 h) {
                    const bool pending = m_have && (((m_st.reserved3 & 16) != 0) != m_cfg_home || ((m_st.reserved3 & 32) != 0) != m_cfg_ovl);
                    const char *l1 = pending ? "重启主机后生效" : "包含音量条、快捷设置、电源及悬浮菜单";
                    r->drawString(l1, false, x + 15, y + 22, 16, r->a(pending ? tsl::style::color::ColorHighlight : tsl::style::color::ColorDescription));
                }), 36);

                list->addItem(new tsl::elm::CategoryHeader("显示模式（ReverseNX-RT）"));
                static const char *const names[] = { "", "游戏默认", "掌机模式（720p）", "底座模式（1080p）" };
                for (int i = 1; i <= 3; ++i) {
                    auto *item = new tsl::elm::ListItem(names[i]);
                    const Mode m = static_cast<Mode>(i);
                    item->setClickListener([this, m](u64 keys) {
                        if (keys & HidNpadButton_A) { SetMode(m); m_tick = 0; return true; }
                        return false;
                    });
                    m_modes[i] = item;
                    list->addItem(item);
                }
                list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *r, s32 x, s32 y, s32 w, s32 h) {
                    const char *note = !g_rnx_note.empty() ? g_rnx_note.c_str() : "1080p60 建议 CPU 1785 MHz";
                    r->drawString(note, false, x + 15, y + 22, 16, r->a(tsl::style::color::ColorDescription));
                }), 36);

                /* v0.7.18: homebrew from the Homebrew Menu told the console is docked
                 * (homebrew_docked) - from the next app started, no restart */
                list->addItem(new tsl::elm::CategoryHeader("自制应用（实验功能）"));
                {
                    std::string v;
                    bool on = false;
                    tsl::hlp::doWithSDCardHandle([&] { on = sftini::Get("homebrew_docked", &v) && v == "1"; });
                    auto *hb = new tsl::elm::ToggleListItem("底座模式（1080p）", on);
                    hb->setStateChangedListener([](bool on) {
                        tsl::hlp::doWithSDCardHandle([&] { sftini::Set("homebrew_docked", on ? "1" : "0"); });
                        SftapReload();
                    });
                    list->addItem(hb);
                }
                list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *r, s32 x, s32 y, s32 w, s32 h) {
                    r->drawString("下次通过 R 启动游戏或前端进入时生效", false, x + 15, y + 22, 16, r->a(tsl::style::color::ColorDescription));
                }), 36);

                frame->setContent(list);
                this->Refresh();
                return frame;
            }

            void update() override {
                if (++m_tick % 30 == 1) { this->Refresh(); }
            }

        private:
            void Refresh() {
                m_have = SftapStatus(&m_st);
                m_status = StatusText(m_have, m_st);
                if (m_have && m_st.state == LiveState_Streaming && m_st.game_fps_x10 != 0) {
                    char b[64];
                    std::snprintf(b, sizeof(b), "游戏 %u.%u FPS", m_st.game_fps_x10 / 10, m_st.game_fps_x10 % 10);
                    m_game = b;
                } else {
                    m_game.clear();
                }
                if (m_app != nullptr) {
                    if (!m_have || m_st.version < 3 || m_st.app_tid == 0) { m_app->setValue("无应用", true); }
                    else { m_app->setValue(m_st.app_excluded ? "关闭" : "开启", m_st.app_excluded != 0); }
                }
                if (m_toggle != nullptr && m_have && m_toggle->getState() != (m_st.enabled != 0)) {
                    m_toggle->setState(m_st.enabled != 0);
                }
                const Mode cur = CurrentMode();
                for (int i = 1; i <= 3; ++i) {
                    if (m_modes[i] == nullptr) { continue; }
                    if (cur == Mode::Unavailable) { m_modes[i]->setValue("-", true); }
                    else { m_modes[i]->setValue(static_cast<int>(cur) == i ? "已启用" : "", false); }
                }
            }

            StreamStatus m_st = {};
            bool m_have = false;
            u32 m_tick = 0;
            std::string m_status, m_game;
            tsl::elm::ToggleListItem *m_toggle = nullptr;
            tsl::elm::ListItem *m_app = nullptr;
            tsl::elm::ListItem *m_modes[4] = {};
            tsl::elm::ToggleListItem *m_patch = nullptr;
            PatchState m_patch_state = PatchState::Missing;
            bool m_patch_changed = false;     /* toggled since the last restart (in this overlay session) */
            bool m_cfg_home = false, m_cfg_ovl = false;   /* config.ini: what the next boot runs */
    };

    class FrameTapOverlay : public tsl::Overlay {
        public:
            /* libtesla has already initialized fs, hid, pl, pmdmnt, hid:sys and set:sys */
            void initServices() override {}
            void exitServices() override {
                if (g_sftap_open) { serviceClose(&g_sftap); g_sftap_open = false; }
                if (g_shmem_ok) { shmemClose(&g_shmem); g_shmem_ok = false; }
            }
            std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<MainGui>(); }
    };

}

int main(int argc, char **argv) {
    return tsl::loop<FrameTapOverlay>(argc, argv);
}
