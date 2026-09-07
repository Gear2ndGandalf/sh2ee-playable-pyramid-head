// SH2PlayableRed.cpp -- "Play As Pyramid Head" as an SH2Framework plugin.
//
// Everything the Cheat Engine group of the same name did, done from inside the
// game: <game>\PlayableRed.asi next to sh2pc.exe, loaded by the Enhanced
// Edition's own ASI loader (d3d8.dll, "Enable loading ASI plugins"), configured by
// <game>\plugins\PlayableRedMod\PlayableRed.ini (written by PlayableRedConfig.exe
// or by hand), re-read whenever that file changes.  The LoadPlugin/UnloadPlugin/
// Update exports are kept, so the same file would also serve as an SH2Framework
// plugin from plugins\; nothing needs that loader, and the file goes in ONE place.
//
// Two kinds of change, kept apart on purpose:
//   * PATCHES  -- bytes written into the game once (code hooks, .rdata floats);
//                 each remembers the original bytes and can be reverted, so a
//                 feature turned off in the .ini while the game runs really
//                 goes away;
//   * ENFORCED -- values in the game's live state (.data) that the game itself
//                 rewrites -- max health when a save loads, the game-over
//                 toggle -- written again on every tick of our own thread
//                 whenever they are found different.
//
// The hooks are __declspec(naked) x86 stubs that do nothing clever: they save
// the flags and registers, call a plain C function with what it needs, and put
// the answer where the game expects it.  The C functions are what the test
// program checks.  Addresses are sh2pc.exe RVAs; every code site is verified
// byte for byte before it is touched, and a site that does not match is refused
// and logged, never patched.

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "playablered_logic.h"

// ---------------------------------------------------------------------------
// paths, log
// ---------------------------------------------------------------------------
static HMODULE g_hModule = NULL;
static char g_dir[MAX_PATH];        // <game>\plugins\PlayableRedMod\  (with trailing backslash), from the game exe's folder
static char g_ini[MAX_PATH];
static char g_logPath[MAX_PATH];
static CRITICAL_SECTION g_logLock;

static volatile LONG g_logReady = 0;      // the lock and the path exist (set in DllMain, before anything can call Log)

static void Log(const char* fmt, ...)
{
    if (!g_logReady || !g_logPath[0]) return;
    EnterCriticalSection(&g_logLock);
    FILE* f = fopen(g_logPath, "a");
    if (f) {
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
        fputc('\n', f);
        fclose(f);
    }
    LeaveCriticalSection(&g_logLock);
}

// ---------------------------------------------------------------------------
// memory helpers
// ---------------------------------------------------------------------------
static uintptr_t g_base = 0;        // sh2pc.exe

// Code bytes change while the game runs (at boot, and whenever the .ini changes), so the
// process's other threads are held for each write, and the write waits while any of them
// is stopped inside the very bytes.  The thread list is taken once per Apply (a system-wide
// snapshot is slow); a thread born after it is not held, which is the usual compromise.
// Nothing that could take a lock (the log, the heap) runs while threads are held.
static DWORD g_threadIds[256];
static int g_threadCount = 0;

static void ListThreads()
{
    g_threadCount = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te; te.dwSize = sizeof te;
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    if (Thread32First(snap, &te)) do {
        if (te.th32OwnerProcessID == pid && te.th32ThreadID != me && g_threadCount < 256) g_threadIds[g_threadCount++] = te.th32ThreadID;
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
}

static bool WriteMem(uintptr_t addr, const void* src, size_t n)
{
    HANDLE held[256]; int nheld = 0;
    for (int attempt = 0; attempt < 50; ++attempt) {
        bool inside = false;
        nheld = 0;
        for (int i = 0; i < g_threadCount; ++i) {
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, g_threadIds[i]);
            if (!h) continue;
            if (SuspendThread(h) == (DWORD)-1) { CloseHandle(h); continue; }
            CONTEXT c; c.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(h, &c) && c.Eip >= addr && c.Eip < addr + n) inside = true;
            held[nheld++] = h;
        }
        if (!inside) break;
        for (int i = 0; i < nheld; ++i) { ResumeThread(held[i]); CloseHandle(held[i]); }
        nheld = 0;
        Sleep(1);
    }
    DWORD old = 0, back;
    bool ok = VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old) != 0;
    if (ok) {
        memcpy((void*)addr, src, n);
        VirtualProtect((void*)addr, n, old, &back);
        FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
    }
    DWORD err = ok ? 0 : GetLastError();
    for (int i = 0; i < nheld; ++i) { ResumeThread(held[i]); CloseHandle(held[i]); }
    if (!ok) Log("VirtualProtect failed at %08X (%lu)", (unsigned)addr, err);
    return ok;
}

static bool Expect(uintptr_t rva, const unsigned char* bytes, size_t n, const char* what)
{
    if (memcmp((void*)(g_base + rva), bytes, n) == 0) return true;
    char got[3 * 16 + 1] = {0};
    for (size_t i = 0; i < n && i < 16; ++i) sprintf(got + 3 * i, "%02X ", ((unsigned char*)(g_base + rva))[i]);
    Log("REFUSED %s: sh2pc.exe+%X holds %s-- not the bytes this build expects; the exe differs", what, (unsigned)rva, got);
    return false;
}

// A patch: some bytes at an RVA, with the originals kept for the way back.
struct Patch {
    const char* name;
    uintptr_t rva;
    unsigned char want[16];
    unsigned char orig[16];
    size_t n;
    bool applied;
};

static bool ApplyPatch(Patch& p)
{
    if (p.applied) return true;
    memcpy(p.orig, (void*)(g_base + p.rva), p.n);
    if (!WriteMem(g_base + p.rva, p.want, p.n)) return false;
    p.applied = true;
    return true;
}

static void RevertPatch(Patch& p)
{
    if (!p.applied) return;
    WriteMem(g_base + p.rva, p.orig, p.n);
    p.applied = false;
}

// A JMP hook: 5-byte jump plus NOPs over `len` bytes of original code.
static bool InstallJmp(Patch& p, void* target)
{
    unsigned char code[16];
    memset(code, 0x90, sizeof code);
    code[0] = 0xE9;
    int32_t rel = (int32_t)((uintptr_t)target - (g_base + p.rva + 5));
    memcpy(code + 1, &rel, 4);
    memcpy(p.want, code, p.n);
    return ApplyPatch(p);
}

