// playablered_logic.cpp -- see playablered_logic.h
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "playablered_logic.h"

static const char* SECTION = "PlayableRed";

void ConfigDefaults(Config* c)
{
    c->animation = true;
    c->lightDamage = 1200.0f;
    c->heavyDamage = 10000.0f;
    c->maxHealth = 1000.0f;
    c->fatigueZero = true;
    c->hyperArmour = true;
    c->mariaImmortal = true;
    c->flashlightColour = true;
    c->torchR = 127.0f; c->torchG = 0.000999451f; c->torchB = 0.000999451f;
    c->floodlight = true;
    c->floodSize = 1.1f;
    c->glowGone = true;
    c->knifeInInventory = true;
    c->flashlightInInventory = true;
    c->directional2D = true;
    c->toggleRun = true;
    c->runKey = 0x10;          // VK_SHIFT
    c->runPadButton = 0x4000;  // XINPUT_GAMEPAD_X
    c->runSpeed = 4.0f;
    c->runStep = 3000;
    c->runDefaultOn = false;
    c->allowGameOver = true;
    c->shortTrail = true;
    c->knifeHitFollowsBlade = true;
    c->lightWiden = 10.0f;
    c->roomFixes = true;
}

void ConfigAllOff(Config* c)
{
    ConfigDefaults(c);
    c->animation = false; c->lightDamage = 0; c->heavyDamage = 0; c->maxHealth = 0; c->fatigueZero = false;
    c->hyperArmour = false; c->mariaImmortal = false; c->flashlightColour = false; c->floodlight = false; c->glowGone = false;
    c->knifeInInventory = false; c->flashlightInInventory = false; c->directional2D = false; c->toggleRun = false;
    c->allowGameOver = false; c->shortTrail = false; c->roomFixes = false;
    c->knifeHitFollowsBlade = false; c->lightWiden = 0.0f;
}

static bool GetBool(const char* ini, const char* key, bool def)
{
    return GetPrivateProfileIntA(SECTION, key, def ? 1 : 0, ini) != 0;
}
static float GetFloat(const char* ini, const char* key, float def)
{
    char buf[64];
    GetPrivateProfileStringA(SECTION, key, "", buf, sizeof buf, ini);
    if (!buf[0]) return def;
    return (float)atof(buf);
}
static int GetInt(const char* ini, const char* key, int def)
{
    char buf[64];
    GetPrivateProfileStringA(SECTION, key, "", buf, sizeof buf, ini);
    if (!buf[0]) return def;
    return (int)strtol(buf, NULL, 0);     // 0x.. accepted for keys and button masks
}

bool LoadConfig(const char* ini, Config* c)
{
    ConfigDefaults(c);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) return false;
    Config d = *c;
    c->animation = GetBool(ini, "AnimationTranslation", d.animation);
    c->lightDamage = GetFloat(ini, "LightAttackDamage", d.lightDamage);
    c->heavyDamage = GetFloat(ini, "HeavyAttackDamage", d.heavyDamage);
    c->maxHealth = GetFloat(ini, "MaxHealth", d.maxHealth);
    c->fatigueZero = GetBool(ini, "FatigueZero", d.fatigueZero);
    c->hyperArmour = GetBool(ini, "HyperArmour", d.hyperArmour);
    c->mariaImmortal = GetBool(ini, "MariaImmortal", d.mariaImmortal);
    c->flashlightColour = GetBool(ini, "FlashlightColour", d.flashlightColour);
    c->torchR = GetFloat(ini, "FlashlightR", d.torchR);
    c->torchG = GetFloat(ini, "FlashlightG", d.torchG);
    c->torchB = GetFloat(ini, "FlashlightB", d.torchB);
    c->floodlight = GetBool(ini, "Floodlight", d.floodlight);
    c->floodSize = GetFloat(ini, "FloodlightSize", d.floodSize);
    c->glowGone = GetBool(ini, "FlashlightGlowGone", d.glowGone);
    c->knifeInInventory = GetBool(ini, "GreatKnifeInInventory", d.knifeInInventory);
    c->flashlightInInventory = GetBool(ini, "FlashlightInInventory", d.flashlightInInventory);
    c->directional2D = GetBool(ini, "Directional2D", d.directional2D);
    c->toggleRun = GetBool(ini, "ToggleRun", d.toggleRun);
    c->runKey = GetInt(ini, "ToggleRunKey", d.runKey);
    c->runPadButton = GetInt(ini, "ToggleRunPadButton", d.runPadButton);
    c->runSpeed = GetFloat(ini, "RunSpeed", d.runSpeed);
    c->runStep = GetInt(ini, "RunAnimationStep", d.runStep);
    c->runDefaultOn = GetBool(ini, "RunModeOnAtStart", d.runDefaultOn);
    c->allowGameOver = GetBool(ini, "AllowGameOver", d.allowGameOver);
    c->shortTrail = GetBool(ini, "ShortTrail", d.shortTrail);
    c->knifeHitFollowsBlade = GetBool(ini, "KnifeHitFollowsBlade", d.knifeHitFollowsBlade);
    c->lightWiden = GetFloat(ini, "LightAttackWiden", d.lightWiden);
    return true;
}

