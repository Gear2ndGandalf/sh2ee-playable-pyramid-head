PLAYABLE PYRAMID HEAD  --  Silent Hill 2: Enhanced Edition  --  version 1.0
=============================================================================

James plays as Pyramid Head with the Great Knife: Pyramid Head's model in every
room, his own idle, walk, stance, swing and overhead while the Great Knife is
equipped, the knife held at his angle, a red flashlight, and a settings window
for every part of it.  Nothing in the game's own files is changed: the mod's
models and animation bank live in sh2e\ and the game is patched in memory by a
plugin while it runs, so removing the files removes the mod.  It needs no other
mod and no loader beyond the Enhanced Edition's own.


REQUIREMENTS
------------
* Silent Hill 2 for PC (2001), the 1.0 executable Enhanced Edition uses.  The
  plugin checks every byte it patches against that executable and refuses
  anything else, so no other version is harmed -- it just does nothing and
  says so in its log.
* Silent Hill 2: Enhanced Edition (https://enhanced.townofsilenthill.com/SH2/),
  which loads the mod's files from sh2e\ and the plugin from the game folder.
  In SH2EEconfig.exe, Advanced tab, tick "Enable loading ASI plugins" and
  leave "Only load ASI plugins from the plugin folders" unticked (the default),
  then save -- once; the same switch Heaven's Knife and other ASI mods use.
* Windows 7, 8, 8.1, 10 or 11, 32-bit or 64-bit.  The plugin and the settings
  window are built with a static runtime and use only user32, kernel32, gdi32,
  comctl32, uxtheme and gdiplus, all part of Windows.  No .NET, no DirectX
  SDK.  XInput (for the run-mode pad button) is found by name -- xinput1_4,
  xinput1_3 or xinput9_1_0 -- and Windows 7 has the last one built in.
* If you used the "Play As Pyramid Head" Cheat Engine table before: untick it.
  The plugin refuses a code site that is already patched, and says which.


INSTALL
-------
1. Extract the archive straight into your Silent Hill 2 folder (the one with
   sh2pc.exe), so that these land in place:
       PlayableRedConfig.exe
       PlayableRed.asi
       sh2e\chr\jms\  (thirteen files: the four James models, their eight
                       no-texture and mirrored twins, and the animation bank)
       sh2e\chr\wp\   (the Great Knife, wp_nata.mdl and rwp_nata.mdl)
2. Start the game.  The settings file plugins\PlayableRedMod\PlayableRed.ini
   is written with the defaults on the first run, and the log
   plugins\PlayableRedMod\PlayableRed.log says what the plugin did.
3. Run PlayableRedConfig.exe (it can stay open while the game runs) to change
   anything: the plugin picks a saved change up within a second.

UNINSTALL: delete PlayableRedConfig.exe, PlayableRed.asi, the
plugins\PlayableRedMod\ folder, and the files listed above under sh2e\chr\jms\
and sh2e\chr\wp\.  The game then uses its own files from data\ again.


SETTINGS  (PlayableRedConfig.exe, or PlayableRed.ini by hand; 1 = on)
---------------------------------------------------------------------
Pyramid Head's animations       his idle, walk, stance, swing and overhead while
                                the Great Knife is equipped; other weapons keep
                                James's own
Light / Heavy attack damage     Great Knife swing / overhead damage (stock 600 /
                                1000; 0 = stock)
Great knife hit line follows    the game's hit line is tied to the knife's tip
  the blade                     marker; it follows the drawn blade
Light attack widen (degrees)    widens the light attack's sweep each side (10)
Max health                      held at this value (1000; 0 = the game's own);
                                a new game starts at full health
Fatigue zero                    no tiring
Hyper armour                    no flinch when hit
  Allow game over               with Hyper armour: dying still ends the game
Immortal Maria                  Maria cannot be killed; controls stay normal
Flashlight colour, red/green/   the torch's colour (127 / 0.001 / 0.001 is the
  blue                          red torch)
Floodlight, beam size           the beam's size (1.1)
Flashlight glow gone            no lens glow
Great knife in inventory        the item is added when it is missing; nothing
Flashlight in inventory         else in the inventory is touched
2D directional controls         the game's 2D control scheme, held on
Toggle run, key, pad button,    run mode: press Shift (0x10) or X / Square
  run speed, run animation      (0x4000) to switch; speed 4, animation step
  step, on at start             3000
Short Trail                     shortens the winding trail at the start

Always on, no setting: No Tripping (no stumble when a room with stairs loads),
the floor height following the player at every room change (a door onto
another storey no longer leaves him in the air), and the inventory items are
not added in the hospital's employee elevator (room 0x9D), whose weight limit
needs everything shelved.

The whole mod withdraws while Born From a Wish is playing and returns in the
main scenario, so Maria keeps her own model, clips, inventory and health.


NOTES
-----
* The plugin patches the game in memory.  Some antivirus programs dislike
  that; the source is included so anyone can build the same file.
* Other mods: every code site is checked byte for byte before it is
  patched, so a site another mod has already changed is refused, named in
  the log, and left to that mod; nothing else is touched.
* Keep one copy of PlayableRed.asi.  The loader also scans plugins\, and a
  second copy there would start a second instance that refuses every site the
  first already patched.
* The log names anything the plugin refused and why.  If something does not
  work, that file is the first thing to look at.


CREDITS
-------
See CREDITS.txt.
