#pragma once
/*
 * v0.7.14: the viewer updates itself from the project's GitHub releases.
 *
 * The system's own tools do the network and the unpacking - curl and tar
 * (Windows 10 1803 and later ship both; Linux has them), curl and ditto on
 * macOS - so the viewer carries no TLS library. A check asks api.github.com
 * for the newest release (with "Include betas", the newest of any kind) and
 * compares its tag with SFT_VERSION (common/sft_version.h). An update
 * downloads this platform's asset, unpacks it in a temporary folder and
 * swaps the files:
 *   - Linux: the new binary is copied beside the running one and renamed
 *     over it (the running process keeps its old file until it exits);
 *   - Windows: a running .exe or .dll cannot be overwritten but can be
 *     renamed, so each file in the way becomes name.old first (removed at
 *     the next start);
 *   - macOS: the new "Switch Frame Tap.app" is copied beside the old one and
 *     the two bundles swap names.
 * Then "Restart" starts the new viewer and this one exits. Builds without a
 * version (make, the Flatpak) do not update themselves: their packaging does.
 * Everything slow runs on a thread; the menus read g_upd.
 */
#include "../../common/sft_version.h"
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifndef SFT_VERSION
#define SFT_VERSION "dev"
#endif

#define UPD_REPO "TomasUribe/switch-frame-tap"

enum { UPD_IDLE, UPD_CHECKING, UPD_LATEST, UPD_FOUND, UPD_GETTING, UPD_READY, UPD_ERROR, UPD_UNSUPPORTED };

static struct {
    SDL_atomic_t state;
    char tag[48];            /* the newer release */
    char url[600];           /* its asset for this platform */
    char msg[200];           /* what went wrong, or why updating is off */
    int failed_update;       /* the error came from an update (not a check) */
} g_upd;
static int g_upd_betas = 0;          /* viewer.ini update_betas: offer pre-releases too */
static int g_upd_check = 1;          /* viewer.ini update_check: look once at start-up */
static char **g_upd_argv;            /* for the restart */
static char g_upd_self[1024];        /* this program (Linux, Windows) or its .app (macOS) */
static char g_upd_tmp[1024];

static Uint32 g_upd_note_until = 0;  /* a passing note (upd_note) shows until then */
static char g_upd_note[120];

static int upd_state(void) { return SDL_AtomicGet(&g_upd.state); }
static void upd_note(const char *m) { snprintf(g_upd_note, sizeof(g_upd_note), "%s", m); g_upd_note_until = SDL_GetTicks() + 4000; }
static void upd_set(int s) { SDL_AtomicSet(&g_upd.state, s); }
static void upd_fail(int update, const char *m)
{
    snprintf(g_upd.msg, sizeof(g_upd.msg), "%s", m);
    g_upd.failed_update = update;
    upd_set(UPD_ERROR);
}

static const char *upd_asset_suffix(void)
{
#if defined(_WIN32)
    return "-windows-viewer.zip";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "-macos-arm64.zip";
#elif defined(__APPLE__)
    return "-macos-x86_64.zip";
#elif defined(__linux__) && defined(__x86_64__)
    /* v0.7.17: an AppImage updates to the release's AppImage */
    return getenv("APPIMAGE") ? "-linux-x86_64.AppImage" : "-linux.tar.gz";
#else
    return NULL;
#endif
}

/* ---- running the system's tools ---------------------------------------- */

#ifdef _WIN32
static int upd_widen(const char *s, wchar_t *out, int cap) { return MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap) > 0; }