static void PutInt(const char* ini, const char* key, int v)   { char b[32]; sprintf(b, "%d", v); WritePrivateProfileStringA(SECTION, key, b, ini); }
static void PutHex(const char* ini, const char* key, int v)   { char b[32]; sprintf(b, "0x%X", v); WritePrivateProfileStringA(SECTION, key, b, ini); }
static void PutFloat(const char* ini, const char* key, float v) { char b[32]; sprintf(b, "%g", v); WritePrivateProfileStringA(SECTION, key, b, ini); }

bool SaveConfig(const char* ini, const Config* c)
{
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) {
        FILE* f = fopen(ini, "w");
        if (!f) return false;
        fputs(DefaultIniText(), f);
        fclose(f);
    }
    PutInt(ini, "AnimationTranslation", c->animation);
    PutFloat(ini, "LightAttackDamage", c->lightDamage);
    PutFloat(ini, "HeavyAttackDamage", c->heavyDamage);
    PutFloat(ini, "MaxHealth", c->maxHealth);
    PutInt(ini, "FatigueZero", c->fatigueZero);
    PutInt(ini, "HyperArmour", c->hyperArmour);
    PutInt(ini, "MariaImmortal", c->mariaImmortal);
    PutInt(ini, "FlashlightColour", c->flashlightColour);
    PutFloat(ini, "FlashlightR", c->torchR);
    PutFloat(ini, "FlashlightG", c->torchG);
    PutFloat(ini, "FlashlightB", c->torchB);
    PutInt(ini, "Floodlight", c->floodlight);
    PutFloat(ini, "FloodlightSize", c->floodSize);
    PutInt(ini, "FlashlightGlowGone", c->glowGone);
    PutInt(ini, "GreatKnifeInInventory", c->knifeInInventory);
    PutInt(ini, "FlashlightInInventory", c->flashlightInInventory);
    PutInt(ini, "Directional2D", c->directional2D);
    PutInt(ini, "ToggleRun", c->toggleRun);
    PutHex(ini, "ToggleRunKey", c->runKey);
    PutHex(ini, "ToggleRunPadButton", c->runPadButton);
    PutFloat(ini, "RunSpeed", c->runSpeed);
    PutInt(ini, "RunAnimationStep", c->runStep);
    PutInt(ini, "RunModeOnAtStart", c->runDefaultOn);
    PutInt(ini, "AllowGameOver", c->allowGameOver);
    PutInt(ini, "ShortTrail", c->shortTrail);
    PutInt(ini, "KnifeHitFollowsBlade", c->knifeHitFollowsBlade);
    PutFloat(ini, "LightAttackWiden", c->lightWiden);
    return true;
}

