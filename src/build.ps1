# build.ps1 -- builds PlayableRed.asi (into the game folder, next to sh2pc.exe, where the
# Enhanced Edition's own ASI loader finds it) and
# PlayableRedConfig.exe (into the game's root folder, next to sh2pc.exe), with the
# x86 MSVC toolchain found through vswhere, the way plugins\CodeBuilderMod\dobuild.ps1 does.
#
# Nothing ships before two test programs pass:
#   test_logic.exe   the decisions (descriptor substitution, tables, run rule, the .ini)
#   host_test.exe    the DLL itself, loaded the way the framework loads it, patching a
#                    private copy of sh2pc.exe -- every hook, every value, and the way back
#
#   powershell -File build.ps1            tests, then the DLL and the config tool
#   powershell -File build.ps1 -Test      tests only
#   powershell -File build.ps1 -Out X     write the .asi to X instead (a loaded one is locked while the game runs)
param([string]$Out = "", [switch]$Test)

Set-Location $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Write-Error "vswhere.exe not found; install Visual Studio with the C++ workload."; exit 1 }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { Write-Error "No Visual Studio with C++ tools found."; exit 1 }
$vcToolsRoot = Join-Path $vsPath "VC\Tools\MSVC"
$msvcVer = (Get-ChildItem $vcToolsRoot | Sort-Object Name -Descending | Select-Object -First 1).Name
$msvcDir = Join-Path $vcToolsRoot "$msvcVer\bin\Hostx86\x86"
$cl = Join-Path $msvcDir "cl.exe"
if (-not (Test-Path $cl)) { Write-Error "cl.exe not found at $cl"; exit 1 }
$wkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
$sdkVer = (Get-ChildItem "$wkRoot\Include" | Sort-Object Name -Descending | Select-Object -First 1).Name
$env:INCLUDE = @("$vcToolsRoot\$msvcVer\include", "$wkRoot\Include\$sdkVer\um", "$wkRoot\Include\$sdkVer\shared", "$wkRoot\Include\$sdkVer\ucrt") -join ";"
$env:LIB = @("$vcToolsRoot\$msvcVer\lib\x86", "$wkRoot\Lib\$sdkVer\um\x86", "$wkRoot\Lib\$sdkVer\ucrt\x86") -join ";"
$env:PATH = "$msvcDir;" + $env:PATH

New-Item -ItemType Directory -Force obj | Out-Null
$common = @("/nologo", "/O2", "/W3", "/EHsc", "/DNDEBUG", "/D_CRT_SECURE_NO_WARNINGS", "/Foobj\")
$gameRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

# 1. the logic tests
& $cl @common src\test_logic.cpp src\playablered_logic.cpp /Fe:obj\test_logic.exe /link user32.lib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& obj\test_logic.exe
if ($LASTEXITCODE -ne 0) { Write-Error "logic tests failed"; exit 1 }

# 2. the plugin (.asi), built to obj\ first so the harness can load it whether or not the game holds the shipped one
& $cl @common /LD src\SH2PlayableRed.cpp src\playablered_logic.cpp /Fe:obj\PlayableRed.asi /link /DEF:src\SH2PlayableRed.def user32.lib /IMPLIB:obj\PlayableRed.lib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# 3. the harness against a copy of the real exe
& $cl @common src\host_test.cpp src\playablered_logic.cpp /Fe:obj\host_test.exe /link user32.lib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& obj\host_test.exe "$PSScriptRoot\obj\PlayableRed.asi" "$gameRoot\sh2pc.exe"
if ($LASTEXITCODE -ne 0) { Write-Error "host test failed"; exit 1 }
if ($Test) { exit 0 }

# 4. ship
$dll = if ($Out) { $Out } else { Join-Path $gameRoot "PlayableRed.asi" }
try { Copy-Item obj\PlayableRed.asi $dll -Force -ErrorAction Stop }
catch { Write-Error "could not write $dll -- close the game first (a loaded plugin is locked), or use -Out"; exit 1 }
$exe = Join-Path $gameRoot "PlayableRedConfig.exe"
& $cl @common src\PlayableRedConfig.cpp src\playablered_logic.cpp /Fe:$exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib uxtheme.lib gdiplus.lib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Remove-Item -Force obj\*.exp -ErrorAction SilentlyContinue
Write-Host "built $dll and $exe"
