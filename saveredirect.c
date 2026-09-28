// DISaveRedirect: Dead Island keeps its saves and profile in Steam Cloud (ISteamRemoteStorage004,
#define _CRT_SECURE_NO_WARNINGS
// Steam\userdata\<id>\91310\remote). This hands the engine a stand-in interface that keeps the same
// files in a folder of your choice instead, so modded saves never touch the Steam copies.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NSLOTS 22   // ISteamRemoteStorage004: FileWrite .. GetCachedUGCHandle

typedef struct { void **vt; } Iface;
typedef Iface *(__cdecl *SteamRemoteStorageFn)(void);

static SteamRemoteStorageFn origSRS;
static Iface *real;
static void *proxyVt[NSLOTS];
static Iface proxy = { proxyVt };
static char root[MAX_PATH];         // save folder, with trailing backslash
static int cfgImport, cfgLog;
static FILE *logf;
static CRITICAL_SECTION cs;
static int imported;

typedef struct { char name[260]; int size; } Entry;
static Entry *list;
static int listCount, listCap;

static void Log(const char *fmt, ...)
{
    va_list va;
    if (!logf) return;
    va_start(va, fmt);
    vfprintf(logf, fmt, va);
    va_end(va);
    fputc('\n', logf);
    fflush(logf);
}

static Iface *Real(void)
{
    if (!real) real = origSRS();
    return real;
}

// "out/save/save_0.sav" -> <root>out\save\save_0.sav; rejects anything that climbs out of root
static int MapPath(const char *name, char *out)
{
    char *p;
    if (!name || !*name || strstr(name, "..") || strchr(name, ':') || name[0] == '/' || name[0] == '\\') return 0;
    if (strlen(root) + strlen(name) >= MAX_PATH) return 0;
    strcpy(out, root);
    strcat(out, name);
    for (p = out; *p; p++) if (*p == '/') *p = '\\';
    return 1;
}

static void MakeDirs(const char *path)
{
    char tmp[MAX_PATH], *p;
    strcpy(tmp, path);
    for (p = tmp + 3; *p; p++) {
        if (*p != '\\') continue;
        *p = 0;
        CreateDirectoryA(tmp, NULL);
        *p = '\\';
    }
}

static int WriteLocal(const char *name, const void *data, int size)
{
    char path[MAX_PATH], tmp[MAX_PATH + 4];
    HANDLE h;
    DWORD done = 0;
    if (!MapPath(name, path) || size < 0) return 0;
    MakeDirs(path);
    sprintf(tmp, "%s.tmp", path);
    h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    WriteFile(h, data, size, &done, NULL);
    FlushFileBuffers(h);
    CloseHandle(h);
    if ((int)done != size || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp);
        return 0;
    }
    return 1;
}

static int SaveSize(const char *name)
{
    char path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!MapPath(name, path) || !GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) return -1;
    if (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return -1;
    return (int)fa.nFileSizeLow;
}

