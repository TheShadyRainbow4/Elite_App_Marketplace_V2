@echo off
if "%~1"=="logging" goto :logging

echo Starting build process... Output is being written to build_log.txt
call "%~f0" logging > "%~dp0build_log.txt" 2>&1
exit

:logging
echo Terminating running instances...
taskkill /F /IM Elite_App_Marketplace-Server.v2.exe >nul 2>&1
taskkill /F /IM LocalAPKStore.exe >nul 2>&1

echo Cleaning old builds...
if exist LocalAPKStore.exe del LocalAPKStore.exe
if exist Elite_App_Marketplace-Server.v2.exe del Elite_App_Marketplace-Server.v2.exe

echo Building Manager App...
windres resource.rc -O coff -o resource.res
gcc -O2 -c miniz.c -o miniz.o
g++ -O2 -mwindows -std=c++17 -o Elite_App_Marketplace-Server.v2.exe main.cpp miniz.o resource.res -lcomctl32 -lws2_32 -lwinhttp -lgdiplus -lole32 -luuid -static -static-libgcc -static-libstdc++

if %errorlevel% equ 0 (
    echo Build successful: Elite_App_Marketplace-Server.v2.exe
    cd ..
    powershell -NoProfile -ExecutionPolicy Bypass -File publish_release.ps1
) else (
    echo Error: EXE Build failed! Aborting release.
)
