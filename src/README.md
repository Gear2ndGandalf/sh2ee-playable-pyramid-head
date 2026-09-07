# Playable Pyramid Head -- PlayableRed.asi

Everything the Cheat Engine group "Play As Pyramid Head" did, as a plugin the
game loads itself.  No Cheat Engine, no table to tick.  **Untick that Cheat
Engine group before the first run**: the plugin refuses a code site whose bytes
are not the game's own, and says so in the log.

## Files

    <game>\PlayableRedConfig.exe                 the settings window (dark, green), next to sh2pc.exe
    <game>\PlayableRed.asi                    the plugin, loaded by the Enhanced Edition's own ASI loader
                                                 (SH2EEconfig.exe > Advanced > "Enable loading ASI plugins")
    <game>\plugins\PlayableRedMod\
        PlayableRed.ini                          the settings (written with defaults on first run)
        PlayableRed.log                       what the plugin did this run, and anything it refused
        build.ps1, src\                          source, tests and build

No other mod or loader is needed.  The Enhanced Edition's loader takes .asi
files from the game folder and from `plugins\`, and "Only load ASI plugins
from the plugin folders" (SH2EEconfig, Advanced) must stay unticked for the
game folder to be scanned -- the default.  Keep ONE copy of the file: a second
one in `plugins\` would boot a second instance, which refuses every site the
first already patched.  The settings folder is `plugins\PlayableRedMod\`
wherever the file sits, since it is derived from the game exe's folder.

The models and the animation bank are separate files of the mod: the plugin
only redirects the game's clip table, so `sh2e\chr\jms\jms_wpnata.anm` must
be the mod's bank and the `sh2e\chr\jms\*_jms.mdl` files the mod's Pyramid
Head models.

## Settings

Run `PlayableRedConfig.exe`, change what you like, press Save.  The plugin
re-reads the file within a second, with the game running, and turns each
feature on or off to match -- code patches are reverted, not just skipped.
The same keys can be edited by hand in `PlayableRed.ini`.

| key | what |
|---|---|
| `AnimationTranslation` | Pyramid Head's idle, walk, stance, swing and overhead for James while the Great Knife is equipped (the knife byte at sh2pc.exe+1B7A809 == 0F); other weapons keep James's own clips |
| `LightAttackDamage`, `HeavyAttackDamage` | Great Knife swing / overhead damage (stock 600 / 1000); 0 = stock |
| `KnifeHitFollowsBlade` | the Great Knife's hit line runs from its bone 0 (the grip) toward bone 1 (a tip marker); the mesh was turned 40.2 degrees into Pyramid Head's grip but the marker was not, leaving the swung line 30 degrees off the drawn blade. A hook in the tip getter (sh2pc.exe+1463BA) moves the marker from (426, -480, 515) to (198, -235, 517), where the same turn puts it, while the Great Knife is equipped |
| `LightAttackWiden` | degrees added to each side of the light attack's sweep (attack records 19 and 20): the hit primitive the game pushes (sh2pc.exe+144049) goes in as a copy with last frame's far point turned back and this frame's turned on, about the near point around the vertical |
| `MaxHealth` | enforced every 30 ms, so a loaded save cannot lower it; 0 = leave the game's. While enforced, a brand new game is set to full health for its first three seconds after arriving in room 1 (the starting bathroom), so the low-health indicator does not show at the start |
| `FatigueZero` | fatigue held at 0 |
| `HyperArmour` | the flinch branch flipped (no stagger when hit) |
| `FlashlightColour`, `FlashlightR/G/B` | the torch's colour; 127 / 0.001 / 0.001 is the red torch |
| `Floodlight`, `FloodlightSize` | beam size |
| `FlashlightGlowGone` | the two 0.5 glow constants set to 0 |
| `GreatKnifeInInventory`, `FlashlightInInventory` | the item's own bit in the inventory flags (three 32-bit words at sh2pc.exe+1B7A7E0, one bit per item: Great Knife bit 15, flashlight bit 18) is set whenever it is found clear; nothing else in those bytes is written, so other weapons and items are kept. Not in room 0x9D, the hospital's employee elevator with the weight limit, where everything must be shelved |
| `Directional2D` | the 2D control byte, enforced |
| `ToggleRun`, `ToggleRunKey`, `ToggleRunPadButton`, `RunSpeed`, `RunAnimationStep`, `RunModeOnAtStart` | run mode: press the key (virtual-key code; 0x10 = Shift) or the pad button (XInput mask; 0x4000 = X on an Xbox pad, Square on a PlayStation pad through Xidi or Steam) to switch; while on, travel speed is `RunSpeed` and the walk animation steps `RunAnimationStep` per tick |
| `MariaImmortal` | Immortal Maria: the branch that turns Maria's death into a game over is skipped (sh2pc.exe+12CC95 `test al,al` -> `test al,0`, the `je` always taken), and the byte that leaves James with the after-her-death controls (sh2pc.exe+1BB0F4C) is forced to 0 where the game reads it (+12A35F), so the controls stay normal |
| `AllowGameOver` | when health (sh2pc.exe+1BB113C) reaches 0, the game-over state byte (sh2pc.exe+1BB8117) is set to 2, the value the game uses for dead. A child of `HyperArmour`: it only acts while that is on |
| `ShortTrail` | sh2pc.exe+4E700B held at 1 |

## Always on: the room-load fixes

Two things the plugin does whenever it is active, with no key and no button,
because James as Pyramid Head needs them wherever there are stairs:

* **No Tripping** -- the stumble the game asks for when the floor is found
  more than 250 units below the feet (the `je` at sh2pc.exe+12F057) is flipped
  to `jne`, exactly as the Cheat Engine script of that name did, so a room with
  stairs loads without a trip.
* **The floor follows the player at every room change.**  The game keeps
  the floor height under the player across a room change, and a door that
  lands the player on another storey (Blue Creek 0x20 -> 0x26 places them at
  y 0 while the old floor was -1705; Y grows downward) leaves the gravity
  routine lifting them to the old height when its first probe in the new
  room finds nothing -- the player then stands in the air above the
  hallway.  On the tick the room id changes, the floor height (the player
  object's and the global) is set to the player's y, as the game's own first
  placement does.
* **The door table** -- the fallback for a door that still leaves the player
  a storey up: for three seconds after the room changes from the listed room
  to the listed room, a player more than a metre above the listed floor is
  put on it (position, stored position, floor height and vertical speed).
  One entry: Blue Creek Apartments 0x20 -> 0x26, floor 0.
  The log also records every room change and every vertical jump of more
  than 500 units in one tick -- position, floor, probe result, material and
  the two clips playing -- so another such door can be added from the log.


All of these stay on whatever the .ini says -- an all-zero file turns every keyed
feature off and leaves these -- and are withdrawn only with everything else
in Born From a Wish and on unload.

## Born From a Wish

The whole mod withdraws while the Born From a Wish scenario is playing
(the chapter byte at sh2pc.exe+19BC00C is 1) and returns when the main
scenario is: every patch reverted, nothing enforced, so Maria keeps her own
clips, inventory and health.  The log says when it switches.

## What "enforced" means

The game rewrites some of its own state -- loading a save restores that
save's max health, the game-over toggle is set by the game.  Those values
are compared on every tick of the plugin's own thread and written back when
they differ, and the log says so each time.  Code patches and `.rdata`
constants are written once and reverted when the feature is turned off.

## Safety

Every code site is checked byte for byte against what this build expects
before it is written; a site that differs is refused and named in the log,
never patched.  While code bytes change, the process's other threads are
held, and the write waits while any of them is stopped inside those bytes.  The animation redirection substitutes the descriptor
pointer in the clip setter (sh2pc.exe+138CD0) for one in a private copy of
the basic table, so the stock table is never written.

## Building and testing

    powershell -File build.ps1          # tests, then the .asi and the config tool into the game root
    powershell -File build.ps1 -Test    # tests only
    powershell -File build.ps1 -Out X   # the .asi somewhere else (the loaded one is locked while the game runs)

Two test programs run before anything ships, neither needing the game:
`test_logic.exe` checks the decisions (the descriptor substitution, the copy
of the basic table against the real exe bytes, every weapon slot, the damage
windows, the run rule, the .ini round trip); `host_test.exe` maps a private
copy of sh2pc.exe, loads the DLL exactly as the framework does -- `LoadPlugin`
called at once, before the plugin's own thread can run -- hands it the copy,
and checks every hook is a jump into the DLL, every table and value is
written, a value the "game" changes is corrected within 200 ms, and an
all-off config (and `UnloadPlugin`) puts the code and rdata back byte for byte.

Needs Visual Studio with the C++ workload (x86 toolset) and a Windows 10 SDK.
