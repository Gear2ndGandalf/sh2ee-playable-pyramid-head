// playablered_logic.h -- the decisions of SH2PlayableRed, free of the game and of Windows hooks,
// so test_logic.exe can check them without Silent Hill 2 running.
#pragma once
#include <stdint.h>

#define PLAYABLERED_VERSION "1.0"

struct Config {
    bool  animation;           // Pyramid Head's clips for James while the Great Knife is out
    float lightDamage;         // Great Knife swings (attack records 19, 20); 0 = stock 600
    float heavyDamage;         // overheads (21, 22); 0 = stock 1000
    float maxHealth;           // enforced; 0 = leave the game's
    bool  fatigueZero;         // enforced 0
    bool  hyperArmour;         // the flinch branch flipped
    bool  mariaImmortal;       // Maria's death no longer ends the game (+12CC95) and the controls stay normal after it (+12A35F)
    bool  flashlightColour;    // the three colour stores overwritten
    float torchR, torchG, torchB;
    bool  floodlight;          // beam size hook
    float floodSize;
    bool  glowGone;            // the two 0.5 glow constants -> 0
    bool  knifeInInventory;    // inventory flag bit 15 (byte +1B7A7E1 mask 0x80) set when clear; other items untouched
    bool  flashlightInInventory; // inventory flag bit 18 (byte +1B7A7E2 mask 0x04) set when clear
    bool  directional2D;       // control byte 1, enforced
    bool  toggleRun;           // the two run hooks, switched by a key / pad button
    int   runKey;              // virtual-key code, default VK_SHIFT (0x10)
    int   runPadButton;        // XInput button mask, default X (0x4000) -- Square on a PlayStation pad
    float runSpeed;            // travel speed while running (stock walk 1.5)
    int   runStep;             // animation step per tick while running (stock ~1408)
    bool  runDefaultOn;        // run mode on when the game starts
    bool  allowGameOver;       // when health reaches 0, the game-over state byte is set to 2 (dead); only with hyperArmour
    bool  shortTrail;          // sh2pc.exe+4E700B = 1, enforced
    bool  knifeHitFollowsBlade; // the Great Knife's tip marker (bone 1) turned as the mesh was, so the hit line runs along the drawn blade
    float lightWiden;          // degrees added to each side of the light attack's sweep (attack records 19, 20); 0 = the game's own
    bool  roomFixes;           // the room-load fixes (No Tripping, the door table): always on, not in the .ini; off only in the all-off config
};

// XInput's state, declared here so nothing links against xinput*.lib (it is loaded by name at run time).
struct XINPUT_STATE_MIN {
    unsigned long dwPacketNumber;
    struct { unsigned short wButtons; unsigned char bLeftTrigger, bRightTrigger; short sThumbLX, sThumbLY, sThumbRX, sThumbRY; } Gamepad;
};

void        ConfigDefaults(Config* c);
void        ConfigAllOff(Config* c);
bool        LoadConfig(const char* iniPath, Config* c);     // defaults for anything missing; false if the file is absent
bool        SaveConfig(const char* iniPath, const Config* c);
const char* DefaultIniText();

// The clip-setter substitution: the descriptor to hand the setter, given the one the game passed.
uintptr_t SubstituteDescriptor(uintptr_t desc, unsigned char weaponByte, uintptr_t basicLo, uintptr_t basicHi, uintptr_t copyTable);

// Our copy of the basic descriptor table: the 33 stock records (396 bytes) with nine replaced, then a 0 terminator.
void BuildGreatKnifeBasicTable(unsigned char* out400, const unsigned char* stock396);

// The Great Knife's weapon slot 1..16: the 8 bytes (count, rate, first, last) written at record+2.
void WeaponSlotRecord(int slot, int16_t stockRate, unsigned char* out8);

// The damage window (first, last frame) for attack records 19..22.
void DamageWindow(int record, unsigned char* out2);

// The run hook's decision: the animation step to force, or -1 to keep the game's own.
int RunStep(bool runMode, uint32_t charId, float runFlag, int step);

// The inventory items (Great Knife, flashlight) are verified and added everywhere but room 0x9D:
// the hospital's employee elevator has a weight limit and the player must shelve everything.
bool InventoryEnforcedIn(uint32_t roomId);

// Allow Game Over: should the game-over state byte be set to 2 now?  (health at or below 0, and not already 2)
bool GameOverDue(float health, unsigned char gameOverState);

// New-game top-up: true on every tick of a short window that opens when the room id ARRIVES at
// `triggerRoom` (1 = R_BEGIN_BATHROOM, where a brand new game begins) -- the game finishes writing
// the starting health during those first frames, so one write is not enough and pinning it for
// the whole visit would be too much.  `armed` re-arms whenever the room is not the trigger;
// `countdown` is the caller's tick counter.  Never when max health is not enforced.
// `nowMs` is the caller's clock; `until` the deadline it keeps (0 = no window open).
bool NewGameHeal(float maxHealth, uint32_t roomId, uint32_t triggerRoom, uint32_t windowMs, uint32_t nowMs, bool* armed, uint32_t* until);

// The scenario gate: the mod applies in the main scenario only, never in Born From a Wish (chapter byte 1).
bool ScenarioAllows(unsigned char chapterId);

// The Great Knife's hit line runs from its bone 0 (the grip) toward its bone 1 (a tip marker).
// The mesh was turned 40.2 degrees into Pyramid Head's grip (mdl_rotate_weapon.py) but the
// marker was not, so the game swings a line 30 degrees off the drawn blade.  `t1` is bone 1's
// translation in the weapon's own space as the game read it: when it is the stock knife's and
// the Great Knife is equipped (weapon byte 0x0F) it becomes where the same turn puts it.
extern const float KNIFE_TIP_STOCK[3];
extern const float KNIFE_TIP_TURNED[3];
bool KnifeTipFollow(float* t1, unsigned char weaponByte);   // true when substituted

// A melee hit primitive sweeps the blade line from last frame's far point p1 to this frame's
// p3, both about the near point p0.  Turn p1 back and p3 on by `degrees` about the vertical
// axis through p0 (Y is the vertical); the sweep's own direction says which way is on, and a
// standing blade widens both ways.  Heights are kept.
void WidenSweep(const float* p0, float* p1, float* p3, float degrees);

// The door table.  A door whose far side leaves the player a storey too high (Y grows
// DOWNWARD in this engine: a smaller y is higher up): when the room changes from `from`
// to `to` and, inside the window the caller keeps, the player's y is more than `aboveBy`
// above `floorY`, the player is put on floorY.
struct DoorFix { uint32_t from, to; float floorY, aboveBy; };
const DoorFix* DoorFixFor(uint32_t fromRoom, uint32_t toRoom);   // NULL when that transition has no entry
bool DoorFixDue(const DoorFix* f, float y);                      // y < floorY - aboveBy

