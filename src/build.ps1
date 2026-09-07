# build.ps1 -- builds PlayableRed.asi and PlayableRedConfig.exe with the x86 MSVC
# toolchain found through vswhere (Visual Studio 2022 or later with the "Desktop
# development with C++" workload and a Windows 10/11 SDK).  No other dependency.
#
# Nothing ships before two test programs pass:
#   test_logic.exe   the decisions (descriptor substitution, tables, run rule, the .ini);
#                    needs nothing but this source
#   host_test.exe    the DLL itself, loaded the way the game's framework loads it,
#                    patching a private copy of sh2pc.exe -- every hook, every value,
#                    and the way back.  Needs the game's sh2pc.exe (read only; the
#                    file is never written), so it is skipped with -SkipHostTest.
#
#   powershell -File build.ps1 -SkipHostTest         build from a plain checkout, no game needed;
#                                                    output in src\out\
#   powershell -File build.ps1 -Game "C:\...\SH2"    run the host test against that folder's
#                                                    sh2pc.exe, then build into that folder
#   powershell -File build.ps1 -Game ... -Out X      ... but put the two files in folder X
#                                                    (a loaded .asi is locked while the game runs)
#   powershell -File build.ps1 -Game ... -Test       tests only, nothing shipped
#
# Without -Game, the game folder is taken to be two levels above this script
# (the mod's own layout, <game>\plugins\PlayableRedMod\build.ps1) if sh2pc.exe is
# there; otherwise there is no game and -SkipHostTest is required.
param([string]$Game = "", [string]$Out = "", [switch]$Test, [switch]$SkipHostTest)

Set-Location $PSScriptRoot

# --- toolchain ---------------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Write-Error "vswhere.exe not found; install Visual Studio with the 'Desktop development with C++' workload."; exit 1 }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { Write-Error "No Visual Studio with the C++ x86/x64 build tools found."; exit 1 }
$vcToolsRoot = Join-Path $vsPath "VC\Tools\MSVC"
$msvcVer = (Get-ChildItem $vcToolsRoot | Sort-Object Name -Descending | Select-Object -First 1).Name
$msvcDir = Join-Path $vcToolsRoot "$msvcVer\bin\Hostx86\x86"
$cl = Join-Path $msvcDir "cl.exe"
if (-not (Test-Path $cl)) { Write-Error "cl.exe not found at $cl"; exit 1 }
$wkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
if (-not (Test-Path "$wkRoot\Include")) { Write-Error "No Windows 10/11 SDK under $wkRoot; add one in the Visual Studio Installer."; exit 1 }
$sdkVer = (Get-ChildItem "$wkRoot\Include" | Sort-Object Name -Descending | Select-Object -First 1).Name
$env:INCLUDE = @("$vcToolsRoot\$msvcVer\include", "$wkRoot\Include\$sdkVer\um", "$wkRoot\Include\$sdkVer\shared", "$wkRoot\Include\$sdkVer\ucrt") -join ";"
$env:LIB = @("$vcToolsRoot\$msvcVer\lib\x86", "$wkRoot\Lib\$sdkVer\um\x86", "$wkRoot\Lib\$sdkVer\ucrt\x86") -join ";"
$env:PATH = "$msvcDir;" + $env:PATH
Write-Host "toolchain: $vsPath  MSVC $msvcVer  SDK $sdkVer  (x86)"

# --- where the game is, where the output goes ---------------------------------
$gameRoot = ""
if ($Game) {
    if (-not (Test-Path (Join-Path $Game "sh2pc.exe"))) { Write-Error "no sh2pc.exe in $Game"; exit 1 }
    $gameRoot = (Resolve-Path $Game).Path
} else {
    $guess = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (Test-Path (Join-Path $guess "sh2pc.exe")) { $gameRoot = $guess }
}
if (-not $gameRoot -and -not $SkipHostTest) {
    Write-Error "sh2pc.exe not found. Pass -Game <folder containing sh2pc.exe> to run the host test, or -SkipHostTest to build without the game."
    exit 1
}
$outDir = if ($Out) { $Out } elseif ($gameRoot) { $gameRoot } else { Join-Path $PSScriptRoot "out" }
New-Item -ItemType Directory -Force $outDir | Out-Null
$outDir = (Resolve-Path $outDir).Path

New-Item -ItemType Directory -Force obj | Out-Null
$common = @("/nologo", "/O2", "/W3", "/EHsc", "/DNDEBUG", "/D_CRT_SECURE_NO_WARNINGS", "/Foobj\")

# 1. the logic tests
& $cl @common src\test_logic.cpp src\playablered_logic.cpp /Fe:obj\test_logic.exe /link user32.lib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& obj\test_logic.exe
if ($LASTEXITCODE -ne 0) { Write-Error "logic tests failed"; exit 1 }

# 2. the plugin (.asi), built to obj\ first so the harness can load it whether or not the game holds the shipped one
& $cl @common /LD src\SH2PlayableRed.cpp src\playablered_logic.cpp /Fe:obj\PlayableRed.asi /link /DEF:src\SH2PlayableRed.def user32.lib /IMPLIB:obj\PlayableRed.lib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# 3. the harness against a copy of the real exe
if ($SkipHostTest) {
    Write-Host "host test skipped (-SkipHostTest)"
} else {
    & $cl @common src\host_test.cpp src\playablered_logic.cpp /Fe:obj\host_test.exe /link user32.lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & obj\host_test.exe "$PSScriptRoot\obj\PlayableRed.asi" "$gameRoot\sh2pc.exe"
    if ($LASTEXITCODE -ne 0) { Write-Error "host test failed"; exit 1 }
}
if ($Test) { exit 0 }

# 4. ship
$dll = Join-Path $outDir "PlayableRed.asi"
try { Copy-Item obj\PlayableRed.asi $dll -Force -ErrorAction Stop }
catch { Write-Error "could not write $dll -- close the game first (a loaded plugin is locked), or use -Out"; exit 1 }
$exe = Join-Path $outDir "PlayableRedConfig.exe"
& $cl @common src\PlayableRedConfig.cpp src\playablered_logic.cpp /Fe:$exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib uxtheme.lib gdiplus.lib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Remove-Item -Force obj\*.exp -ErrorAction SilentlyContinue
Write-Host "built $dll and $exe"