// ---------------------------------------------------------------------------
// the game's addresses (RVAs into sh2pc.exe)
// ---------------------------------------------------------------------------
enum {
    RVA_SETTER          = 0x138CD0,   // clip setter: mov eax,[esp+8]; mov edx,[esp+4]
    RVA_BASIC_TABLE     = 0x2CE8DC,   // 33 x 12-byte clip descriptors, ids 101..133
    RVA_BASIC_END       = 0x2CEA68,
    RVA_WEAPON_BLOCK    = 0x2CEFE8,   // Great Knife's weapon clips, 12 bytes per slot, slot 1 = id 451
    RVA_ATTACK_TABLE    = 0x2EC384,   // 36-byte attack records; +0 damage float, +28 damage window
    RVA_WEAPON_BYTE     = 0x1B7A809,  // 0x0F while the Great Knife is equipped
    RVA_HYPER_ARMOUR    = 0x1358ED,   // jne -> je
    RVA_TORCH_BLUE      = 0x10A936,   // fst  dword [sh2pc.exe+542AB8]
    RVA_TORCH_GREEN     = 0x10A93C,   // fst  dword [sh2pc.exe+542AB4]
    RVA_TORCH_RED       = 0x10A942,   // fstp dword [sh2pc.exe+542AB0]
    RVA_TORCH_R         = 0x542AB0,
    RVA_TORCH_G         = 0x542AB4,
    RVA_TORCH_B         = 0x542AB8,
    RVA_FLOODLIGHT      = 0x7A612,    // fsub dword [esi+34]; add esp,18
    RVA_GLOW_A          = 0x2C3168,   // 0.5f
    RVA_GLOW_B          = 0x2C316C,   // 0.5f
    RVA_HEALTH          = 0x1BB113C,  // float, the player's health
    RVA_MAX_HEALTH      = 0x1BB1140,  // float
    RVA_FATIGUE         = 0x1BB80F4,  // float
    RVA_GAME_OVER       = 0x1BB8117,  // byte: 0 = alive, 2 = the game-over state
    RVA_ROOM_ID         = 0x1BB7DAC,  // dword; 1 = R_BEGIN_BATHROOM, where a brand new game begins
    RVA_CHAPTER_ID      = 0x19BC00C,  // byte; 0 = main scenario, 1 = Born From a Wish
    RVA_SHORT_TRAIL     = 0x4E700B,   // byte, the "Short Trail" cheat: 1 (stock 0x80)
    RVA_INV_KNIFE       = 0x1B7A7E1,  // byte 0x80
    RVA_INV_FLASHLIGHT  = 0x1B7A7E2,  // byte 0x34
    RVA_DIRECTIONAL_2D  = 0x19BC008,  // byte 1
    RVA_RUN_FLAG        = 0x1BB10A8,  // float >= 0.5 while running is asked for
    RVA_MARIA_DEATH     = 0x12CC95,   // test al,al / je: al = the byte below; nonzero writes the game-over byte -> test al,0 (the je always taken)
    RVA_MARIA_CONTROL   = 0x12A35F,   // mov cl,[1FB0F4C] after a call: hooked to write 0 there and read it back
    RVA_MARIA_FLAG      = 0x1BB0F4C,  // byte: 1 = the controls after Maria's death ("dysfunctional"), 0 = normal
    RVA_KNIFE_TIP_HOOK  = 0x1463BA,   // the weapon tip getter, far-point path: `mov dword [esp+12C],1.0` right after bone 1's translation is copied to [esp+10]
    RVA_SWEEP_HOOK      = 0x144049,   // the melee hit builder's `call 123240` (push this frame's hit primitive)
    RVA_PUSH_PRIMITIVE  = 0x123240,   // the game's push (cdecl, one pointer to 0x48 bytes)
    RVA_NO_TRIP         = 0x12F057,   // the gravity routine's `je +8E` that skips the stumble; flipped to jne (the "No Tripping" script)
    RVA_PLAYER_PTR      = 0x1BB7C80,  // dword: the player object (0x1FB1000 in the running game)
    RVA_FLOOR_Y         = 0x1BB7C98,  // float: the floor under the player (a probe from 250 above the feet, 1500 down)
    RVA_FLOOR_MATERIAL  = 0x1BB7CE8,  // dword: the floor's material (0x18 exempts the stumble; 0xFF right after a room load)
    RVA_MOVE_FLAGS      = 0x1BB7D00,  // dword: bit 0x1000000 = stumble asked for
    RVA_PROBE_HIT       = 0x1BB7E28,  // dword: 1 when the floor probe hit something
    RVA_PROBE_Y         = 0x1BB7E30,  // float: where
    RVA_AIRBORNE        = 0x1BB8118,  // byte: 1 while the player is falling
    RVA_WALK_ANIM_HOOK  = 0x1527C2    // movsx ecx,ax; mov [ebp+28],ax  (unique pattern)
};

static const unsigned char SETTER_ORIG[8]     = {0x8B,0x44,0x24,0x08,0x8B,0x54,0x24,0x04};
static const unsigned char HYPER_ORIG[6]      = {0x0F,0x85,0xCF,0x00,0x00,0x00};
static const unsigned char TORCH_B_ORIG[6]    = {0xD9,0x15,0xB8,0x2A,0x94,0x00};
static const unsigned char TORCH_G_ORIG[6]    = {0xD9,0x15,0xB4,0x2A,0x94,0x00};
static const unsigned char TORCH_R_ORIG[6]    = {0xD9,0x1D,0xB0,0x2A,0x94,0x00};
static const unsigned char FLOOD_ORIG[6]      = {0xD8,0x66,0x34,0x83,0xC4,0x18};
static const unsigned char SPEED_PATTERN[7]   = {0x89,0x8E,0xA8,0x00,0x00,0x00,0x57};   // mov [esi+A8],ecx; push edi
static const unsigned char WALK_PATTERN[10]   = {0x0F,0xBF,0xC8,0x66,0x89,0x45,0x28,0x01,0x4D,0x18};
static const unsigned char NOTRIP_ORIG[6]     = {0x0F,0x84,0x8E,0x00,0x00,0x00};
static const unsigned char MARIA_DEATH_ORIG[3]   = {0x84,0xC0,0x74};                     // test al,al; je
static const unsigned char MARIA_CONTROL_ORIG[6] = {0x8A,0x0D,0x4C,0x0F,0xFB,0x01};      // mov cl,[1FB0F4C]
static const unsigned char KNIFE_TIP_ORIG[11]    = {0xC7,0x84,0x24,0x2C,0x01,0x00,0x00,0x00,0x00,0x80,0x3F};   // mov dword [esp+12C],3F800000
static const unsigned char SWEEP_ORIG[5]         = {0xE8,0xF2,0xF1,0xFD,0xFF};          // call 523240

// The player object (Y grows downward: a smaller y is higher up).
enum { PL_POS_X = 0x1C, PL_POS_Y = 0x20, PL_POS_Z = 0x24, PL_VEL_Y = 0xB0, PL_FLOOR = 0xC8, PL_STORED_Y = 0xD0,
       PL_UPPER = 0x1A0, PL_LOWER = 0x220, CH_DESC = 0x38 };

// ---------------------------------------------------------------------------
// config
// ---------------------------------------------------------------------------
static Config g_cfg;
static Config g_live;               // what is currently applied
static bool g_haveLive = false;
static FILETIME g_iniTime = {0, 0};