const char* DefaultIniText()
{
    return
        "; PlayableRed.ini -- settings of PlayableRed.asi (Play As Pyramid Head).\n"
        "; Read when the game starts and again whenever this file changes, so\n"
        "; PlayableRedConfig.exe can be used with the game running.  1 = on, 0 = off.\n"
        "; A damage or health of 0 means: leave the game's own value.\n"
        "\n"
        "[PlayableRed]\n"
        "; James plays Pyramid Head's clips while the Great Knife is equipped\n"
        "; (uses the mod's animation bank, sh2e\\chr\\jms\\jms_wpnata.anm).\n"
        "AnimationTranslation=1\n"
        "; Great Knife swing damage (stock 600) and overhead damage (stock 1000).\n"
        "LightAttackDamage=1200\n"
        "HeavyAttackDamage=10000\n"
        "; The Great Knife's hit line follows the drawn blade (its tip marker turned as the mesh was),\n"
        "; and the light attack's sweep is widened by this many degrees on each side (0 = the game's own).\n"
        "KnifeHitFollowsBlade=1\n"
        "LightAttackWiden=10\n"
        "; Enforced every tick: loading a save cannot lower it.\n"
        "MaxHealth=1000\n"
        "FatigueZero=1\n"
        "; No flinch when hit.\n"
        "HyperArmour=1\n"
        "; Immortal Maria: her death no longer ends the game, and James keeps normal controls after it.\n"
        "MariaImmortal=1\n"
        "; Flashlight colour: red 127 with a trace of green and blue is the red torch.\n"
        "FlashlightColour=1\n"
        "FlashlightR=127\n"
        "FlashlightG=0.000999451\n"
        "FlashlightB=0.000999451\n"
        "; Beam size (stock about 1.0).\n"
        "Floodlight=1\n"
        "FloodlightSize=1.1\n"
        "FlashlightGlowGone=1\n"
        "; Inventory: the item's own flag bit is set when it is found clear (Great Knife = bit 15,\n"
        "; flashlight = bit 18 of the flags at sh2pc.exe+1B7A7E0); every other item is left alone.\n"
        "; Not in room 0x9D (the employee elevator's weight limit: everything must be shelved).\n"
        "GreatKnifeInInventory=1\n"
        "FlashlightInInventory=1\n"
        "Directional2D=1\n"
        "; Run mode: press the key (virtual-key code, 0x10 = Shift) or the pad button\n"
        "; (XInput mask, 0x4000 = X on Xbox, Square on PlayStation) to switch it.\n"
        "ToggleRun=1\n"
        "ToggleRunKey=0x10\n"
        "ToggleRunPadButton=0x4000\n"
        "RunSpeed=4\n"
        "RunAnimationStep=3000\n"
        "RunModeOnAtStart=0\n"
        "; Allow game over: when health reaches 0 the game-over state byte (sh2pc.exe+1BB8117)\n"
        "; is set to 2, the value the game uses for dead.  Only acts with HyperArmour on.\n"
        "AllowGameOver=1\n"
        "; Short Trail: shortens the winding trail at the start (sh2pc.exe+4E700B held at 1).\n"
        "ShortTrail=1\n"
        "\n"
        "; Always on, not settings: No Tripping (the stumble branch at sh2pc.exe+12F057\n"
        "; flipped, needed wherever there are stairs) and the door table (Blue Creek's room\n"
        "; 0x20 -> 0x26 door, which left the player a storey up: put on its floor, 0).\n"
        "; Not settings, but worth knowing: with MaxHealth enforced, a brand new game\n"
        "; (arriving in room 1, the starting bathroom) is set to full health for its first\n"
        "; three seconds; and the whole mod withdraws while Born From a Wish is playing\n"
        "; (chapter byte 1) and returns in the main scenario.\n";
}

uintptr_t SubstituteDescriptor(uintptr_t desc, unsigned char weaponByte, uintptr_t basicLo, uintptr_t basicHi, uintptr_t copyTable)
{
    if (weaponByte != 0x0F) return desc;
    if (desc < basicLo || desc >= basicHi) return desc;
    return copyTable + (desc - basicLo);
}

static void PutRecord(unsigned char* rec, uint16_t count, int16_t rate, uint16_t first, uint16_t last)
{
    memcpy(rec + 2, &count, 2);
    memcpy(rec + 4, &rate, 2);
    memcpy(rec + 6, &first, 2);
    memcpy(rec + 8, &last, 2);
}

void BuildGreatKnifeBasicTable(unsigned char* out, const unsigned char* stock)
{
    memcpy(out, stock, 33 * 12);
    memset(out + 33 * 12, 0, 4);
    // id 101 idle and 113..118, 123 -> PH 5311 reveal (49 frames 957..1005 at 2048); 104 walk -> PH 5302 on 24 frames (LL)
    static const int reveal[] = {101, 113, 114, 115, 116, 117, 118, 123};
    for (int i = 0; i < 8; ++i) PutRecord(out + 12 * (reveal[i] - 101), 49, 2048, 957, 1005);
    PutRecord(out + 12 * (104 - 101), 24, 1408, 722, 745);
}

