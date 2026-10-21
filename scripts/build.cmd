@echo off
rem Configure, build and test ByteLeash from a plain command prompt.
rem Usage: scripts\build.cmd [debug^|release]   (default: debug)
rem In the Visual Studio IDE, use File ^> Open ^> Folder instead; it reads
rem CMakePresets.json directly.
setlocal

set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=debug"

set "VSINSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
set "PATH=%PATH%;%VSINSTALLER%"
set "VSDIR="
for /f "usebackq delims=" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio with the C++ workload was not found.
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET% || exit /b 1
ctest --preset %PRESET%