static void WriteDefaultIni()
{
    FILE* f = fopen(g_ini, "w");
    if (!f) return;
    fputs(DefaultIniText(), f);
    fclose(f);
}

static bool IniChanged()
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(g_ini, GetFileExInfoStandard, &a)) return false;
    if (CompareFileTime(&a.ftLastWriteTime, &g_iniTime) != 0) {
        g_iniTime = a.ftLastWriteTime;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// feature: Animation Translation (LL)
// ---------------------------------------------------------------------------
static unsigned char g_gkbasic[33 * 12 + 4];    // our copy of the basic table, GK-only
static Patch g_setterHook = {"animation: clip setter hook", RVA_SETTER, {0}, {0}, 8, false};
static Patch g_weaponSlots[16];
static Patch g_windows[4];
static uintptr_t g_setterReturn = 0;

// C side of the setter hook: given the 4th argument (the descriptor), return the one to use.
static uintptr_t __stdcall SetterDescriptor(uintptr_t desc)
{
    return SubstituteDescriptor(desc, *(const unsigned char*)(g_base + RVA_WEAPON_BYTE),
                                g_base + RVA_BASIC_TABLE, g_base + RVA_BASIC_END, (uintptr_t)g_gkbasic);
}

__declspec(naked) static void SetterHookStub()
{
    __asm {
        pushfd
        pushad
        mov eax, [esp + 0x24 + 0x10]     // 4th argument: 9 dwords pushed (flags + 8 regs) above esp+4
        push eax
        call SetterDescriptor
        mov [esp + 0x24 + 0x10], eax
        popad
        popfd
        mov eax, [esp + 8]                // the two instructions the jump replaced
        mov edx, [esp + 4]
        jmp g_setterReturn
    }
}

static bool AnimationOn()
{
    if (!Expect(RVA_SETTER, SETTER_ORIG, 8, "animation: clip setter")) return false;
    BuildGreatKnifeBasicTable(g_gkbasic, (const unsigned char*)(g_base + RVA_BASIC_TABLE));
    g_setterReturn = g_base + RVA_SETTER + 8;
    if (!InstallJmp(g_setterHook, (void*)SetterHookStub)) return false;
    // the Great Knife's own weapon slots 1..16 and its four damage windows: written in place
    for (int slot = 1; slot <= 16; ++slot) {
        Patch& p = g_weaponSlots[slot - 1];
        p.name = "animation: weapon slot"; p.rva = RVA_WEAPON_BLOCK + 12 * slot + 2; p.n = 8; p.applied = false;
        int16_t stockRate = *(int16_t*)(g_base + RVA_WEAPON_BLOCK + 12 * slot + 4);
        WeaponSlotRecord(slot, stockRate, p.want);
        ApplyPatch(p);
    }
    static const int recs[4] = {19, 20, 21, 22};
    for (int i = 0; i < 4; ++i) {
        Patch& p = g_windows[i];
        p.name = "animation: damage window"; p.rva = RVA_ATTACK_TABLE + 36 * recs[i] + 28; p.n = 2; p.applied = false;
        DamageWindow(recs[i], p.want);
        ApplyPatch(p);
    }
    Log("animation translation ON: setter hooked at +%X, basic copy at %p, 16 weapon slots and 4 windows written", RVA_SETTER, g_gkbasic);
    return true;
}

static void AnimationOff()
{
    RevertPatch(g_setterHook);
    for (int i = 0; i < 16; ++i) RevertPatch(g_weaponSlots[i]);
    for (int i = 0; i < 4; ++i) RevertPatch(g_windows[i]);
    Log("animation translation OFF (a clip already running keeps its record until the next state change)");
}

// ---------------------------------------------------------------------------
// feature: damage floats (.rdata, patched, revertible)
// ---------------------------------------------------------------------------
static Patch g_light[2]  = {{"light damage", RVA_ATTACK_TABLE + 36 * 19, {0}, {0}, 4, false}, {"light damage", RVA_ATTACK_TABLE + 36 * 20, {0}, {0}, 4, false}};
static Patch g_heavy[2]  = {{"heavy damage", RVA_ATTACK_TABLE + 36 * 21, {0}, {0}, 4, false}, {"heavy damage", RVA_ATTACK_TABLE + 36 * 22, {0}, {0}, 4, false}};
static Patch g_glow[2]   = {{"glow", RVA_GLOW_A, {0}, {0}, 4, false}, {"glow", RVA_GLOW_B, {0}, {0}, 4, false}};

static void FloatPatchOn(Patch* p, int count, float value)
{
    for (int i = 0; i < count; ++i) {
        if (p[i].applied) RevertPatch(p[i]);
        memcpy(p[i].want, &value, 4);
        ApplyPatch(p[i]);
    }
}
static void FloatPatchOff(Patch* p, int count) { for (int i = 0; i < count; ++i) RevertPatch(p[i]); }

// ---------------------------------------------------------------------------
// feature: hyper armour (one branch flipped)
// ---------------------------------------------------------------------------
static Patch g_hyper = {"hyper armour", RVA_HYPER_ARMOUR, {0x0F,0x84,0xCF,0x00,0x00,0x00}, {0}, 6, false};

// ---------------------------------------------------------------------------
// always on: no tripping (one branch flipped) -- the stumble the gravity routine
// asks for when the floor is found more than 250 units below the feet, which a
// room with stairs triggers on load with this model.  Same flip as the Cheat
// Engine script: je -> jne, target unchanged.
// ---------------------------------------------------------------------------
static Patch g_noTrip = {"no tripping", RVA_NO_TRIP, {0x0F,0x85,0x8E,0x00,0x00,0x00}, {0}, 6, false};

// ---------------------------------------------------------------------------
// feature: immortal Maria (the "Maria Immortal" script) -- her death no longer
// reaches the game-over write, and the byte that leaves James with the
// after-her-death controls is forced to 0 where the game reads it.
// ---------------------------------------------------------------------------
static Patch g_mariaDeath = {"immortal Maria: death", RVA_MARIA_DEATH, {0xA8,0x00}, {0}, 2, false};   // test al,0
static Patch g_hookMaria  = {"immortal Maria: controls", RVA_MARIA_CONTROL, {0}, {0}, 6, false};
static uintptr_t g_mariaFlagAddr, g_retMaria;

__declspec(naked) static void MariaControlStub()
{
    __asm {
        push eax
        mov eax, g_mariaFlagAddr
        mov byte ptr [eax], 0          // normal controls (the script's mov bl,0 / mov [flag],bl)
        pop eax
        mov cl, 0                      // what the game's mov cl,[flag] now reads; mov leaves the flags alone
        jmp g_retMaria
    }
}

static bool MariaOn()
{
    if (g_hookMaria.applied) return true;
    if (!Expect(RVA_MARIA_DEATH, MARIA_DEATH_ORIG, 3, "immortal Maria: death") || !Expect(RVA_MARIA_CONTROL, MARIA_CONTROL_ORIG, 6, "immortal Maria: controls")) return false;
    g_mariaFlagAddr = g_base + RVA_MARIA_FLAG;
    g_retMaria = g_base + RVA_MARIA_CONTROL + 6;
    if (!ApplyPatch(g_mariaDeath)) return false;
    if (!InstallJmp(g_hookMaria, (void*)MariaControlStub)) { RevertPatch(g_mariaDeath); return false; }
    Log("immortal Maria ON: +12CC95 test al,al -> test al,0; controls hook at +12A35F");
    return true;
}
static void MariaOff() { RevertPatch(g_hookMaria); RevertPatch(g_mariaDeath); }

// ---------------------------------------------------------------------------
// feature: the Great Knife's hit line.  (a) Its tip marker follows the drawn
// blade: the getter that turns bone 0 -> bone 1 into the blade line is hooked
// where bone 1's translation sits on its stack, and the stock marker becomes
// the turned one.  (b) The light attack's sweep is widened: the primitive the
// builder pushes goes in as a copy with its two far points turned apart.
// ---------------------------------------------------------------------------
static Patch g_hookKnifeTip = {"knife tip marker", RVA_KNIFE_TIP_HOOK, {0}, {0}, 11, false};
static Patch g_hookSweep    = {"light attack sweep", RVA_SWEEP_HOOK, {0}, {0}, 5, false};
static uintptr_t g_retKnifeTip, g_retSweep, g_pushPrimitive;
static bool g_tipLogged = false, g_widenLogged = false;
static unsigned char g_primCopy[0x48];

static void __stdcall KnifeTipHook(float* t1)
{
    if (!g_live.knifeHitFollowsBlade) return;
    if (KnifeTipFollow(t1, *(const unsigned char*)(g_base + RVA_WEAPON_BYTE)) && !g_tipLogged) {
        g_tipLogged = true;
        Log("Great Knife tip marker (%g, %g, %g) -> (%g, %g, %g): the hit line now runs along the drawn blade", KNIFE_TIP_STOCK[0], KNIFE_TIP_STOCK[1], KNIFE_TIP_STOCK[2], t1[0], t1[1], t1[2]);
    }
}
__declspec(naked) static void KnifeTipStub()
{
    __asm {
        mov dword ptr [esp + 0x12C], 0x3F800000   // what the game did: bone 1's matrix copy gets a unit w in its translation row
        pushfd
        push eax
        push ecx
        push edx
        lea eax, [esp + 0x20]                    // bone 1's translation: [esp+10] at the site, four pushes ago
        push eax
        call KnifeTipHook
        pop edx
        pop ecx
        pop eax
        popfd
        jmp g_retKnifeTip
    }
}

// The pointer to push: a widened copy for the light attack, else the game's own primitive.
static const unsigned char* __stdcall WidenPrimitive(const unsigned char* prim)
{
    float deg = g_live.lightWiden;
    int id = (int)*(const uint16_t*)(prim + 2) - 0x100;
    if (!(deg > 0.0f) || (id != 19 && id != 20)) return prim;
    memcpy(g_primCopy, prim, sizeof g_primCopy);
    WidenSweep((const float*)(g_primCopy + 8), (float*)(g_primCopy + 0x18), (float*)(g_primCopy + 0x38), deg);
    if (!g_widenLogged) { g_widenLogged = true; Log("light attack %d: sweep widened %g degrees each side", id, deg); }
    return g_primCopy;
}
__declspec(naked) static void SweepStub()
{
    __asm {
        mov eax, [esp]                 // the game's primitive, pushed for the call we replaced
        push eax
        call WidenPrimitive            // eax = the primitive to push
        push eax
        call g_pushPrimitive           // the game's own push (cdecl)
        add esp, 4
        jmp g_retSweep                 // the caller's add esp,4 pops the game's argument
    }
}

static bool KnifeTipOn()
{
    if (g_hookKnifeTip.applied) return true;
    if (!Expect(RVA_KNIFE_TIP_HOOK, KNIFE_TIP_ORIG, 11, "knife tip marker")) return false;
    g_retKnifeTip = g_base + RVA_KNIFE_TIP_HOOK + 11;
    g_tipLogged = false;
    return InstallJmp(g_hookKnifeTip, (void*)KnifeTipStub);
}
static void KnifeTipOff() { RevertPatch(g_hookKnifeTip); }
static bool SweepOn()
{
    if (g_hookSweep.applied) return true;
    if (!Expect(RVA_SWEEP_HOOK, SWEEP_ORIG, 5, "light attack sweep")) return false;
    g_pushPrimitive = g_base + RVA_PUSH_PRIMITIVE;
    g_retSweep = g_base + RVA_SWEEP_HOOK + 5;
    g_widenLogged = false;
    return InstallJmp(g_hookSweep, (void*)SweepStub);
}
static void SweepOff() { RevertPatch(g_hookSweep); }

// ---------------------------------------------------------------------------
// feature: flashlight colour (three stores, each followed by our overwrite)
// ---------------------------------------------------------------------------
static float g_torchR = 127.0f, g_torchG = 0.000999451f, g_torchB = 0.000999451f;
static uintptr_t g_torchAddrR, g_torchAddrG, g_torchAddrB;
static uintptr_t g_retTorchR, g_retTorchG, g_retTorchB;
static Patch g_hookTorchB = {"flashlight blue", RVA_TORCH_BLUE, {0}, {0}, 6, false};
static Patch g_hookTorchG = {"flashlight green", RVA_TORCH_GREEN, {0}, {0}, 6, false};
static Patch g_hookTorchR = {"flashlight red", RVA_TORCH_RED, {0}, {0}, 6, false};

__declspec(naked) static void TorchBlueStub()
{
    __asm {
        push eax
        mov eax, g_torchAddrB
        fst dword ptr [eax]            // what the game did
        push ecx
        mov ecx, dword ptr g_torchB
        mov [eax], ecx                 // what we want instead
        pop ecx
        pop eax
        jmp g_retTorchB
    }
}
__declspec(naked) static void TorchGreenStub()
{
    __asm {
        push eax
        mov eax, g_torchAddrG
        fst dword ptr [eax]
        push ecx
        mov ecx, dword ptr g_torchG
        mov [eax], ecx
        pop ecx
        pop eax
        jmp g_retTorchG
    }
}
__declspec(naked) static void TorchRedStub()
{
    __asm {
        push eax
        mov eax, g_torchAddrR
        fstp dword ptr [eax]
        push ecx
        mov ecx, dword ptr g_torchR
        mov [eax], ecx
        pop ecx
        pop eax
        jmp g_retTorchR
    }
}

static bool TorchOn(float r, float g, float b)
{
    g_torchR = r; g_torchG = g; g_torchB = b;
    if (g_hookTorchR.applied) return true;
    if (!Expect(RVA_TORCH_BLUE, TORCH_B_ORIG, 6, "flashlight blue") || !Expect(RVA_TORCH_GREEN, TORCH_G_ORIG, 6, "flashlight green")
        || !Expect(RVA_TORCH_RED, TORCH_R_ORIG, 6, "flashlight red")) return false;
    g_torchAddrR = g_base + RVA_TORCH_R; g_torchAddrG = g_base + RVA_TORCH_G; g_torchAddrB = g_base + RVA_TORCH_B;
    g_retTorchB = g_base + RVA_TORCH_BLUE + 6; g_retTorchG = g_base + RVA_TORCH_GREEN + 6; g_retTorchR = g_base + RVA_TORCH_RED + 6;
    return InstallJmp(g_hookTorchB, (void*)TorchBlueStub) && InstallJmp(g_hookTorchG, (void*)TorchGreenStub) && InstallJmp(g_hookTorchR, (void*)TorchRedStub);
}
static void TorchOff() { RevertPatch(g_hookTorchB); RevertPatch(g_hookTorchG); RevertPatch(g_hookTorchR); }

// ---------------------------------------------------------------------------
// feature: floodlight (the beam's size)
// ---------------------------------------------------------------------------
static float g_floodSize = 1.1f;
static uintptr_t g_retFlood;
static Patch g_hookFlood = {"floodlight", RVA_FLOODLIGHT, {0}, {0}, 6, false};

__declspec(naked) static void FloodStub()
{
    __asm {
        fld dword ptr [g_floodSize]    // in place of fsub dword ptr [esi+34]
        add esp, 0x18
        jmp g_retFlood
    }
}
static bool FloodOn(float size)
{
    g_floodSize = size;
    if (g_hookFlood.applied) return true;
    if (!Expect(RVA_FLOODLIGHT, FLOOD_ORIG, 6, "floodlight")) return false;
    g_retFlood = g_base + RVA_FLOODLIGHT + 6;
    return InstallJmp(g_hookFlood, (void*)FloodStub);
}
static void FloodOff() { RevertPatch(g_hookFlood); }

// ---------------------------------------------------------------------------
// feature: toggle run -- two hooks that only act while g_runMode is on
// ---------------------------------------------------------------------------
static volatile LONG g_runMode = 0;
static float g_runSpeed = 4.0f;
static int16_t g_runStep = 3000;
static uintptr_t g_runFlagAddr, g_retSpeed, g_retWalk;
static Patch g_hookSpeed = {"run: travel speed", 0, {0}, {0}, 6, false};
static Patch g_hookWalk  = {"run: walk animation step", RVA_WALK_ANIM_HOOK, {0}, {0}, 7, false};

// C side: the step to write, or -1 to keep the game's own.  charId: [ebp-210] (u32); runFlag: the game's float.
static int __stdcall RunStepFor(uint32_t charId, float runFlag)
{
    return RunStep(g_runMode != 0, charId, runFlag, g_runStep);
}

__declspec(naked) static void SpeedStub()
{
    __asm {
        pushfd                           // cmp must not leak into the game's flags
        cmp dword ptr g_runMode, 0
        je keep
        push eax
        mov eax, dword ptr g_runSpeed
        mov [esi + 0xA8], eax
        pop eax
        popfd
        jmp g_retSpeed
    keep:
        popfd
        mov [esi + 0xA8], ecx            // the instruction the jump replaced
        jmp g_retSpeed
    }
}

__declspec(naked) static void WalkStub()
{
    __asm {
        pushfd
        pushad
        mov ecx, g_runFlagAddr
        push dword ptr [ecx]             // the run flag's bits, as a float argument
        push dword ptr [ebp - 0x210]     // the character id
        call RunStepFor
        cmp eax, 0
        jl keep
        mov word ptr [esp + 0x1C], ax    // pushad's eax slot: only ax becomes our step, as `mov ax,#3000` did
        popad
        popfd
        movsx ecx, ax
        mov [ebp + 0x28], ax
        jmp g_retWalk
    keep:
        popad
        popfd
        movsx ecx, ax
        mov [ebp + 0x28], ax
        jmp g_retWalk
    }
}

static uintptr_t FindFirst(const unsigned char* pat, size_t n)
{
    // .text of sh2pc.exe: RVA 0x1000 .. 0x229000
    const unsigned char* lo = (const unsigned char*)(g_base + 0x1000);
    const unsigned char* hi = (const unsigned char*)(g_base + 0x229000) - n;
    for (const unsigned char* p = lo; p <= hi; ++p)
        if (p[0] == pat[0] && memcmp(p, pat, n) == 0) return (uintptr_t)p - g_base;
    return 0;
}

static bool RunOn(float speed, int step, bool startOn)
{
    g_runSpeed = speed; g_runStep = (int16_t)step;
    if (g_hookWalk.applied) return true;
    uintptr_t speedRva = FindFirst(SPEED_PATTERN, sizeof SPEED_PATTERN);   // first match, as Cheat Engine's aobscanmodule takes it
    if (!speedRva) { Log("REFUSED run: travel-speed pattern not found"); return false; }
    if (!Expect(RVA_WALK_ANIM_HOOK, WALK_PATTERN, 10, "run: walk animation")) return false;
    g_hookSpeed.rva = speedRva;
    g_runFlagAddr = g_base + RVA_RUN_FLAG;
    g_retSpeed = g_base + speedRva + 6;
    g_retWalk = g_base + RVA_WALK_ANIM_HOOK + 7;
    InterlockedExchange(&g_runMode, startOn ? 1 : 0);
    bool ok = InstallJmp(g_hookSpeed, (void*)SpeedStub) && InstallJmp(g_hookWalk, (void*)WalkStub);
    Log("toggle run %s: speed hook at +%X, walk hook at +%X, run mode %s", ok ? "ON" : "FAILED", (unsigned)speedRva, RVA_WALK_ANIM_HOOK, startOn ? "on" : "off");
    return ok;
}
static void RunOff() { RevertPatch(g_hookSpeed); RevertPatch(g_hookWalk); InterlockedExchange(&g_runMode, 0); }

// input for the toggle: keyboard edge, or an XInput button edge (Xbox X, PlayStation Square through Xidi/Steam)
typedef DWORD (WINAPI* pfnXInputGetState)(DWORD, XINPUT_STATE_MIN*);
static pfnXInputGetState g_XInputGetState = NULL;
static HMODULE g_xinput = NULL;

static void LoadXInput()
{
    const char* names[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (int i = 0; i < 3 && !g_xinput; ++i) g_xinput = LoadLibraryA(names[i]);
    if (g_xinput) g_XInputGetState = (pfnXInputGetState)GetProcAddress(g_xinput, "XInputGetState");
    Log("XInput: %s", g_XInputGetState ? "available" : "not available (keyboard toggle only)");
}

static bool g_keyWas = false, g_padWas = false;
static uint32_t g_padRetry[4] = {0, 0, 0, 0};   // an absent pad is asked again only after this tick count

static void PollToggle()
{
    bool key = (GetAsyncKeyState(g_live.runKey) & 0x8000) != 0;
    bool pad = false;
    if (g_XInputGetState) {
        // XInputGetState is slow for a pad that is not there; ask those only every two seconds
        uint32_t now = GetTickCount();
        for (DWORD i = 0; i < 4 && !pad; ++i) {
            if ((int32_t)(now - g_padRetry[i]) < 0) continue;
            XINPUT_STATE_MIN st;
            DWORD r = g_XInputGetState(i, &st);
            if (r == 0) { if (st.Gamepad.wButtons & g_live.runPadButton) pad = true; }
            else g_padRetry[i] = now + 2000;
        }
    }
    bool toggled = false;
    if (key && !g_keyWas) toggled = true;
    if (pad && !g_padWas) toggled = true;
    g_keyWas = key; g_padWas = pad;
    if (toggled) {
        LONG now = InterlockedExchange(&g_runMode, g_runMode ? 0 : 1);
        Log("run mode -> %s", now ? "off" : "on");
    }
}

// ---------------------------------------------------------------------------
// enforced values (live .data the game rewrites)
// ---------------------------------------------------------------------------
// The log names the first three writes of each value and then every 500th: the game
// rewrites fatigue on every frame of a walk, which was drowning the room trace.
static bool LogThisWrite(uintptr_t rva, unsigned* n)
{
    static struct { uintptr_t rva; unsigned n; } counts[16];
    for (int i = 0; i < 16; ++i) {
        if (counts[i].rva == rva || counts[i].rva == 0) {
            counts[i].rva = rva; *n = ++counts[i].n;
            return *n <= 3 || (*n % 500) == 0;
        }
    }
    *n = 0; return true;
}
static void EnforceFloat(uintptr_t rva, float v, const char* what)
{
    float* p = (float*)(g_base + rva);
    unsigned n;
    if (*p != v) { *p = v; if (LogThisWrite(rva, &n)) Log(n > 3 ? "enforced %s = %g (write %u)" : "enforced %s = %g", what, v, n); }
}
static void EnforceByte(uintptr_t rva, unsigned char v, const char* what)
{
    unsigned char* p = (unsigned char*)(g_base + rva);
    unsigned n;
    if (*p != v) { *p = v; if (LogThisWrite(rva, &n)) Log(n > 3 ? "enforced %s = %02X (write %u)" : "enforced %s = %02X", what, v, n); }
}
// An item's bit in the inventory flags: set when clear, the rest of the byte (other items) untouched.
static void EnsureBits(uintptr_t rva, unsigned char mask, const char* what)
{
    unsigned char* p = (unsigned char*)(g_base + rva);
    if ((*p & mask) != mask) { unsigned char was = *p; *p |= mask; Log("%s added to the inventory (byte %02X -> %02X)", what, was, *p); }
}

// ---------------------------------------------------------------------------
// always on: the door table, and a trace of what the player does around a room
// change -- every change, and every vertical jump of more than 500 units in one
// tick, is logged with position, floor, probe and clips, so a door that leaves
// the player a storey up can be seen and added to the table.
// ---------------------------------------------------------------------------
struct PlayerState {
    uint32_t room; float x, y, z, floor, vel; uint32_t flags, material; unsigned char air; uint32_t hit; float probeY; int lower, upper;
};

// A clip descriptor's id when the pointer looks like one (in the exe image or in our copy table); else -1.
static int DescriptorId(uintptr_t p)
{
    if ((p >= g_base + 0x1000 && p + 12 <= g_base + 0x400000) || (p >= (uintptr_t)g_gkbasic && p + 12 <= (uintptr_t)g_gkbasic + sizeof g_gkbasic))
        return *(const uint16_t*)p;
    return -1;
}

static bool ReadPlayerState(PlayerState* s)
{
    uintptr_t pl = *(const uintptr_t*)(g_base + RVA_PLAYER_PTR);
    if (!pl) return false;
    __try {
        s->room = *(const uint32_t*)(g_base + RVA_ROOM_ID);
        s->x = *(const float*)(pl + PL_POS_X); s->y = *(const float*)(pl + PL_POS_Y); s->z = *(const float*)(pl + PL_POS_Z);
        s->vel = *(const float*)(pl + PL_VEL_Y);
        s->floor = *(const float*)(g_base + RVA_FLOOR_Y);
        s->flags = *(const uint32_t*)(g_base + RVA_MOVE_FLAGS);
        s->material = *(const uint32_t*)(g_base + RVA_FLOOR_MATERIAL);
        s->air = *(const unsigned char*)(g_base + RVA_AIRBORNE);
        s->hit = *(const uint32_t*)(g_base + RVA_PROBE_HIT);
        s->probeY = *(const float*)(g_base + RVA_PROBE_Y);
        s->lower = DescriptorId(*(const uintptr_t*)(pl + PL_LOWER + CH_DESC));
        s->upper = DescriptorId(*(const uintptr_t*)(pl + PL_UPPER + CH_DESC));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

static const char* StateLine(const PlayerState& s, char* buf, size_t n)
{
    _snprintf(buf, n, "room %X pos (%g, %g, %g) floor %g vel %g flags %08X air %d material %X probe %u/%g clips lower %d upper %d",
              s.room, s.x, s.y, s.z, s.floor, s.vel, s.flags, s.air, s.material, s.hit, s.probeY, s.lower, s.upper);
    buf[n - 1] = 0;
    return buf;
}

static bool g_haveLast = false;
static PlayerState g_last;
static const DoorFix* g_doorFix = NULL;    // the entry armed by the last room change, while its window is open
static uint32_t g_doorFixUntil = 0;
enum { DOOR_FIX_WINDOW_MS = 3000, JUMP_LOG_UNITS = 500 };

static void RoomTick()
{
    PlayerState s;
    if (!ReadPlayerState(&s)) { g_haveLast = false; return; }
    char line[256];
    uint32_t now = GetTickCount();
    if (g_haveLast) {
        if (s.room != g_last.room) {
            Log("room %X -> %X: %s", g_last.room, s.room, StateLine(s, line, sizeof line));
            // The floor height is kept across a room change, and a door that lands the
            // player on another storey (Blue Creek 0x20 -> 0x26: placed at y 0, the old
            // floor -1705) leaves the gravity routine 'correcting' them up to the old
            // height when its first probe in the new room finds nothing.  So the floor
            // follows the player here, as the game's own first placement does.
            if (s.floor != s.y) {
                uintptr_t pl = *(const uintptr_t*)(g_base + RVA_PLAYER_PTR);
                *(float*)(pl + PL_FLOOR) = s.y;
                *(float*)(g_base + RVA_FLOOR_Y) = s.y;
                Log("room %X: floor %g -> %g, following the player", s.room, s.floor, s.y);
                s.floor = s.y;
            }
            g_doorFix = DoorFixFor(g_last.room, s.room);
            g_doorFixUntil = g_doorFix ? now + DOOR_FIX_WINDOW_MS : 0;
            if (g_doorFix) Log("door table: %X -> %X armed for %d s (floor %g)", g_doorFix->from, g_doorFix->to, DOOR_FIX_WINDOW_MS / 1000, g_doorFix->floorY);
        }
        float dy = s.y - g_last.y;
        if (dy > JUMP_LOG_UNITS || dy < -JUMP_LOG_UNITS)
            Log("player y %g -> %g in one tick (%s %g): %s", g_last.y, s.y, dy < 0 ? "up" : "down", dy < 0 ? -dy : dy, StateLine(s, line, sizeof line));
    }
    if (g_doorFixUntil && (int32_t)(now - g_doorFixUntil) >= 0) { g_doorFixUntil = 0; g_doorFix = NULL; }
    if (g_doorFix && DoorFixDue(g_doorFix, s.y)) {
        uintptr_t pl = *(const uintptr_t*)(g_base + RVA_PLAYER_PTR);
        float fy = g_doorFix->floorY;
        *(float*)(pl + PL_POS_Y) = fy;
        *(float*)(pl + PL_STORED_Y) = fy;
        *(float*)(pl + PL_FLOOR) = fy;
        *(float*)(pl + PL_VEL_Y) = 0.0f;
        *(float*)(g_base + RVA_FLOOR_Y) = fy;
        Log("door table %X -> %X: the player was at y %g, more than %g above the floor %g: put on the floor", g_doorFix->from, g_doorFix->to, s.y, g_doorFix->aboveBy, fy);
        s.y = fy;
    }
    g_last = s; g_haveLast = true;
}

static bool g_healArmed = true;     // the new-game full heal is ready
static uint32_t g_healUntil = 0;    // the heal window's deadline (GetTickCount), 0 = none
enum { HEAL_ROOM = 1, HEAL_WINDOW_MS = 3000 };   // the starting bathroom; three seconds

static void EnforceTick()
{
    const Config& c = g_live;
    if (c.maxHealth > 0.0f) {
        EnforceFloat(RVA_MAX_HEALTH, c.maxHealth, "max health");
        // a brand new game arrives in room 1 with the save's default health; hold it at the max for its first seconds
        if (NewGameHeal(c.maxHealth, *(const uint32_t*)(g_base + RVA_ROOM_ID), HEAL_ROOM, HEAL_WINDOW_MS, GetTickCount(), &g_healArmed, &g_healUntil)) {
            float* h = (float*)(g_base + RVA_HEALTH);
            if (*h != c.maxHealth) { *h = c.maxHealth; Log("new game (room %d): health set to the max health %g", HEAL_ROOM, c.maxHealth); }
        }
    } else {
        g_healUntil = 0;
    }
    if (c.fatigueZero) EnforceFloat(RVA_FATIGUE, 0.0f, "fatigue");
    if (c.hyperArmour && c.allowGameOver && GameOverDue(*(const float*)(g_base + RVA_HEALTH), *(const unsigned char*)(g_base + RVA_GAME_OVER)))
        EnforceByte(RVA_GAME_OVER, 2, "game-over state (health reached 0)");
    if (c.shortTrail) EnforceByte(RVA_SHORT_TRAIL, 1, "short trail");
    // the items: verified and added everywhere but the weight-limit elevator room, where the player must shelve them
    static bool inventoryHeld = false;
    bool enforceItems = InventoryEnforcedIn(*(const uint32_t*)(g_base + RVA_ROOM_ID));
    if (enforceItems == inventoryHeld) { inventoryHeld = !enforceItems; Log(enforceItems ? "inventory items enforced again" : "room 0x9D: inventory items left alone (the weight-limit elevator)"); }
    if (enforceItems && c.knifeInInventory) EnsureBits(RVA_INV_KNIFE, 0x80, "Great Knife");          // inventory flag bit 15
    if (enforceItems && c.flashlightInInventory) EnsureBits(RVA_INV_FLASHLIGHT, 0x04, "flashlight"); // inventory flag bit 18
    if (c.directional2D) EnforceByte(RVA_DIRECTIONAL_2D, 1, "2D directional");
    if (c.roomFixes) RoomTick(); else g_haveLast = false;
}

// ---------------------------------------------------------------------------
// applying a config: turn each feature on or off as it differs from what is live
// ---------------------------------------------------------------------------
static CRITICAL_SECTION g_applyLock;   // one Apply at a time: the worker's and UnloadPlugin's revert never interleave

static void Apply(const Config& c)
{
    EnterCriticalSection(&g_applyLock);
    ListThreads();
    const Config* was = g_haveLive ? &g_live : NULL;
#define CHANGED(field) (!was || was->field != c.field)
    if (CHANGED(animation)) { if (c.animation) AnimationOn(); else AnimationOff(); }
    if (CHANGED(lightDamage)) { if (c.lightDamage > 0) FloatPatchOn(g_light, 2, c.lightDamage); else FloatPatchOff(g_light, 2); }
    if (CHANGED(heavyDamage)) { if (c.heavyDamage > 0) FloatPatchOn(g_heavy, 2, c.heavyDamage); else FloatPatchOff(g_heavy, 2); }
    if (CHANGED(hyperArmour)) { if (c.hyperArmour) { if (Expect(RVA_HYPER_ARMOUR, HYPER_ORIG, 6, "hyper armour")) ApplyPatch(g_hyper); } else RevertPatch(g_hyper); }
    if (CHANGED(mariaImmortal)) { if (c.mariaImmortal) MariaOn(); else MariaOff(); }
    if (CHANGED(knifeHitFollowsBlade)) { if (c.knifeHitFollowsBlade) KnifeTipOn(); else KnifeTipOff(); }
    if (CHANGED(lightWiden)) { if (c.lightWiden > 0.0f) SweepOn(); else SweepOff(); }
    if (CHANGED(flashlightColour) || CHANGED(torchR) || CHANGED(torchG) || CHANGED(torchB)) { if (c.flashlightColour) TorchOn(c.torchR, c.torchG, c.torchB); else TorchOff(); }
    if (CHANGED(floodlight) || CHANGED(floodSize)) { if (c.floodlight) FloodOn(c.floodSize); else FloodOff(); }
    if (CHANGED(glowGone)) { if (c.glowGone) FloatPatchOn(g_glow, 2, 0.0f); else FloatPatchOff(g_glow, 2); }
    if (CHANGED(toggleRun) || CHANGED(runSpeed) || CHANGED(runStep)) { if (c.toggleRun) RunOn(c.runSpeed, c.runStep, c.runDefaultOn); else RunOff(); }
    if (CHANGED(roomFixes)) { if (c.roomFixes) { if (Expect(RVA_NO_TRIP, NOTRIP_ORIG, 6, "no tripping")) ApplyPatch(g_noTrip); } else { RevertPatch(g_noTrip); g_doorFix = NULL; g_doorFixUntil = 0; } }
#undef CHANGED
    g_live = c;
    g_haveLive = true;
    Log("config applied: anim %d light %g heavy %g knifetip %d widen %g maxhp %g fatigue0 %d hyper %d maria %d torch %d(%g,%g,%g) flood %d(%g) glow %d knife %d flash %d 2d %d run %d(key %02X pad %04X speed %g step %d default %d) allowgameover %d shorttrail %d roomfixes %d",
        c.animation, c.lightDamage, c.heavyDamage, c.knifeHitFollowsBlade, c.lightWiden, c.maxHealth, c.fatigueZero, c.hyperArmour, c.mariaImmortal, c.flashlightColour, c.torchR, c.torchG, c.torchB,
        c.floodlight, c.floodSize, c.glowGone, c.knifeInInventory, c.flashlightInInventory, c.directional2D, c.toggleRun, c.runKey, c.runPadButton, c.runSpeed, c.runStep, c.runDefaultOn, c.allowGameOver, c.shortTrail, c.roomFixes);
    LeaveCriticalSection(&g_applyLock);
}

static void RevertAll()
{
    Config off; ConfigAllOff(&off);
    Apply(off);
}

// ---------------------------------------------------------------------------
// the thread: config reload, enforcement, input
// ---------------------------------------------------------------------------
static volatile LONG g_stop = 0;
static HANDLE g_thread = NULL;

// The wanted config (from the .ini) and whether the scenario lets it apply:
// in Born From a Wish (chapter byte 1) everything is withdrawn -- Maria must
// not get the knife, the health, or James's clip table -- and comes back
// when the main scenario is playing again.
static bool g_bfaw = false;

static void ApplyWanted()
{
    if (g_bfaw) { Config off; ConfigAllOff(&off); Apply(off); }
    else Apply(g_cfg);
}

static DWORD WINAPI Worker(LPVOID)
{
    int tick = 0;
    while (!g_stop) {
        if ((tick++ % 33) == 0 && IniChanged()) {          // about once a second
            LoadConfig(g_ini, &g_cfg);
            ApplyWanted();
        }
        bool bfaw = !ScenarioAllows(*(const unsigned char*)(g_base + RVA_CHAPTER_ID));
        if (bfaw != g_bfaw) {
            g_bfaw = bfaw;
            Log(bfaw ? "Born From a Wish: mod withdrawn" : "main scenario: mod applied");
            ApplyWanted();
        }
        if (g_haveLive) {
            EnforceTick();
            if (g_live.toggleRun) PollToggle();
        }
        Sleep(30);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// entry
// ---------------------------------------------------------------------------
// Only sh2pc.exe is patched.  Any other host (the test harness, a stray
// LoadLibrary) gets an idle plugin that logs and waits -- unless the harness
// hands it a base of its own through PlayableRedTestBase.
static bool IsGameProcess()
{
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    const char* name = strrchr(exe, '\\');
    name = name ? name + 1 : exe;
    return _stricmp(name, "sh2pc.exe") == 0;
}

static volatile LONG g_booted = 0;

static void StartWorking()
{
    char parent[MAX_PATH]; strcpy(parent, g_dir); parent[strlen(parent) - 1] = 0;
    char* cut = strrchr(parent, '\\'); if (cut) { *cut = 0; CreateDirectoryA(parent, NULL); }   // the plugins folder first
    CreateDirectoryA(g_dir, NULL);
    if (GetFileAttributesA(g_ini) == INVALID_FILE_ATTRIBUTES) { WriteDefaultIni(); Log("wrote a default config"); }
    LoadXInput();
    LoadConfig(g_ini, &g_cfg);
    IniChanged();
    g_bfaw = !ScenarioAllows(*(const unsigned char*)(g_base + RVA_CHAPTER_ID));
    if (g_bfaw) Log("Born From a Wish is playing: mod withdrawn until the main scenario");
    ApplyWanted();
    g_thread = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
}

static void Boot()
{
    FILE* f = fopen(g_logPath, "w"); if (f) fclose(f);
    Log("PlayableRed %s booting; host module at %08X; config %s", PLAYABLERED_VERSION, (unsigned)GetModuleHandleA(NULL), g_ini);
    if (!IsGameProcess()) {
        Log("host is not sh2pc.exe: idle until a test base is given");
        return;
    }
    g_base = (uintptr_t)GetModuleHandleA(NULL);
    if (InterlockedExchange(&g_booted, 1) == 0) StartWorking();
}

// Test harness entry: work against a copy of sh2pc.exe mapped at `base` instead of the real image.
extern "C" __declspec(dllexport) int __stdcall PlayableRedTestBase(uintptr_t base)
{
    if (IsGameProcess()) return 0;
    g_base = base;
    Log("test base %08X", (unsigned)base);
    if (InterlockedExchange(&g_booted, 1) == 0) StartWorking();
    return 1;
}

// Boot on a thread of our own: DllMain runs under the loader lock, where the
// LoadLibrary of XInput and the file work below do not belong (the overlay
// plugin boots the same way).  The thread starts once DllMain has returned.
static DWORD WINAPI BootThread(LPVOID)
{
    Boot();
    return 0;
}

BOOL WINAPI DllMain(HMODULE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = h;
        DisableThreadLibraryCalls(h);
        // What LoadPlugin may need before the boot thread has run: the lock
        // and the paths.  Nothing here takes the loader lock or loads a DLL.
        InitializeCriticalSection(&g_logLock);
        InitializeCriticalSection(&g_applyLock);
        // the game's folder (the exe's), not this file's: the .asi may sit in the root or in the plugins folder
        char self[MAX_PATH];
        GetModuleFileNameA(NULL, self, MAX_PATH);
        char* slash = strrchr(self, '\\');
        if (slash) slash[1] = 0;
        sprintf(g_dir, "%splugins\\PlayableRedMod\\", self);
        sprintf(g_ini, "%sPlayableRed.ini", g_dir);
        sprintf(g_logPath, "%sPlayableRed.log", g_dir);
        InterlockedExchange(&g_logReady, 1);
        CreateThread(NULL, 0, BootThread, NULL, 0, NULL);
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_stop, 1);
    }
    return TRUE;
}

// SH2Framework (winmm.dll) resolves these by plain name; the .def keeps them undecorated.
extern "C" int __stdcall LoadPlugin(int frameworkVersion)
{
    Log("SH2Framework: LoadPlugin (framework version %d)", frameworkVersion);
    return 1;
}
extern "C" int __stdcall UnloadPlugin(void)
{
    Log("SH2Framework: UnloadPlugin -- reverting every patch");
    InterlockedExchange(&g_stop, 1);
    if (g_thread) { WaitForSingleObject(g_thread, 3000); g_thread = NULL; }
    RevertAll();
    return 1;
}
extern "C" int __stdcall Update(void)
{
    return 1;   // our own thread does the work; the framework's cadence is not relied on
}
