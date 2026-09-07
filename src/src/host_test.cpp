// host_test.cpp -- loads PlayableRed.asi the way the game does, without the game.
//
//   host_test.exe <PlayableRed.asi> <sh2pc.exe>
//
// The exe's sections are copied into a private region at their RVAs; the DLL
// is loaded and LoadPlugin called AT ONCE (the framework does that under the
// loader lock, before any thread of ours can run -- the 0xC0000142 of the first
// deployment); then the DLL is handed the region as its base and the bytes it
// wrote are checked: every hook a jump into the DLL, the tables, the enforced
// values.  Then an all-off config is written and the code/rdata region must
// come back byte for byte.  Exit 0 = passed.
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "playablered_logic.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); ++fails; } else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef int (__stdcall* pfnInt)(int);
typedef int (__stdcall* pfnVoid)(void);
typedef int (__stdcall* pfnBase)(uintptr_t);

static unsigned char* MapExe(const char* path, unsigned char** pristine, size_t* size)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* file = (unsigned char*)malloc(n);
    fread(file, 1, n, f); fclose(f);
    uint32_t pe = *(uint32_t*)(file + 0x3C);
    uint16_t nsec = *(uint16_t*)(file + pe + 6);
    uint16_t opt = *(uint16_t*)(file + pe + 20);
    uint32_t imageSize = *(uint32_t*)(file + pe + 24 + 56);
    uint32_t hdrSize = *(uint32_t*)(file + pe + 24 + 60);
    unsigned char* region = (unsigned char*)VirtualAlloc(NULL, imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!region) return NULL;
    memcpy(region, file, hdrSize);
    for (int i = 0; i < nsec; ++i) {
        unsigned char* s = file + pe + 24 + opt + 40 * i;
        uint32_t vs = *(uint32_t*)(s + 8), va = *(uint32_t*)(s + 12), rs = *(uint32_t*)(s + 16), ro = *(uint32_t*)(s + 20);
        uint32_t take = rs < vs ? rs : vs;
        if (va + take <= imageSize && ro + take <= (uint32_t)n) memcpy(region + va, file + ro, take);
    }
    free(file);
    *pristine = (unsigned char*)malloc(imageSize);
    memcpy(*pristine, region, imageSize);
    *size = imageSize;
    return region;
}

static bool JumpsInto(const unsigned char* site, HMODULE dll)
{
    if (site[0] != 0xE9) return false;
    int32_t rel; memcpy(&rel, site + 1, 4);
    uintptr_t target = (uintptr_t)site + 5 + rel;
    // the DLL's image bounds from its PE header
    const unsigned char* b = (const unsigned char*)dll;
    uint32_t pe = *(const uint32_t*)(b + 0x3C);
    uint32_t imageSize = *(const uint32_t*)(b + pe + 24 + 56);
    return target >= (uintptr_t)b && target < (uintptr_t)b + imageSize;
}