void WeaponSlotRecord(int slot, int16_t stockRate, unsigned char* out8)
{
    int16_t sign = stockRate < 0 ? -1 : 1;
    unsigned char rec[12] = {0};
    if (slot <= 6)       PutRecord(rec, 1, (int16_t)(2048 * sign), 752, 752);     // ready / retract / stance transitions: one frame
    else if (slot <= 8)  PutRecord(rec, 18, 2048, 752, 769);                      // stance: PH 5310 idle
    else if (slot <= 12) PutRecord(rec, 67, (int16_t)(2048 * sign), 770, 836);    // swing: PH 5303
    else                 PutRecord(rec, 120, (int16_t)(2048 * sign), 837, 956);   // overhead: PH 5304
    memcpy(out8, rec + 2, 8);
}

void DamageWindow(int record, unsigned char* out2)
{
    if (record == 19 || record == 20) { out2[0] = 32; out2[1] = 39; }
    else { out2[0] = 66; out2[1] = 73; }
}

int RunStep(bool runMode, uint32_t charId, float runFlag, int step)
{
    if (!runMode) return -1;
    if (charId >= 0x200) return -1;      // not the player
    if (!(runFlag >= 0.5f)) return -1;   // the game is not asking for a run
    return step;
}

bool InventoryEnforcedIn(uint32_t roomId)
{
    return roomId != 0x9D;
}

bool GameOverDue(float health, unsigned char gameOverState)
{
    return health <= 0.0f && gameOverState != 2;
}

bool NewGameHeal(float maxHealth, uint32_t roomId, uint32_t triggerRoom, uint32_t windowMs, uint32_t nowMs, bool* armed, uint32_t* until)
{
    if (maxHealth <= 0.0f) { *until = 0; return false; }
    if (roomId != triggerRoom) { *armed = true; *until = 0; return false; }
    if (*armed) { *armed = false; *until = nowMs + windowMs; }
    if (!*until) return false;
    if ((int32_t)(nowMs - *until) >= 0) { *until = 0; return false; }   // the deadline passed (wrap-safe)
    return true;
}

bool ScenarioAllows(unsigned char chapterId)
{
    return chapterId != 1;
}

const float KNIFE_TIP_STOCK[3]  = {425.957f, -479.930f, 515.439f};   // wp_nata.mdl bone 1, the file's
const float KNIFE_TIP_TURNED[3] = {197.813f, -234.515f, 517.472f};   // the same point after the mesh's turn

bool KnifeTipFollow(float* t1, unsigned char weaponByte)
{
    if (weaponByte != 0x0F) return false;
    for (int i = 0; i < 3; ++i) { float d = t1[i] - KNIFE_TIP_STOCK[i]; if (d > 0.5f || d < -0.5f) return false; }
    for (int i = 0; i < 3; ++i) t1[i] = KNIFE_TIP_TURNED[i];
    return true;
}

static void TurnAbout(const float* p0, float* p, float rad)
{
    float x = p[0] - p0[0], z = p[2] - p0[2];
    float c = (float)cos(rad), s = (float)sin(rad);
    p[0] = p0[0] + x * c - z * s;
    p[2] = p0[2] + x * s + z * c;
}

void WidenSweep(const float* p0, float* p1, float* p3, float degrees)
{
    if (!(degrees > 0.0f)) return;
    const float PI = 3.14159265f;
    float sweep = (float)atan2(p3[2] - p0[2], p3[0] - p0[0]) - (float)atan2(p1[2] - p0[2], p1[0] - p0[0]);
    while (sweep > PI) sweep -= 2 * PI;
    while (sweep < -PI) sweep += 2 * PI;
    float rad = degrees * PI / 180.0f;
    if (sweep < 0.0f) rad = -rad;          // the sweep runs the other way round: the sides swap
    TurnAbout(p0, p1, -rad);
    TurnAbout(p0, p3, rad);
}

// Blue Creek Apartments: the door from room 0x20 lands the player in the hallway 0x26 at
// y 0 (its floor), and the game's stale floor height of -1705 then lifted them a storey.
// The floor now follows the player at every room change; this entry is the fallback.
static const DoorFix DOOR_FIXES[] = {
    {0x20, 0x26, 0.0f, 1000.0f},
};

const DoorFix* DoorFixFor(uint32_t fromRoom, uint32_t toRoom)
{
    for (size_t i = 0; i < sizeof DOOR_FIXES / sizeof DOOR_FIXES[0]; ++i)
        if (DOOR_FIXES[i].from == fromRoom && DOOR_FIXES[i].to == toRoom) return &DOOR_FIXES[i];
    return NULL;
}

bool DoorFixDue(const DoorFix* f, float y)
{
    return f != NULL && y < f->floorY - f->aboveBy;
}


