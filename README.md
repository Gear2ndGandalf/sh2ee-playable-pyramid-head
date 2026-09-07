# Playable Pyramid Head

*Silent Hill 2: Enhanced Edition* — play as Pyramid Head.

James wears the Red Pyramid Thing's model in every room and cutscene variant,
and while the Great Knife is equipped he moves the way Pyramid Head does: his
idle, his walk, his stance, his swing and his overhead, the knife held at his
angle. A red flashlight to go with it, and a settings window for every part of
it.

Nothing in the game's own files is changed. The models and the animation bank
live in `sh2e\`, and a plugin patches the game in memory while it runs, so
deleting the files is the uninstall. No other mod and no loader beyond the
Enhanced Edition's own is needed.

## Requirements

* Silent Hill 2 for PC (2001), the 1.0 executable Enhanced Edition uses. The
  plugin checks every byte it patches against that executable and refuses
  anything else.
* [Silent Hill 2: Enhanced Edition](https://enhanced.townofsilenthill.com/SH2/).
  In `SH2EEconfig.exe`, Advanced tab, tick **Enable loading ASI plugins** and
  leave **Only load ASI plugins from the plugin folders** unticked (the
  default), then save.
* Windows 7, 8, 8.1, 10 or 11, 32- or 64-bit. Nothing else to install.
* If you used the "Play As Pyramid Head" Cheat Engine table before: untick it.

## Install

Extract `release/Playable_Pyramid_Head_1.0.zip` (or copy the contents of
`mod/`) straight into the Silent Hill 2 folder, the one with `sh2pc.exe`:

    PlayableRed.asi              the plugin
    PlayableRedConfig.exe        the settings window
    sh2e\chr\jms\                the Pyramid Head models, their twins, the animation bank
    sh2e\chr\wp\                 the Great Knife at his angle

Start the game once; the settings file `plugins\PlayableRedMod\PlayableRed.ini`
and the log `plugins\PlayableRedMod\PlayableRed.log` appear. Run
`PlayableRedConfig.exe` to change anything, even with the game running.

Keep one copy of `PlayableRed.asi`: the loader also scans `plugins\`, and a
second copy there would start a second instance.

## Settings

| setting | what |
|---|---|
| Pyramid Head's animations | his idle, walk, stance, swing and overhead while the Great Knife is equipped; other weapons keep James's own |
| Light / Heavy attack damage | Great Knife swing / overhead damage (stock 600 / 1000; 0 = stock) |
| Great knife hit line follows the blade | the game's hit line is tied to the knife's tip marker; it follows the drawn blade |
| Light attack widen (degrees) | widens the light attack's sweep each side |
| Max health | held at this value; a new game starts at full health |
| Fatigue zero | no tiring |
| Hyper armour, Allow game over | no flinch when hit; with Allow game over, dying still ends the game |
| Immortal Maria | Maria cannot be killed; controls stay normal |
| Flashlight colour, Floodlight, Flashlight glow gone | the torch's colour, beam size and lens glow |
| Great knife / Flashlight in inventory | the item is added when it is missing; nothing else is touched |
| 2D directional controls | the game's 2D control scheme, held on |
| Toggle run | run mode on Shift / X / Square, with its speed and animation step |
| Short Trail | shortens the winding trail at the start |

Always on: No Tripping, the floor height following the player at every room
change, and the inventory items left alone in the hospital's weight-limit
elevator. The whole mod withdraws while Born From a Wish is playing.

## Building

`src/build.ps1` builds `PlayableRed.asi` and `PlayableRedConfig.exe` with the
x86 MSVC toolchain (Visual Studio with the C++ workload and a Windows 10 SDK),
after two test programs pass: `test_logic.exe` checks the decisions, and
`host_test.exe` loads the plugin against a private copy of `sh2pc.exe` and
checks every hook, every value and the way back. See `src/README.md`.

## Layout

    mod/        the files as installed into the game folder
    src/        the plugin's source, tests and build script
    release/    the archives uploaded to Nexus Mods, the manifest, the page text
    CREDITS.txt everyone this rests on

## Credits

Gear2 (the mod, the research, the model work, the testing) and Claude
(Anthropic) (the formats, the tools, the plugin). Silent Hill 2 is Konami's.
The Enhanced Edition team, the Silent Hill Museum, alanm1, Rich Whitehouse,
descawed, Cheat Engine, Blender and Capstone made the work possible. The full
list is in [CREDITS.txt](CREDITS.txt).

## License

MIT, see [LICENSE](LICENSE). The game's assets remain Konami's.