int main(int argc, char** argv)
{
    if (argc < 3) { printf("usage: host_test <PlayableRed.asi> <sh2pc.exe>\n"); return 2; }
    unsigned char *pristine, *region; size_t size;
    region = MapExe(argv[2], &pristine, &size);
    if (!region) { printf("could not map %s\n", argv[2]); return 2; }
    printf("mapped a copy of sh2pc.exe at %p (%u bytes)\n", region, (unsigned)size);

    // a fresh config next to the DLL, and values for the enforcement to correct
    char ini[MAX_PATH]; strcpy(ini, argv[1]);
    char* slash = strrchr(ini, '\\'); if (slash) slash[1] = 0;
    strcat(ini, "plugins\\PlayableRedMod\\PlayableRed.ini");
    DeleteFileA(ini);
    region[0x1BB8117] = 0;                       // the game-over state: alive
    *(float*)(region + 0x1BB113C) = 50.0f;       // a new game's starting health
    *(float*)(region + 0x1BB1140) = 100.0f;      // a save's max health
    *(float*)(region + 0x1BB80F4) = 7.0f;        // fatigue
    *(uint32_t*)(region + 0x1BB7DAC) = 0;        // room 0: a brand new game
    region[0x19BC00C] = 0;                       // main scenario
    region[0x1B7A7E1] = 0x21;                    // inventory flags byte 1: two other weapons, no Great Knife (bit 0x80)
    region[0x1B7A7E2] = 0x30;                    // byte 2: photo and letter, no flashlight (bit 0x04)

    HMODULE dll = LoadLibraryA(argv[1]);
    CHECK(dll != NULL, "LoadLibrary %s", argv[1]);
    if (!dll) return 1;
    pfnInt loadPlugin = (pfnInt)GetProcAddress(dll, "LoadPlugin");
    pfnVoid update = (pfnVoid)GetProcAddress(dll, "Update");
    pfnVoid unload = (pfnVoid)GetProcAddress(dll, "UnloadPlugin");
    pfnBase testBase = (pfnBase)GetProcAddress(dll, "PlayableRedTestBase");
    CHECK(loadPlugin && update && unload && testBase, "exports LoadPlugin, Update, UnloadPlugin, PlayableRedTestBase");
    CHECK(loadPlugin(1) == 1, "LoadPlugin at once, before the boot thread");
    CHECK(update() == 1, "Update");
    Sleep(300);
    CHECK(memcmp(region, pristine, 0x399000) == 0, "nothing written before a base is known (host is not sh2pc.exe)");
    CHECK(testBase((uintptr_t)region) == 1, "PlayableRedTestBase accepted");
    Sleep(1500);

    // hooks: a jump into the DLL at every site
    CHECK(JumpsInto(region + 0x138CD0, dll), "clip setter +138CD0 jumps into the DLL");
    CHECK(JumpsInto(region + 0x10A936, dll) && JumpsInto(region + 0x10A93C, dll) && JumpsInto(region + 0x10A942, dll), "three flashlight sites jump into the DLL");
    CHECK(JumpsInto(region + 0x7A612, dll), "floodlight +7A612 jumps into the DLL");
    CHECK(JumpsInto(region + 0x146D4C, dll), "run travel speed +146D4C (first match) jumps into the DLL");
    CHECK(JumpsInto(region + 0x1527C2, dll) && region[0x1527C7] == 0x90 && region[0x1527C8] == 0x90 && region[0x1527C9] == 0x01, "run walk step +1527C2: jump, two nops, add [ebp+18] intact");
    CHECK(region[0x1358ED] == 0x0F && region[0x1358EE] == 0x84, "hyper armour: jne -> je");
    CHECK(region[0x12F057] == 0x0F && region[0x12F058] == 0x85 && region[0x12F059] == 0x8E, "no tripping: je -> jne at +12F057 (always on, no key)");
    CHECK(region[0x12CC95] == 0xA8 && region[0x12CC96] == 0x00 && region[0x12CC97] == 0x74, "immortal Maria: test al,al -> test al,0 at +12CC95, the je kept");
    CHECK(JumpsInto(region + 0x12A35F, dll) && region[0x12A364] == 0x90 && region[0x12A365] == 0x83, "immortal Maria: controls hook at +12A35F, nop, add esp intact");
    CHECK(JumpsInto(region + 0x1463BA, dll) && region[0x1463BF] == 0x90 && region[0x1463C4] == 0x90 && region[0x1463C5] == 0x89, "knife tip marker hook at +1463BA: jump, six nops, the next instruction intact");
    CHECK(JumpsInto(region + 0x144049, dll) && region[0x14404E] == 0x83, "light attack sweep hook at +144049: the call replaced, add esp intact");
    // data patches
    CHECK(*(float*)(region + 0x2EC630) == 1200.0f && *(float*)(region + 0x2EC654) == 1200.0f, "light damage 1200");
    CHECK(*(float*)(region + 0x2EC678) == 10000.0f && *(float*)(region + 0x2EC69C) == 10000.0f, "heavy damage 10000");
    CHECK(*(float*)(region + 0x2C3168) == 0.0f && *(float*)(region + 0x2C316C) == 0.0f, "glow constants 0");
    const unsigned char stance[8] = {0x12,0x00,0x00,0x08,0xF0,0x02,0x01,0x03};
    const unsigned char over[8] = {0x78,0x00,0x00,0x08,0x45,0x03,0xBC,0x03};
    const unsigned char overColli[8] = {0x78,0x00,0x00,0xF8,0x45,0x03,0xBC,0x03};
    CHECK(memcmp(region + 0x2CEFE8 + 12 * 8 + 2, stance, 8) == 0, "weapon slot 8 (458 Stance) = idle 752..769");
    CHECK(memcmp(region + 0x2CEFE8 + 12 * 15 + 2, over, 8) == 0 && memcmp(region + 0x2CEFE8 + 12 * 16 + 2, overColli, 8) == 0, "slots 15/16: overhead forward and backwards");
    CHECK(region[0x2EC384 + 36 * 19 + 28] == 32 && region[0x2EC384 + 36 * 19 + 29] == 39 && region[0x2EC384 + 36 * 22 + 28] == 66, "damage windows");
    CHECK(memcmp(region + 0x2CE8DC, pristine + 0x2CE8DC, 396) == 0, "the stock basic table is never written");
    // enforced values
    CHECK(*(float*)(region + 0x1BB1140) == 1000.0f, "max health forced to 1000");
    CHECK(*(float*)(region + 0x1BB80F4) == 0.0f, "fatigue forced to 0");
    CHECK(region[0x1B7A7E1] == 0xA1, "Great Knife bit added, the two other weapons kept (21 -> A1)");
    CHECK(region[0x1B7A7E2] == 0x34, "flashlight bit added, photo and letter kept (30 -> 34)");
    region[0x1B7A7E1] = 0x80; region[0x1B7A7E2] = 0x04; Sleep(150);
    CHECK(region[0x1B7A7E1] == 0x80 && region[0x1B7A7E2] == 0x04, "already present: no write at all (other bits not restored)");
    CHECK(region[0x19BC008] == 1, "2D directional");
    // the weight-limit elevator room: the items are not put back
    *(uint32_t*)(region + 0x1BB7DAC) = 0x9D; Sleep(150);
    region[0x1B7A7E1] = 0x00; region[0x1B7A7E2] = 0x30; Sleep(200);
    CHECK(region[0x1B7A7E1] == 0x00 && region[0x1B7A7E2] == 0x30, "room 0x9D: the shelved Great Knife and flashlight stay shelved");
    *(uint32_t*)(region + 0x1BB7DAC) = 0; Sleep(200);
    CHECK(region[0x1B7A7E1] == 0x80 && region[0x1B7A7E2] == 0x34, "out of room 0x9D: both added again");
    CHECK(region[0x1BB8117] == 0, "alive with health above 0: game-over state left at 0");
    CHECK(region[0x4E700B] == 1, "short trail byte held at 1");
    // the new-game top-up: a window that opens on arriving in room 1
    CHECK(*(float*)(region + 0x1BB113C) == 50.0f, "room 0 (the intro): health left alone");
    *(uint32_t*)(region + 0x1BB7DAC) = 1;
    Sleep(200);
    CHECK(*(float*)(region + 0x1BB113C) == 1000.0f, "arriving in room 1: health set to the max health");
    *(float*)(region + 0x1BB113C) = 5.0f;          // the game's own late write during the load
    Sleep(200);
    CHECK(*(float*)(region + 0x1BB113C) == 1000.0f, "inside the 3-second window: set again");
    Sleep(3200);
    *(float*)(region + 0x1BB113C) = 5.0f;          // play: health lost
    Sleep(200);
    CHECK(*(float*)(region + 0x1BB113C) == 5.0f, "after the window, still in room 1: not touched");
    *(uint32_t*)(region + 0x1BB7DAC) = 3; Sleep(100);
    *(uint32_t*)(region + 0x1BB7DAC) = 1; Sleep(200);
    CHECK(*(float*)(region + 0x1BB113C) == 1000.0f, "a later new game (room 1 again): the window opens again");
    // re-enforcement after the game 'changes' max health
    *(float*)(region + 0x1BB1140) = 5.0f;
    Sleep(200);
    CHECK(*(float*)(region + 0x1BB1140) == 1000.0f, "max health changed by the game: corrected within 200 ms");
    // allow game over: health reaches 0 (out of room 1 first, or the heal window would refill it)
    *(uint32_t*)(region + 0x1BB7DAC) = 4; Sleep(150);
    *(float*)(region + 0x1BB113C) = 0.0f;
    Sleep(200);
    CHECK(region[0x1BB8117] == 2, "health 0: game-over state set to 2 within 200 ms");
    *(float*)(region + 0x1BB113C) = 60.0f;
    // the room-load fixes (the player object where the game keeps it)
    *(uint32_t*)(region + 0x1BB7C80) = (uint32_t)(uintptr_t)(region + 0x1BB1000);
    float* py = (float*)(region + 0x1BB1020);
    float* pfloor = (float*)(region + 0x1BB10C8);
    float* gfloor = (float*)(region + 0x1BB7C98);
    float* pvel = (float*)(region + 0x1BB10B0);
    // the floor follows the player at every room change
    *py = -1200.0f; *pfloor = -1200.0f; *gfloor = -1200.0f;
    *(uint32_t*)(region + 0x1BB7DAC) = 0x30; Sleep(150);
    *py = 500.0f;                                    // a door onto another storey
    *(uint32_t*)(region + 0x1BB7DAC) = 0x31; Sleep(200);
    CHECK(*pfloor == 500.0f && *gfloor == 500.0f && *py == 500.0f, "room change with the player at y 500 and the floor at -1200: the floor follows (both fields), y untouched");
    *pfloor = -1200.0f; *gfloor = -1200.0f; Sleep(200);
    CHECK(*gfloor == -1200.0f, "the floor is only set at the change itself, never afterwards");
    // the door table: Blue Creek 0x20 -> 0x26, the game's snap to the stale floor undone
    *py = -1705.0f; *pfloor = -1705.0f; *gfloor = -1705.0f;
    *(uint32_t*)(region + 0x1BB7DAC) = 0x20; Sleep(150);
    *py = 0.0f;                                      // the door places the player on the hallway floor
    *(uint32_t*)(region + 0x1BB7DAC) = 0x26; Sleep(200);
    CHECK(*py == 0.0f && *pfloor == 0.0f && *gfloor == 0.0f, "0x20 -> 0x26: placed at y 0, the stale floor -1705 follows to 0");
    *py = -1705.0f; *pvel = 3.0f; Sleep(200);          // what the gravity routine did before: lifted them to the old floor
    CHECK(*py == 0.0f && *pfloor == 0.0f && *gfloor == 0.0f && *(float*)(region + 0x1BB10D0) == 0.0f && *pvel == 0.0f,
          "lifted to -1705 inside the window: put back on the floor 0 (floor fields, stored y and speed too)");
    *py = -300.0f; Sleep(200);
    CHECK(*py == -300.0f, "a third of a metre above the floor: left alone");
    Sleep(3000); *py = -1705.0f; Sleep(200);
    CHECK(*py == -1705.0f, "after the 3-second window: not touched");
    *py = 0.0f;
    *(uint32_t*)(region + 0x1BB7DAC) = 0x27; Sleep(150);
    *py = -1705.0f; *pfloor = -1705.0f; *gfloor = -1705.0f;
    *(uint32_t*)(region + 0x1BB7DAC) = 0x26; Sleep(200);
    CHECK(*py == -1705.0f, "0x27 -> 0x26 is not in the table: not touched");
    *py = 0.0f; *pfloor = 0.0f; *gfloor = 0.0f; Sleep(100);
    // Born From a Wish: everything withdrawn, then back
    region[0x19BC00C] = 1;
    Sleep(400);
    size_t d = (size_t)-1;
    for (size_t i = 0x1000; i < 0x399000; ++i) if (region[i] != pristine[i]) { d = i; break; }
    CHECK(d == (size_t)-1, "chapter 1 (Born From a Wish): code and rdata back to the exe's (first difference at %X)", (unsigned)d);
    *(float*)(region + 0x1BB1140) = 5.0f; Sleep(200);
    CHECK(*(float*)(region + 0x1BB1140) == 5.0f, "chapter 1: nothing enforced");
    region[0x19BC00C] = 0;
    Sleep(400);
    CHECK(JumpsInto(region + 0x138CD0, dll) && *(float*)(region + 0x1BB1140) == 1000.0f, "main scenario again: patched and enforced again");

    // everything off through the .ini: code and rdata back byte for byte
    Config off; ConfigAllOff(&off);
    CHECK(SaveConfig(ini, &off), "wrote an all-off config");
    Sleep(2500);
    size_t firstDiff = (size_t)-1;
    for (size_t i = 0x1000; i < 0x399000; ++i) if (region[i] != pristine[i] && (i < 0x12F057 || i >= 0x12F05D)) { firstDiff = i; break; }
    CHECK(firstDiff == (size_t)-1, "all off: code and rdata identical to the exe but for the no-tripping flip (first difference at %X)", (unsigned)firstDiff);
    CHECK(region[0x12F058] == 0x85, "all off: no tripping stays on (it has no key; only Born From a Wish and unload withdraw it)");
    region[0x1BB8117] = 2;
    Sleep(200);
    CHECK(region[0x1BB8117] == 2, "all off: enforcement stopped");

    // back on: the same bytes again (the way back and forth is repeatable)
    Config on; ConfigDefaults(&on);
    SaveConfig(ini, &on);
    Sleep(2500);
    CHECK(JumpsInto(region + 0x138CD0, dll) && region[0x1358EE] == 0x84 && *(float*)(region + 0x2EC630) == 1200.0f, "defaults again: patched again");

    CHECK(unload() == 1, "UnloadPlugin");
    Sleep(300);
    firstDiff = (size_t)-1;
    for (size_t i = 0x1000; i < 0x399000; ++i) if (region[i] != pristine[i]) { firstDiff = i; break; }
    CHECK(firstDiff == (size_t)-1, "after UnloadPlugin: code and rdata identical to the exe");
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
