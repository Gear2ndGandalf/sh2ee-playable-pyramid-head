# Building Playable Pyramid Head from source

This document is for anyone who wants to build the mod's two executables
themselves and check them against the files uploaded to Nexus Mods. It was
written for a reviewer who has never seen the project: every step is spelled
out, and every claim about the shipped files can be checked with the commands
given.

## 1. What is built, and from what

The mod ships two executables. Everything else in the archive is game data
(models and an animation bank in the game's own file formats), not code.

| shipped file | kind | built from |
|---|---|---|
| `PlayableRed.asi` | a 32-bit Windows DLL, loaded into `sh2pc.exe` by the Enhanced Edition's own ASI loader | `src/src/SH2PlayableRed.cpp`, `src/src/playablered_logic.cpp`, `src/src/playablered_logic.h`, `src/src/SH2PlayableRed.def` |
| `PlayableRedConfig.exe` | a 32-bit Win32 GUI program, the settings window | `src/src/PlayableRedConfig.cpp`, `src/src/playablered_logic.cpp`, `src/src/playablered_logic.h` |

Two test programs are built and run along the way and are not shipped:
`test_logic.exe` (from `src/src/test_logic.cpp`) and `host_test.exe` (from
`src/src/host_test.cpp`).

The whole source is seven files, about 2,300 lines of plain C++ with the Win32
API. There are **no third-party libraries, no package manager, no submodules,
no downloads during the build, and no generated code.** Every `#include` is
either one of these seven files or a header that ships with Visual Studio and
the Windows SDK (`windows.h`, `commctrl.h`, `uxtheme.h`, `gdiplus.h`,
`tlhelp32.h`, `stdio.h`, `stdint.h`, `stdlib.h`, `string.h`, `math.h`).

The build is a single PowerShell script, `src/build.ps1`, that calls `cl.exe`
directly. There is no Visual Studio solution or project file, and no build
system to install. Section 5 gives the equivalent `cl.exe` commands for
building without the script at all.

## 2. Prerequisites

* **Windows 10 or 11** (64-bit is fine; the toolchain cross-compiles to x86).
* **Visual Studio 2022 or 2026, any edition including the free Community
  edition**, with the workload **"Desktop development with C++"**. That
  workload installs the two things the script looks for:
  * the MSVC x86/x64 build tools (component id
    `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`), and
  * a Windows 10 or 11 SDK (any version; the script picks the newest one
    under `C:\Program Files (x86)\Windows Kits\10`).

  The "Build Tools for Visual Studio" package works too, with the same
  workload. `vswhere.exe`, which the script uses to find the toolchain, is
  installed by the Visual Studio Installer itself.
* Optional: a copy of **Silent Hill 2 PC (2001) with the Enhanced Edition**,
  for the second test program. `host_test.exe` reads the game's `sh2pc.exe`
  (it never writes it), loads the freshly built plugin against a private copy
  of that executable in memory, and checks every patch. Without the game the
  build still works; that one test is skipped with a switch.

Nothing else is needed. No .NET, no DirectX SDK, no Python, no CMake.

The 1.0 release on Nexus Mods was built on Windows 11 with:

| | |
|---|---|
| Visual Studio | Community 2026, version 18.4.11626.88 |
| MSVC toolset | 14.50.35717 (linker version 14.50, as the shipped PE headers say) |
| Windows SDK | 10.0.26100.0 |
| Target | x86 (32-bit), `/O2`, static C runtime (the default for `/O2` without `/MD`) |

## 3. Building with the script

Open PowerShell and run:

```powershell
git clone https://github.com/Gear2ndGandalf/sh2ee-playable-pyramid-head.git
cd sh2ee-playable-pyramid-head
powershell -ExecutionPolicy Bypass -File src\build.ps1 -SkipHostTest
```

(`-ExecutionPolicy Bypass` only lets this one invocation run an unsigned
script; it changes nothing on the machine.)

The script prints the toolchain it found and then, in order:

1. compiles and runs `test_logic.exe`, which must end with
   `ALL PASSED (0 failures)`;
2. compiles the plugin to `src\obj\PlayableRed.asi`;
3. would run `host_test.exe` against the game; with `-SkipHostTest` it prints
   `host test skipped (-SkipHostTest)` instead;
4. copies the plugin to `src\out\PlayableRed.asi` and compiles the settings
   window to `src\out\PlayableRedConfig.exe`, then prints
   `built <path>\PlayableRed.asi and <path>\PlayableRedConfig.exe`.

The build takes a few seconds. Output lands in `src\out\`; intermediate files
are in `src\obj\`. Both folders are ignored by git.

### With the game installed

If Silent Hill 2 is installed, point the script at the folder that contains
`sh2pc.exe` so that the second test program runs as well:

```powershell
powershell -ExecutionPolicy Bypass -File src\build.ps1 -Game "C:\Games\Silent Hill 2" -Out src\out
```

`host_test.exe` prints `mapped a copy of sh2pc.exe at ... (34471936 bytes)`,
one `ok:` line per check, and `ALL PASSED (0 failures)`. Without `-Out` the
finished files are copied into the game folder itself, which is the mod's
own install location; `-Test` runs the tests and ships nothing.

If the script is run with neither `-Game` nor `-SkipHostTest` and no game is
found, it stops with `sh2pc.exe not found. Pass -Game <folder containing
sh2pc.exe> to run the host test, or -SkipHostTest to build without the game.`

### Switches

| switch | meaning |
|---|---|
| `-SkipHostTest` | build without the game; only `test_logic.exe` runs |
| `-Game <folder>` | the folder containing `sh2pc.exe`; enables `host_test.exe` |
| `-Out <folder>` | where the two finished files go (default: the game folder if known, else `src\out\`) |
| `-Test` | run the tests, ship nothing |

## 4. What the two test programs check

* **`test_logic.exe`** links the shared logic file and checks its decisions
  without any game: which animation descriptor is substituted for which
  weapon, the private copy of the game's clip table against the byte values
  expected from the 1.0 executable, the damage and run-speed rules, and that
  a settings file written by the code reads back identically.
* **`host_test.exe`** maps the sections of `sh2pc.exe` into a private,
  writable region of its own memory (the file on disk is only read), loads
  `PlayableRed.asi` with `LoadLibrary` exactly as the game's plugin framework
  does, hands it that region as if it were the running game, and checks that
  every hook site now holds a jump into the DLL, that every patched table and
  constant has the expected value, that a value the "game" changes is written
  back within 200 ms, and finally that switching everything off in the
  settings file (and calling `UnloadPlugin`) restores the code and read-only
  data byte for byte. It is the reason the plugin has a fourth export,
  `PlayableRedTestBase`, which the game never calls.

## 5. Building without the script

The script does nothing that cannot be typed by hand. From an **"x86 Native
Tools Command Prompt for VS"** (Start menu, Visual Studio folder; it sets up
`cl.exe` for 32-bit output), in the `src` folder of the checkout:

```bat
mkdir obj
set CF=/nologo /O2 /W3 /EHsc /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /Foobj\

rem 1. the logic tests
cl %CF% src\test_logic.cpp src\playablered_logic.cpp /Fe:obj\test_logic.exe /link user32.lib
obj\test_logic.exe

rem 2. the plugin
cl %CF% /LD src\SH2PlayableRed.cpp src\playablered_logic.cpp /Fe:obj\PlayableRed.asi /link /DEF:src\SH2PlayableRed.def user32.lib

rem 3. the settings window
cl %CF% src\PlayableRedConfig.cpp src\playablered_logic.cpp /Fe:obj\PlayableRedConfig.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib uxtheme.lib gdiplus.lib

rem 4. (optional, needs the game) the harness
cl %CF% src\host_test.cpp src\playablered_logic.cpp /Fe:obj\host_test.exe /link user32.lib
obj\host_test.exe "%CD%\obj\PlayableRed.asi" "C:\Games\Silent Hill 2\sh2pc.exe"
```

The finished files are `obj\PlayableRed.asi` and `obj\PlayableRedConfig.exe`.
For step 4 the plugin and `host_test.exe` must sit in the same folder,
because the harness writes its throw-away settings file relative to the
plugin's path and the plugin looks for it relative to the host program's;
the script keeps both in `obj\` for that reason.

## 6. Checking a build against the shipped 1.0 files

The files uploaded to Nexus Mods are the ones in `mod/` and inside
`release/Playable_Pyramid_Head_1.0.zip` in this repository (identical bytes).

| file | size | MD5 | SHA-256 |
|---|---|---|---|
| `PlayableRed.asi` | 166,912 | `cce5fa8ba9c743cb52e9929802d8fcd2` | `82dca071fac7dc37c94db88b7fbf28bdfb2af3c914249f0cf124c70808f32946` |
| `PlayableRedConfig.exe` | 162,816 | `5c93632abdf70ae60872c4c5d9c3f9c0` | `bf0f973b7c0446e44f4f8bc5139427f21843854ca594ed0fecd9913975eb7261` |

A rebuild with the same MSVC toolset (14.50) produces files of exactly the
same size that differ from the shipped ones in **four bytes each**: the
`TimeDateStamp` in the PE file header and the timestamp in the debug
directory, both of which the linker sets to the build time. Everything else,
code and data, is identical. A different MSVC version will produce a
functionally identical but not byte-identical file, as usual with native
code.

To compare, `dumpbin` ships with Visual Studio (run it from the same
developer prompt):

```bat
dumpbin /headers PlayableRed.asi        rem machine (x86), linker version, timestamp
dumpbin /imports PlayableRed.asi        rem the DLLs it links against
dumpbin /exports PlayableRed.asi        rem LoadPlugin, PlayableRedTestBase, UnloadPlugin, Update
fc /b PlayableRed.asi <rebuilt>\PlayableRed.asi
```

Expected imports, for the shipped files and for any rebuild:

| file | imports |
|---|---|
| `PlayableRed.asi` | `KERNEL32.dll`, `USER32.dll` |
| `PlayableRedConfig.exe` | `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `COMCTL32.dll`, `UxTheme.dll`, `gdiplus.dll` |

All of these are part of Windows. Neither file imports or loads any
networking library (no `WS2_32`, `WININET` or `WINHTTP`), and the source
contains no networking, registry, process-creation or shell calls. The one
library loaded by name at run time is XInput (`xinput1_4`, `xinput1_3` or
`xinput9_1_0`, whichever Windows has), for the optional gamepad run-toggle
button.

## 7. What the executables do, for a security review

* **`PlayableRed.asi`** runs inside `sh2pc.exe` only, loaded by the Enhanced
  Edition's ASI loader. It patches the game's own code and data in memory:
  each site is compared byte for byte against the values expected from the
  game's 1.0 executable before anything is written, and a site that does not
  match is refused and named in the log. Patches are recorded with their
  original bytes and reverted when a feature is switched off or the plugin
  is unloaded. It touches no other process. On disk it writes exactly two
  files, both under `<game>\plugins\PlayableRedMod\`: `PlayableRed.ini`
  (created with defaults on first run, then only read) and `PlayableRed.log`.
* **`PlayableRedConfig.exe`** is the settings window. It reads and writes
  `PlayableRed.ini` in the same folder as above, next to wherever it sits,
  and does nothing else. The plugin notices a changed file within a second.

The binaries are not packed, obfuscated or signed; `dumpbin` and any
disassembler show them as ordinary MSVC output.

## 8. The data files

`mod/sh2e/chr/jms/*.mdl`, `mod/sh2e/chr/wp/*.mdl` and
`mod/sh2e/chr/jms/jms_wpnata.anm` are models and an animation bank in Silent
Hill 2's own formats, loaded by the game's renderer from the Enhanced
Edition's `sh2e\` override folder. They contain Pyramid Head's model fitted to
James's skeleton and Pyramid Head's animations, made in Blender with a custom
import/export add-on; they are data, not code, and are not produced by the
build. Nothing in the game's `data\` folder is modified.