static void Scan(const char *rel)
{
    char pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    sprintf(pat, "%s%s*", root, rel);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char sub[MAX_PATH];
        size_t n;
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        sprintf(sub, "%s%s", rel, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            strcat(sub, "\\");
            Scan(sub);
            continue;
        }
        n = strlen(sub);
        if (n > 4 && !_stricmp(sub + n - 4, ".tmp")) continue;
        if (listCount == listCap) {
            listCap = listCap ? listCap * 2 : 32;
            list = (Entry *)realloc(list, listCap * sizeof(Entry));
        }
        strcpy(list[listCount].name, sub);
        for (n = 0; list[listCount].name[n]; n++) if (list[listCount].name[n] == '\\') list[listCount].name[n] = '/';
        list[listCount].size = (int)fd.nFileSizeLow;
        listCount++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// first use: when the folder holds nothing yet, copy the Steam Cloud files in (Steam's copies are left alone)
static void ImportOnce(void)
{
    Iface *r;
    int i, n;
    if (imported || !cfgImport) return;
    listCount = 0;
    Scan("");
    if (listCount) { imported = 1; Log("import skipped (folder not empty)"); return; }
    if (!(r = Real())) return;      // Steam not up yet, try again on the next call
    imported = 1;
    n = ((int(__fastcall *)(Iface *, void *))r->vt[11])(r, NULL);
    for (i = 0; i < n; i++) {
        int size = 0;
        const char *name = ((const char *(__fastcall *)(Iface *, void *, int, int *))r->vt[12])(r, NULL, i, &size);
        char *buf;
        int got;
        if (!name || size < 0) continue;
        buf = (char *)malloc(size ? size : 1);
        got = ((int(__fastcall *)(Iface *, void *, const char *, void *, int))r->vt[1])(r, NULL, name, buf, size);
        Log("import %s (%d bytes): %s", name, size, got == size && WriteLocal(name, buf, size) ? "ok" : "FAILED");
        free(buf);
    }
}

// ---- ISteamRemoteStorage004 stand-in (__thiscall written as __fastcall(this, edx, ...)) ----
static int __fastcall FileWrite(Iface *self, void *edx, const char *name, const void *data, int size)
{
    int ok;
    EnterCriticalSection(&cs);
    ImportOnce();
    ok = WriteLocal(name, data, size);
    LeaveCriticalSection(&cs);
    Log("FileWrite %s %d -> %d", name, size, ok);
    return ok;
}

static int __fastcall FileRead(Iface *self, void *edx, const char *name, void *data, int size)
{
    char path[MAX_PATH];
    HANDLE h;
    DWORD done = 0;
    EnterCriticalSection(&cs);
    ImportOnce();
    LeaveCriticalSection(&cs);
    if (!MapPath(name, path) || size <= 0) return 0;
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        ReadFile(h, data, size, &done, NULL);
        CloseHandle(h);
    }
    Log("FileRead %s %d -> %lu", name, size, done);
    return (int)done;
}

static int __fastcall FileForget(Iface *self, void *edx, const char *name) { return 1; }

static int __fastcall FileDelete(Iface *self, void *edx, const char *name)
{
    char path[MAX_PATH];
    int ok = MapPath(name, path) && DeleteFileA(path);
    Log("FileDelete %s -> %d", name, ok);
    return ok;
}

static unsigned __int64 __fastcall FileShare(Iface *self, void *edx, const char *name) { return 0; }
static int __fastcall SetSyncPlatforms(Iface *self, void *edx, const char *name, int platforms) { return 1; }

static int __fastcall FileExists(Iface *self, void *edx, const char *name)
{
    EnterCriticalSection(&cs);
    ImportOnce();
    LeaveCriticalSection(&cs);
    return SaveSize(name) >= 0;
}

static int __fastcall FilePersisted(Iface *self, void *edx, const char *name) { return SaveSize(name) >= 0; }

static int __fastcall GetFileSize_(Iface *self, void *edx, const char *name)
{
    int n = SaveSize(name);
    return n < 0 ? 0 : n;
}

