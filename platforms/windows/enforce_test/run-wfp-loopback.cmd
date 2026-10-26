@echo off
rem WFP enforcement tests on one PC (loopback), as run on 2026-10-26.
rem Run from an administrator terminal after building (scripts\build.cmd).
rem Usage: run-wfp-loopback.cmd [results folder]   (default: .\wfp-results)
rem Starts its own sink as bl-sink.exe (a renamed copy, so the "block this
rem app" filter doesn't match it), runs four tests, then stops the sink.
rem Windows Firewall may ask about bl-sink.exe; loopback works either way.
setlocal

set "EXE=%~dp0..\..\..\build\debug\platforms\windows\enforce_test\byteleash-enforce-test.exe"
if not exist "%EXE%" (
    echo Build first: scripts\build.cmd debug
    exit /b 1
)
set "OUT=%~1"
if "%OUT%"=="" set "OUT=%CD%\wfp-results"
if not exist "%OUT%" mkdir "%OUT%"
cd /d "%OUT%"

copy /y "%EXE%" bl-sink.exe >nul
start "" /b bl-sink.exe sink --port 5201 > sink.txt
timeout /t 2 /nobreak >nul

echo === 1/4 block app (10 s) ===
"%EXE%" wfp --target 127.0.0.1:5201 --mode app --out wfp-app.txt

echo === 2/4 block one connection (10 s) ===
"%EXE%" wfp --target 127.0.0.1:5201 --mode connection --out wfp-conn.txt

echo === 3/4 block app (60 s), does it recover? ===
"%EXE%" wfp --target 127.0.0.1:5201 --mode app --block 60 --after 20 --out wfp-app-60.txt

echo === 4/4 kill mid-block, check for leftovers ===
start "" /b "%EXE%" wfp --target 127.0.0.1:5201 --mode app --block 120 > wfp-kill.txt
timeout /t 10 /nobreak >nul
netsh wfp show filters file=filters-during.xml >nul
taskkill /f /im byteleash-enforce-test.exe > kill.txt 2>&1
timeout /t 3 /nobreak >nul
netsh wfp show filters file=filters-after.xml >nul
"%EXE%" cleanup > cleanup.txt 2>&1
find /c "ByteLeash test" filters-during.xml filters-after.xml > filter-counts.txt

taskkill /f /im bl-sink.exe >nul 2>&1
echo Done. Results in %OUT%
