// test_logic.cpp -- checks playablered_logic without the game.  Exit 0 = every check passed.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "playablered_logic.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++fails; } else printf("ok: %s\n", msg); } while (0)

int main()
{
    // the substitution: only with the knife, only inside the stock table
    CHECK(SubstituteDescriptor(0x6CE900, 0x0F, 0x6CE8DC, 0x6CEA68, 0x10000) == 0x10000 + 0x24, "walk record redirected into the copy");
    CHECK(SubstituteDescriptor(0x6CE900, 0x03, 0x6CE8DC, 0x6CEA68, 0x10000) == 0x6CE900, "other weapon: stock record kept");
    CHECK(SubstituteDescriptor(0x6CEFF4, 0x0F, 0x6CE8DC, 0x6CEA68, 0x10000) == 0x6CEFF4, "weapon-block record untouched");
    CHECK(SubstituteDescriptor(0x6CEA68, 0x0F, 0x6CE8DC, 0x6CEA68, 0x10000) == 0x6CEA68, "end of table is exclusive");
    CHECK(SubstituteDescriptor(0x6CE8DC, 0x0F, 0x6CE8DC, 0x6CEA68, 0x10000) == 0x10000, "first record maps to the copy's first");

    // the copy table, built over a stock table read from the exe on disk when it is at hand
    unsigned char stock[396];
    for (int i = 0; i < 33; ++i) {
        unsigned short id = (unsigned short)(101 + i);
        memset(stock + 12 * i, 0, 12);
        memcpy(stock + 12 * i, &id, 2);
        stock[12 * i + 10] = (unsigned char)(i % 2);     // a loop flag to prove it is kept
    }
    FILE* f = fopen("..\\..\\..\\sh2pc.exe", "rb");
    if (f) {
        fseek(f, 0x2CE8DC, SEEK_SET);
        fread(stock, 1, 396, f);
        fclose(f);
        printf("(stock table read from sh2pc.exe)\n");
    }
    unsigned char copy[400];
    BuildGreatKnifeBasicTable(copy, stock);
    const unsigned char reveal[8] = {0x31, 0x00, 0x00, 0x08, 0xBD, 0x03, 0xED, 0x03};
    const unsigned char walk[8]   = {0x18, 0x00, 0x80, 0x05, 0xD2, 0x02, 0xE9, 0x02};
    CHECK(memcmp(copy + 2, reveal, 8) == 0, "101 -> reveal 957..1005 @2048");
    CHECK(memcmp(copy + 12 * 3 + 2, walk, 8) == 0, "104 -> walk 722..745 @1408");
    CHECK(memcmp(copy + 12 * 12 + 2, reveal, 8) == 0 && memcmp(copy + 12 * 22 + 2, reveal, 8) == 0, "113 and 123 -> reveal");
    CHECK(memcmp(copy + 12 * 1, stock + 12 * 1, 12) == 0 && memcmp(copy + 12 * 32, stock + 12 * 32, 12) == 0, "102 and 133 stock");
    bool ids = true, flags = true;
    for (int i = 0; i < 33; ++i) {
        if (memcmp(copy + 12 * i, stock + 12 * i, 2) != 0) ids = false;
        if (memcmp(copy + 12 * i + 10, stock + 12 * i + 10, 2) != 0) flags = false;
    }
    CHECK(ids, "every id kept");
    CHECK(flags, "every loop flag kept (+10)");
    CHECK(copy[396] == 0 && copy[397] == 0 && copy[398] == 0 && copy[399] == 0, "terminator");

    // weapon slots
    unsigned char r[8];
    WeaponSlotRecord(1, 1024, r);  CHECK(memcmp(r, "\x01\x00\x00\x08\xF0\x02\xF0\x02", 8) == 0, "451 Ready Alt: one frame 752 @2048");
    WeaponSlotRecord(2, -1024, r); CHECK(memcmp(r, "\x01\x00\x00\xF8\xF0\x02\xF0\x02", 8) == 0, "452 Ret Alt: one frame @-2048");
    WeaponSlotRecord(8, 256, r);   CHECK(memcmp(r, "\x12\x00\x00\x08\xF0\x02\x01\x03", 8) == 0, "458 Stance: idle 752..769");
    WeaponSlotRecord(11, 768, r);  CHECK(memcmp(r, "\x43\x00\x00\x08\x02\x03\x44\x03", 8) == 0, "461 Swing: 770..836");
    WeaponSlotRecord(12, -768, r); CHECK(memcmp(r, "\x43\x00\x00\xF8\x02\x03\x44\x03", 8) == 0, "462 Swing Colli: backwards");
    WeaponSlotRecord(15, 768, r);  CHECK(memcmp(r, "\x78\x00\x00\x08\x45\x03\xBC\x03", 8) == 0, "465 Over: 837..956");
    WeaponSlotRecord(16, -768, r); CHECK(memcmp(r, "\x78\x00\x00\xF8\x45\x03\xBC\x03", 8) == 0, "466 Over Colli: backwards");
    unsigned char w[2];
    DamageWindow(19, w); CHECK(w[0] == 32 && w[1] == 39, "swing window 32..39");
    DamageWindow(22, w); CHECK(w[0] == 66 && w[1] == 73, "overhead window 66..73");

    // run decision
    CHECK(RunStep(false, 0x100, 1.0f, 3000) == -1, "run mode off: keep");
    CHECK(RunStep(true, 0x100, 1.0f, 3000) == 3000, "run mode on, player, running: force step");
    CHECK(RunStep(true, 0x208, 1.0f, 3000) == -1, "an NPC keeps its own");
    CHECK(RunStep(true, 0x100, 0.25f, 3000) == -1, "not running: keep");
    CHECK(RunStep(true, 0x100, 0.5f, 3000) == 3000, "0.5 counts as running");

    // config round trip through a real ini
    char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp); strcat(tmp, "playablered_test.ini");
    DeleteFileA(tmp);
    Config a; ConfigDefaults(&a);
    CHECK(!LoadConfig(tmp, &a) && a.animation && a.runKey == 0x10 && a.runPadButton == 0x4000 && a.maxHealth == 1000.0f && a.allowGameOver && a.hyperArmour && a.mariaImmortal && a.knifeHitFollowsBlade && a.lightWiden == 10.0f, "no file: defaults (allow game over, immortal Maria, the knife's hit line and a 10-degree widening on)");
    a.animation = false; a.lightDamage = 777.5f; a.runKey = 0x20; a.runPadButton = 0x1000; a.floodSize = 2.25f; a.allowGameOver = false; a.runStep = 2500; a.mariaImmortal = false; a.lightWiden = 7.5f; a.knifeHitFollowsBlade = false;
    CHECK(SaveConfig(tmp, &a), "save");
    Config b;
    CHECK(LoadConfig(tmp, &b), "load");
    CHECK(!b.animation && fabs(b.lightDamage - 777.5f) < 1e-3 && b.runKey == 0x20 && b.runPadButton == 0x1000 && fabs(b.floodSize - 2.25f) < 1e-4 && !b.allowGameOver && b.runStep == 2500 && !b.mariaImmortal && fabs(b.lightWiden - 7.5f) < 1e-4 && !b.knifeHitFollowsBlade, "values survive the round trip");
    CHECK(b.heavyDamage == 10000.0f && b.hyperArmour && b.knifeInInventory, "untouched keys keep their defaults");
    // a hand-written file with hex keys and a missing key
    FILE* h = fopen(tmp, "w"); fputs("[PlayableRed]\nToggleRunKey=0x11\nMaxHealth=250\n", h); fclose(h);
    Config c;
    LoadConfig(tmp, &c);
    CHECK(c.runKey == 0x11 && c.maxHealth == 250.0f && c.animation && c.runPadButton == 0x4000, "hand-written file: hex key, missing keys default");
    Config off; ConfigAllOff(&off);
    CHECK(!off.animation && !off.allowGameOver && !off.mariaImmortal && off.lightDamage == 0 && off.maxHealth == 0, "all-off config");
    DeleteFileA(tmp);

    // the inventory items are not enforced in the weight-limit elevator room
    CHECK(!InventoryEnforcedIn(0x9D), "room 0x9D: inventory items left alone");
    CHECK(InventoryEnforcedIn(0x9C) && InventoryEnforcedIn(0x9E) && InventoryEnforcedIn(0) && InventoryEnforcedIn(1), "other rooms: enforced");

    // allow game over
    CHECK(GameOverDue(0.0f, 0), "health 0, state alive: set the game-over state");
    CHECK(GameOverDue(-5.0f, 1), "health below 0: set it");
    CHECK(!GameOverDue(0.0f, 2), "already 2: nothing to write");
    CHECK(!GameOverDue(0.5f, 0), "health above 0: leave it");

    // the new-game top-up window: three seconds by the caller's clock
    bool armed = true; uint32_t until = 0;
    CHECK(!NewGameHeal(1000.0f, 0, 1, 3000, 10000, &armed, &until) && armed, "room 0 (the intro): nothing, armed");
    CHECK(NewGameHeal(1000.0f, 1, 1, 3000, 10000, &armed, &until) && !armed && until == 13000, "arriving in room 1: window opens to now + 3 s, heals, disarmed");
    CHECK(NewGameHeal(1000.0f, 1, 1, 3000, 12999, &armed, &until), "inside the window: heals");
    CHECK(!NewGameHeal(1000.0f, 1, 1, 3000, 13000, &armed, &until) && until == 0, "at the deadline, still in room 1: closed");
    CHECK(!NewGameHeal(1000.0f, 1, 1, 3000, 20000, &armed, &until), "much later in room 1: nothing");
    CHECK(!NewGameHeal(1000.0f, 2, 1, 3000, 21000, &armed, &until) && armed, "leaving room 1: re-armed");
    CHECK(NewGameHeal(1000.0f, 1, 1, 3000, 30000, &armed, &until) && until == 33000, "back in room 1 (a later new game): the window opens again");
    armed = true; until = 0;
    CHECK(!NewGameHeal(0.0f, 1, 1, 3000, 40000, &armed, &until) && until == 0, "max health not enforced: never");
    armed = true; until = 0;
    // opened 1024 ms before the 32-bit clock wraps; 256 ms after the wrap is 1280 ms into a 3000 ms window
    CHECK(NewGameHeal(1000.0f, 1, 1, 3000, 0xFFFFFC00u, &armed, &until) && NewGameHeal(1000.0f, 1, 1, 3000, 0x00000100u, &armed, &until)
          && !NewGameHeal(1000.0f, 1, 1, 3000, 0x00000800u, &armed, &until), "the clock wrapping past zero neither closes the window early nor keeps it open late");
    Config st; ConfigDefaults(&st);
    CHECK(st.shortTrail, "short trail defaults on");

    // the room-load fixes: always on, off only with everything else
    CHECK(st.roomFixes && !off.roomFixes, "room fixes: on by default, off in the all-off config");
    FILE* h2 = fopen(tmp, "w"); fputs("[PlayableRed]\nRoomFixes=0\nNoTripping=0\n", h2); fclose(h2);
    Config rf; LoadConfig(tmp, &rf); DeleteFileA(tmp);
    CHECK(rf.roomFixes, "room fixes cannot be turned off from the .ini");
    const DoorFix* df = DoorFixFor(0x20, 0x26);
    CHECK(df && df->floorY == 0.0f && df->aboveBy == 1000.0f, "Blue Creek 0x20 -> 0x26 is in the door table (floor 0, a metre)");
    CHECK(!DoorFixFor(0x26, 0x20) && !DoorFixFor(0x20, 0x27) && !DoorFixFor(0x21, 0x26), "other transitions are not");
    CHECK(DoorFixDue(df, -1705.0f), "on the ceiling (y -1705): due");
    CHECK(!DoorFixDue(df, 0.0f) && !DoorFixDue(df, 700.0f) && !DoorFixDue(df, -999.0f), "on the floor, below it, or under a metre above it: not due");
    CHECK(DoorFixDue(df, -1001.0f) && !DoorFixDue(NULL, -1705.0f), "just past a metre: due; no entry: never");

    // the Great Knife's tip marker
    float t1[3] = {425.957f, -479.930f, 515.439f};
    CHECK(KnifeTipFollow(t1, 0x0F) && fabs(t1[0] - 197.813f) < 0.01f && fabs(t1[1] + 234.515f) < 0.01f && fabs(t1[2] - 517.472f) < 0.01f, "knife equipped, the stock tip marker: turned with the mesh");
    float t2[3] = {425.957f, -479.930f, 515.439f};
    CHECK(!KnifeTipFollow(t2, 0x03) && t2[0] == 425.957f, "another weapon equipped: untouched");
    float t3[3] = {100.0f, -200.0f, 300.0f};
    CHECK(!KnifeTipFollow(t3, 0x0F) && t3[2] == 300.0f, "not the knife's marker: untouched");
    // the light attack's sweep, widened 10 degrees each side
    float p0[4] = {0, 0, 0, 1}, p1[4] = {100, 5, 0, 1}, p3[4] = {0, 5, 100, 1};
    WidenSweep(p0, p1, p3, 10.0f);
    CHECK(fabs(p1[0] - 98.481f) < 0.01f && fabs(p1[2] + 17.365f) < 0.01f && p1[1] == 5.0f, "p1 at 0 deg turned back to -10 deg, height kept");
    CHECK(fabs(p3[0] + 17.365f) < 0.01f && fabs(p3[2] - 98.481f) < 0.01f, "p3 at 90 deg turned on to 100 deg");
    float q0[4] = {10, 0, 10, 1}, q1[4] = {10, 0, 110, 1}, q3[4] = {110, 0, 10, 1};
    WidenSweep(q0, q1, q3, 10.0f);
    CHECK(fabs(atan2(q1[2] - 10, q1[0] - 10) * 180 / 3.14159265 - 100) < 0.01 && fabs(atan2(q3[2] - 10, q3[0] - 10) * 180 / 3.14159265 + 10) < 0.01, "a sweep the other way round: the sides swap");
    float r1[4] = {100, 0, 0, 1}, r3[4] = {100, 0, 0, 1};
    WidenSweep(p0, r1, r3, 10.0f);
    CHECK(fabs(atan2(r1[2], r1[0]) * 180 / 3.14159265 + 10) < 0.01 && fabs(atan2(r3[2], r3[0]) * 180 / 3.14159265 - 10) < 0.01, "a standing blade: 10 degrees each way");
    float s1[4] = {100, 0, 0, 1}, s3[4] = {0, 0, 100, 1};
    WidenSweep(p0, s1, s3, 0.0f);
    CHECK(s1[0] == 100.0f && s3[2] == 100.0f, "0 degrees: untouched");

    // the scenario gate
    CHECK(ScenarioAllows(0), "main scenario: mod applies");
    CHECK(!ScenarioAllows(1), "Born From a Wish: mod withdrawn");
    CHECK(ScenarioAllows(7), "any other value: applies");

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