static __int64 __fastcall GetFileTimestamp(Iface *self, void *edx, const char *name)
{
    char path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    ULONGLONG t;
    if (!MapPath(name, path) || !GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) return 0;
    t = ((ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
    return (__int64)(t / 10000000ULL - 11644473600ULL);
}

static int __fastcall GetSyncPlatforms(Iface *self, void *edx, const char *name) { return 1; }

static int __fastcall GetFileCount(Iface *self, void *edx)
{
    int n;
    EnterCriticalSection(&cs);
    ImportOnce();
    listCount = 0;
    Scan("");
    n = listCount;
    LeaveCriticalSection(&cs);
    return n;
}

static const char *__fastcall GetFileNameAndSize(Iface *self, void *edx, int i, int *size)
{
    if (i < 0 || i >= listCount) { if (size) *size = 0; return ""; }
    if (size) *size = list[i].size;
    return list[i].name;
}

static int __fastcall GetQuota(Iface *self, void *edx, int *total, int *avail)
{
    if (total) *total = 0x7FFFFFFF;
    if (avail) *avail = 0x7FFFFFFF;
    return 1;
}

static int __fastcall CloudOn(Iface *self, void *edx) { return 1; }
static void __fastcall SetCloudEnabledForApp(Iface *self, void *edx, int on) {}

// UGC slots (17-21) go to the real interface; the engine never calls them
#define FWD(n) static __declspec(naked) void Fwd##n(void) { __asm { mov ecx, real } __asm { mov eax, [ecx] } __asm { jmp dword ptr [eax + n * 4] } }
FWD(17) FWD(18) FWD(19) FWD(20) FWD(21)

// ---- BlockAchievements: ISteamUserStats010 stand-in, SetStat/SetAchievement dropped, the rest forwarded ----
#define NSTATS 40
typedef Iface *(__cdecl *SteamUserStatsFn)(void);
static SteamUserStatsFn origSUS;
static Iface *realStats;
static void *statsVt[NSTATS];
static Iface statsProxy = { statsVt };

static int __fastcall SetStatDropped(Iface *self, void *edx, const char *name, int value) { Log("blocked SetStat %s", name); return 1; }
static int __fastcall SetAchievementDropped(Iface *self, void *edx, const char *name) { Log("blocked SetAchievement %s", name); return 1; }

#define SFWD(n) static __declspec(naked) void SFwd##n(void) { __asm { mov ecx, realStats } __asm { mov eax, [ecx] } __asm { jmp dword ptr [eax + n * 4] } }
SFWD(0) SFWD(1) SFWD(2) SFWD(3) SFWD(4) SFWD(5) SFWD(6) SFWD(7) SFWD(8) SFWD(9)
SFWD(10) SFWD(11) SFWD(12) SFWD(13) SFWD(14) SFWD(15) SFWD(16) SFWD(17) SFWD(18) SFWD(19)
SFWD(20) SFWD(21) SFWD(22) SFWD(23) SFWD(24) SFWD(25) SFWD(26) SFWD(27) SFWD(28) SFWD(29)
SFWD(30) SFWD(31) SFWD(32) SFWD(33) SFWD(34) SFWD(35) SFWD(36) SFWD(37) SFWD(38) SFWD(39)
static void *statsFwd[NSTATS] = {
    SFwd0, SFwd1, SFwd2, SFwd3, SFwd4, SFwd5, SFwd6, SFwd7, SFwd8, SFwd9,
    SFwd10, SFwd11, SFwd12, SFwd13, SFwd14, SFwd15, SFwd16, SFwd17, SFwd18, SFwd19,
    SFwd20, SFwd21, SFwd22, SFwd23, SFwd24, SFwd25, SFwd26, SFwd27, SFwd28, SFwd29,
    SFwd30, SFwd31, SFwd32, SFwd33, SFwd34, SFwd35, SFwd36, SFwd37, SFwd38, SFwd39 };

static Iface *__cdecl HookSUS(void)
{
    if (!realStats) realStats = origSUS();
    return realStats ? &statsProxy : NULL;
}

static Iface *__cdecl HookSRS(void)
{
    Real();
    return &proxy;
}

static void HookIat(HMODULE mod, FARPROC from, void *to)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; imp->Name; imp++) {
        IMAGE_THUNK_DATA *t = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; t->u1.Function; t++) {
            DWORD old;
            if (t->u1.Function != (DWORD)from) continue;
            VirtualProtect(&t->u1.Function, 4, PAGE_READWRITE, &old);
            t->u1.Function = (DWORD)to;
            VirtualProtect(&t->u1.Function, 4, old, &old);
            Log("hooked import %p", from);
        }
    }
}