/* args[0] is looked up on PATH (System32 holds curl.exe and tar.exe) */
static int upd_run(const char *const *args)
{
    char line[4096] = "";
    for (int i = 0; args[i]; i++) {
        size_t n = strlen(line);
        snprintf(line + n, sizeof(line) - n, "%s\"%s\"", i ? " " : "", args[i]);
    }
    wchar_t wl[4096];
    if (!upd_widen(line, wl, 4096)) return -1;
    STARTUPINFOW si = { 0 };
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessW(NULL, wl, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}
#else
static int upd_run(const char *const *args)
{
    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(args[0], (char *const *)args);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
#endif

static int upd_curl(const char *url, const char *out, int seconds)
{
    char ua[64], tmo[16];
    snprintf(ua, sizeof(ua), "switch-frame-tap-viewer/%s", SFT_VERSION);
    snprintf(tmo, sizeof(tmo), "%d", seconds);
    const char *args[] = { "curl", "-fsSL", "--max-time", tmo, "-A", ua, "-H", "Accept: application/vnd.github+json", "-o", out, url, NULL };
    return upd_run(args);
}

/* ---- a check ------------------------------------------------------------ */

/* the string value after "key": in json, from p on; NULL when there is none */
static const char *upd_json_str(const char *p, const char *key, char *out, size_t cap)
{
    char k[64];
    snprintf(k, sizeof(k), "\"%s\"", key);
    const char *q = strstr(p, k);
    if (!q) return NULL;
    q += strlen(k);
    while (*q == ' ' || *q == ':' || *q == '\t' || *q == '\n' || *q == '\r') q++;
    if (*q != '"') return NULL;
    q++;
    size_t n = 0;
    while (*q && *q != '"' && n + 1 < cap) {
        if (*q == '\\' && q[1]) q++;
        out[n++] = *q++;
    }
    out[n] = 0;
    return q;
}

static char *upd_read(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = n > 0 && n < 32L * 1024 * 1024 ? (char *)malloc((size_t)n + 1) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) { b[n] = 0; if (len) *len = n; }
    return b;
}

static void upd_tmp_make(void)
{
    if (g_upd_tmp[0]) return;
#ifdef _WIN32
    wchar_t wt[MAX_PATH];
    char t[MAX_PATH * 3];
    GetTempPathW(MAX_PATH, wt);
    WideCharToMultiByte(CP_UTF8, 0, wt, -1, t, sizeof(t), NULL, NULL);
    snprintf(g_upd_tmp, sizeof(g_upd_tmp), "%ssft-update-%lu", t, (unsigned long)GetCurrentProcessId());
    wchar_t wd[1024];
    if (upd_widen(g_upd_tmp, wd, 1024)) CreateDirectoryW(wd, NULL);
#else
    const char *t = getenv("TMPDIR");
    snprintf(g_upd_tmp, sizeof(g_upd_tmp), "%s/sft-update-XXXXXX", t && *t ? t : "/tmp");
    if (!mkdtemp(g_upd_tmp)) snprintf(g_upd_tmp, sizeof(g_upd_tmp), "/tmp");
#endif
}

/* the download and its unpacked files, once they are in place */
static void upd_tmp_clean(void)
{
#ifndef _WIN32
    char a[1200];
    snprintf(a, sizeof(a), "%s", g_upd_tmp);
    if (!strstr(a, "/sft-update-")) return;   /* only our own folder (mkdtemp may have failed) */
    const char *rm[] = { "rm", "-rf", a, NULL };
    upd_run(rm);
    g_upd_tmp[0] = 0;
#endif
}

static int upd_check_main(void *arg)
{
    (void)arg;
    upd_tmp_make();
    char json[1200], url[200];
    snprintf(json, sizeof(json), "%s/releases.json", g_upd_tmp);
    if (g_upd_betas) snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases?per_page=10", UPD_REPO);
    else snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", UPD_REPO);
    const int rc = upd_curl(url, json, 30);
    if (rc != 0) {
        upd_fail(0, rc < 0 || rc == 127 ? "无法运行 curl，请到 GitHub 查看更新" : "无法连接 GitHub");
        return 0;
    }
    char *b = upd_read(json, NULL);
    remove(json);
    if (!b) { upd_fail(0, "无法读取 GitHub 的响应"); return 0; }
    /* the highest version in the answer: GitHub lists releases by date, and
     * a fix release can come after the next version's betas */
    char tag[48] = "", t[48];
    for (const char *p = b; (p = upd_json_str(p, "tag_name", t, sizeof(t))) != NULL; ) {
        if (t[0] && (!tag[0] || sft_version_cmp(t, tag) > 0)) snprintf(tag, sizeof(tag), "%s", t);
    }
    if (!tag[0]) { free(b); upd_tmp_clean(); upd_fail(0, "GitHub 响应中没有发布版本"); return 0; }
    if (sft_version_cmp(tag, SFT_VERSION) <= 0) { free(b); upd_tmp_clean(); upd_set(UPD_LATEST); return 0; }
    /* its asset for this platform: .../releases/download/<tag>/<name><suffix> */
    char want[96], u[600];
    snprintf(want, sizeof(want), "/download/%s/", tag);
    const char *suf = upd_asset_suffix(), *p = b;
    g_upd.url[0] = 0;
    while ((p = upd_json_str(p, "browser_download_url", u, sizeof(u))) != NULL) {
        const size_t n = strlen(u), sl = strlen(suf);
        if (strstr(u, want) && n > sl && !strcmp(u + n - sl, suf)) { snprintf(g_upd.url, sizeof(g_upd.url), "%s", u); break; }
    }
    free(b);
    if (!g_upd.url[0]) { upd_tmp_clean(); upd_fail(0, "最新版本尚未提供适用于此电脑的客户端"); return 0; }
    snprintf(g_upd.tag, sizeof(g_upd.tag), "%s", tag);
    upd_set(UPD_FOUND);
    return 0;
}

/* ---- an update ---------------------------------------------------------- */

#ifdef _WIN32
/* every file under src goes to dst; one in the way is renamed name.old */
static int upd_swap_tree(const wchar_t *src, const wchar_t *dst)
{
    wchar_t pat[1100];
    swprintf(pat, 1100, L"%ls\\*", src);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int ok = 1;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t s[1100], d[1100], old[1110];
        swprintf(s, 1100, L"%ls\\%ls", src, fd.cFileName);
        swprintf(d, 1100, L"%ls\\%ls", dst, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CreateDirectoryW(d, NULL);
            ok = upd_swap_tree(s, d) && ok;
            continue;
        }
        if (GetFileAttributesW(d) != INVALID_FILE_ATTRIBUTES) {
            swprintf(old, 1110, L"%ls.old", d);
            if (!MoveFileExW(d, old, MOVEFILE_REPLACE_EXISTING)) { ok = 0; continue; }
        }
        if (!MoveFileExW(s, d, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) ok = 0;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

/* the .old files a previous update left (they were in use then) */
static void upd_clean_old(const wchar_t *dir)
{
    wchar_t pat[1100];
    swprintf(pat, 1100, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t p[1100];
        swprintf(p, 1100, L"%ls\\%ls", dir, fd.cFileName);
        const size_t n = wcslen(fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) upd_clean_old(p);
        else if (n > 4 && !wcscmp(fd.cFileName + n - 4, L".old")) DeleteFileW(p);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
#else
/* the one folder the package unpacks to (switch-frame-tap-<ver>-...) */
static int upd_first_dir(const char *in, const char *prefix, char *out, size_t cap)
{
    DIR *d = opendir(in);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d)) != NULL) {
        if (!strncmp(e->d_name, prefix, strlen(prefix))) { snprintf(out, cap, "%s/%s", in, e->d_name); found = 1; break; }
    }
    closedir(d);
    return found;
}
#endif

static int upd_get_main(void *arg)
{
    (void)arg;
    char asset[1200], x[1200];
    snprintf(asset, sizeof(asset), "%s/update%s", g_upd_tmp, upd_asset_suffix());
    snprintf(x, sizeof(x), "%s/x", g_upd_tmp);
    if (upd_curl(g_upd.url, asset, 900) != 0) { upd_fail(1, "下载未完成，请重试"); return 0; }
#ifdef _WIN32
    wchar_t wx[1200], wself[1024];
    upd_widen(x, wx, 1200);
    CreateDirectoryW(wx, NULL);
    const char *tar[] = { "tar", "-xf", asset, "-C", x, NULL };
    if (upd_run(tar) != 0) { upd_fail(1, "无法解压下载文件（tar 需要 Windows 10 1803 或更高版本）"); return 0; }
    /* x\switch-frame-tap-<ver>-windows-viewer\... over the folder this .exe is in */
    wchar_t pat[1300], src[1300];
    swprintf(pat, 1300, L"%ls\\switch-frame-tap-*", wx);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) { upd_fail(1, "下载文件中没有客户端"); return 0; }
    swprintf(src, 1300, L"%ls\\%ls", wx, fd.cFileName);
    FindClose(h);
    upd_widen(g_upd_self, wself, 1024);
    wchar_t *slash = wcsrchr(wself, L'\\');
    if (slash) *slash = 0;
    if (!upd_swap_tree(src, wself)) { upd_fail(1, "部分文件无法替换，请检查目录是否只读"); return 0; }
#elif defined(__APPLE__)
    mkdir(x, 0755);
    const char *ditto[] = { "ditto", "-x", "-k", asset, x, NULL };
    if (upd_run(ditto) != 0) { upd_fail(1, "无法解压下载文件"); return 0; }
    char app[1300], next[1100], old[1100];
    snprintf(app, sizeof(app), "%s/Switch Frame Tap.app", x);
    snprintf(next, sizeof(next), "%s.new", g_upd_self);
    snprintf(old, sizeof(old), "%s.old", g_upd_self);
    const char *rmn[] = { "rm", "-rf", next, NULL }, *rmo[] = { "rm", "-rf", old, NULL };
    upd_run(rmn);
    upd_run(rmo);
    const char *copy[] = { "ditto", app, next, NULL };
    if (upd_run(copy) != 0) { upd_fail(1, "无法将新应用放入原应用目录，请检查目录是否只读"); return 0; }
    if (rename(g_upd_self, old) != 0 || rename(next, g_upd_self) != 0) { upd_fail(1, "无法替换应用"); return 0; }
    upd_run(rmo);
#else
    /* v0.7.17: an AppImage - the download is the new AppImage itself, put in
     * place of this one (renaming over a running AppImage is fine: it stays
     * mounted until it exits) */
    if (getenv("APPIMAGE")) {
        char next[1100];
        snprintf(next, sizeof(next), "%s.new", g_upd_self);
        long n = 0;
        char *data = upd_read(asset, &n);
        FILE *f = data ? fopen(next, "wb") : NULL;
        const int wrote = f && n > 0 && fwrite(data, 1, (size_t)n, f) == (size_t)n;
        if (f) fclose(f);
        free(data);
        if (!wrote || chmod(next, 0755) != 0 || rename(next, g_upd_self) != 0) {
            remove(next);
            upd_fail(1, "无法替换 AppImage，请检查目录是否可写");
            return 0;
        }
        upd_tmp_clean();
        upd_set(UPD_READY);
        return 0;
    }
    mkdir(x, 0755);
    const char *tar[] = { "tar", "-xzf", asset, "-C", x, NULL };
    if (upd_run(tar) != 0) { upd_fail(1, "无法解压下载文件"); return 0; }
    char dir[1600], bin[1700], next[1100];
    if (!upd_first_dir(x, "switch-frame-tap-", dir, sizeof(dir))) { upd_fail(1, "下载文件中没有客户端"); return 0; }
    snprintf(bin, sizeof(bin), "%s/switch-frame-tap-viewer", dir);
    snprintf(next, sizeof(next), "%s.new", g_upd_self);
    long n = 0;
    char *data = upd_read(bin, &n);
    FILE *f = data ? fopen(next, "wb") : NULL;
    const int wrote = f && fwrite(data, 1, (size_t)n, f) == (size_t)n;
    if (f) fclose(f);
    free(data);
    if (!wrote || chmod(next, 0755) != 0 || rename(next, g_upd_self) != 0) {
        remove(next);
        upd_fail(1, "无法替换客户端，请检查安装目录是否可写");
        return 0;
    }
#endif
    upd_tmp_clean();
    upd_set(UPD_READY);
    return 0;
}

/* ---- what the menus call ----------------------------------------------- */

static void upd_check(void)
{
    const int s = upd_state();
    if (s == UPD_CHECKING || s == UPD_GETTING || s == UPD_READY || s == UPD_UNSUPPORTED) return;
    upd_set(UPD_CHECKING);
    SDL_Thread *t = SDL_CreateThread(upd_check_main, "update-check", NULL);
    if (t) SDL_DetachThread(t); else upd_fail(0, "无法开始检查");
}

static void upd_get(void)
{
    if (upd_state() != UPD_FOUND && !(upd_state() == UPD_ERROR && g_upd.failed_update)) return;
    upd_set(UPD_GETTING);
    SDL_Thread *t = SDL_CreateThread(upd_get_main, "update-get", NULL);
    if (t) SDL_DetachThread(t); else upd_fail(1, "无法开始下载");
}

/* the new viewer starts; this one ends */
static void upd_restart(void)
{
#ifdef _WIN32
    wchar_t self[1024];
    upd_widen(g_upd_self, self, 1024);
    STARTUPINFOW si = { 0 };
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = { 0 };
    if (CreateProcessW(self, GetCommandLineW(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        exit(0);
    }
#elif defined(__APPLE__)
    const char *open_app[] = { "open", "-n", g_upd_self, NULL };
    if (upd_run(open_app) == 0) exit(0);
#else
    /* exec keeps open descriptors: the USB device's would stay claimed and
     * the new viewer could not open the Switch */
    for (int fd = 3; fd < 4096; fd++) close(fd);
    execv(g_upd_self, g_upd_argv);
#endif
    upd_fail(1, "请重启客户端以使用新版本");
}

/* at start-up: where this program is, the previous update's leftovers, and
 * the day's check */
static void upd_init(char **argv)
{
    g_upd_argv = argv;
    upd_set(UPD_IDLE);
    const char *ver = SFT_VERSION;
    if (!strcmp(ver, "dev") || !upd_asset_suffix()) {
        snprintf(g_upd.msg, sizeof(g_upd.msg), "当前为源码构建版本，请通过重新构建更新");
        upd_set(UPD_UNSUPPORTED);
        return;
    }
#ifdef _WIN32
    wchar_t w[1024];
    GetModuleFileNameW(NULL, w, 1024);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, g_upd_self, sizeof(g_upd_self), NULL, NULL);
    wchar_t *slash = wcsrchr(w, L'\\');
    if (slash) { *slash = 0; upd_clean_old(w); }
#elif defined(__APPLE__)
    char exe[1024];
    uint32_t sz = sizeof(exe);
    char *app = NULL;
    if (_NSGetExecutablePath(exe, &sz) == 0 && (app = strstr(exe, ".app/")) != NULL) {
        app[4] = 0;
        snprintf(g_upd_self, sizeof(g_upd_self), "%s", exe);
        char old[1100];
        snprintf(old, sizeof(old), "%s.old", g_upd_self);
        const char *rmo[] = { "rm", "-rf", old, NULL };
        struct stat st;
        if (stat(old, &st) == 0) upd_run(rmo);
    } else {
        snprintf(g_upd.msg, sizeof(g_upd.msg), "当前未从 .app 中运行，请从 GitHub 下载更新");
        upd_set(UPD_UNSUPPORTED);
        return;
    }
#else
    struct stat st;
    if (stat("/.flatpak-info", &st) == 0) {
        snprintf(g_upd.msg, sizeof(g_upd.msg), "当前通过 Flatpak 安装，请从发布页下载新版 .flatpak");
        upd_set(UPD_UNSUPPORTED);
        return;
    }
    /* v0.7.17: an AppImage runs from a read-only mount - the file to replace
     * is the .AppImage itself */
    const char *appimage = getenv("APPIMAGE");
    if (appimage && appimage[0]) {
        snprintf(g_upd_self, sizeof(g_upd_self), "%s", appimage);
    } else {
        const ssize_t n = readlink("/proc/self/exe", g_upd_self, sizeof(g_upd_self) - 1);
        if (n <= 0) { snprintf(g_upd.msg, sizeof(g_upd.msg), "无法定位当前程序"); upd_set(UPD_UNSUPPORTED); return; }
        g_upd_self[n] = 0;
    }
    char next[1100];
    snprintf(next, sizeof(next), "%s.new", g_upd_self);
    remove(next);
#endif
    if (g_upd_check) upd_check();
}

/* --update-check / --update: the same, from a terminal (and the tests);
 * returns the exit code: 0 up to date or updated, 1 failed, 2 newer found
 * (check only) */
static int upd_cli(char **argv, int install)
{
    g_upd_check = 0;
    upd_init(argv);
    if (upd_state() == UPD_UNSUPPORTED) { fprintf(stderr, "update: %s\n", g_upd.msg); return 1; }
    upd_set(UPD_CHECKING);
    upd_check_main(NULL);
    if (upd_state() == UPD_LATEST) { printf("up to date (%s)\n", SFT_VERSION); return 0; }
    if (upd_state() != UPD_FOUND) { fprintf(stderr, "update: %s\n", g_upd.msg); return 1; }
    printf("%s is available (this is %s): %s\n", g_upd.tag, SFT_VERSION, g_upd.url);
    if (!install) { upd_tmp_clean(); return 2; }
    upd_set(UPD_GETTING);
    upd_get_main(NULL);
    if (upd_state() != UPD_READY) { fprintf(stderr, "update: %s\n", g_upd.msg); return 1; }
    printf("updated to %s: %s\n", g_upd.tag, g_upd_self);
    return 0;
}

/* one line for the menus: what the updater knows */
static const char *upd_status(char *b, size_t cap)
{
    if (g_upd_note_until && SDL_TICKS_PASSED(g_upd_note_until, SDL_GetTicks()) == 0) return g_upd_note;
    switch (upd_state()) {
    case UPD_CHECKING: return "正在检查 GitHub…";
    case UPD_LATEST: snprintf(b, cap, "已是最新版本（%s）", SFT_VERSION); return b;
    case UPD_FOUND: snprintf(b, cap, "可更新至 %s（当前为 %s）", g_upd.tag, SFT_VERSION); return b;
    case UPD_GETTING: snprintf(b, cap, "正在下载 %s…", g_upd.tag); return b;
    case UPD_READY: snprintf(b, cap, "已安装 %s，重启后生效", g_upd.tag); return b;
    case UPD_ERROR: case UPD_UNSUPPORTED: return g_upd.msg;
    default: snprintf(b, cap, "版本 %s", SFT_VERSION); return b;
    }
}