static void Init(HMODULE self)
{
    char ini[MAX_PATH], dir[MAX_PATH], game[MAX_PATH], *slash;
    HMODULE eng = GetModuleHandleA("engine_x86_rwdi.dll");
    HMODULE sa = GetModuleHandleA("steam_api.dll");

    GetModuleFileNameA(self, ini, MAX_PATH);
    slash = strrchr(ini, '.');
    if (slash) strcpy(slash, ".ini");
    GetPrivateProfileStringA("MAIN", "SaveDir", "", dir, MAX_PATH, ini);
    if ((slash = strstr(dir, "//")) != NULL) *slash = 0;       // trailing // comment
    for (slash = dir + strlen(dir); slash > dir && (slash[-1] == ' ' || slash[-1] == '\t'); ) *--slash = 0;
    cfgImport = GetPrivateProfileIntA("MAIN", "ImportSteamSaves", 1, ini);
    cfgLog = GetPrivateProfileIntA("DEBUG", "Log", 0, ini);
    if (cfgLog) {
        strcpy(game, ini);
        strcpy(strrchr(game, '.'), ".log");
        logf = fopen(game, "w");
    }
    if (GetPrivateProfileIntA("MAIN", "BlockAchievements", 0, ini) && eng && sa
        && (origSUS = (SteamUserStatsFn)GetProcAddress(sa, "SteamUserStats")) != NULL) {
        int i;
        for (i = 0; i < NSTATS; i++) statsVt[i] = statsFwd[i];
        statsVt[3] = SetStatDropped;        // SetStat(int32)
        statsVt[4] = SetStatDropped;        // SetStat(float)
        statsVt[7] = SetAchievementDropped;
        HookIat(eng, (FARPROC)origSUS, HookSUS);
    }
    if (!dir[0]) { Log("SaveDir empty: saves stay in Steam Cloud"); return; }

    // relative SaveDir = relative to the game folder
    GetModuleFileNameA(NULL, game, MAX_PATH);
    *(strrchr(game, '\\') + 1) = 0;
    if (dir[1] == ':' || (dir[0] == '\\' && dir[1] == '\\')) strcpy(root, dir);
    else sprintf(root, "%s%s", game, dir);
    for (slash = root; *slash; slash++) if (*slash == '/') *slash = '\\';
    if (root[strlen(root) - 1] != '\\') strcat(root, "\\");
    MakeDirs(root);
    Log("SaveDir = %s, ImportSteamSaves = %d", root, cfgImport);

    if (!eng || !sa || !(origSRS = (SteamRemoteStorageFn)GetProcAddress(sa, "SteamRemoteStorage"))) {
        Log("engine or steam_api not loaded - not hooked");
        return;
    }
    InitializeCriticalSection(&cs);
    proxyVt[0] = FileWrite;
    proxyVt[1] = FileRead;
    proxyVt[2] = FileForget;
    proxyVt[3] = FileDelete;
    proxyVt[4] = FileShare;
    proxyVt[5] = SetSyncPlatforms;
    proxyVt[6] = FileExists;
    proxyVt[7] = FilePersisted;
    proxyVt[8] = GetFileSize_;
    proxyVt[9] = GetFileTimestamp;
    proxyVt[10] = GetSyncPlatforms;
    proxyVt[11] = GetFileCount;
    proxyVt[12] = GetFileNameAndSize;
    proxyVt[13] = GetQuota;
    proxyVt[14] = CloudOn;
    proxyVt[15] = CloudOn;
    proxyVt[16] = SetCloudEnabledForApp;
    proxyVt[17] = Fwd17;
    proxyVt[18] = Fwd18;
    proxyVt[19] = Fwd19;
    proxyVt[20] = Fwd20;
    proxyVt[21] = Fwd21;
    HookIat(eng, (FARPROC)origSRS, HookSRS);
}

BOOL WINAPI DllMain(HMODULE mod, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        Init(mod);
    }
    return TRUE;
}
