@echo off
setlocal

echo Unregistering existing DLL...
if exist ApkIconHandler.dll (
    regsvr32 /u /s ApkIconHandler.dll
)

echo Terminating Windows Explorer to release file lock...
taskkill /F /IM explorer.exe >nul 2>&1
taskkill /F /IM win32explorer.exe >nul 2>&1
taskkill /F /IM dllhost.exe >nul 2>&1
ping 127.0.0.1 -n 3 >nul

echo Renaming existing locked DLL to avoid permission denied...
if exist ApkIconHandler.dll.old del /F /Q ApkIconHandler.dll.old >nul 2>&1
if exist ApkIconHandler.dll move /Y ApkIconHandler.dll ApkIconHandler.dll.old >nul 2>&1

echo Compiling miniz...
gcc -O2 -c miniz.c -o miniz.o

echo Compiling C++ code and linking...
g++ -O2 -shared -o ApkIconHandler.dll ApkIconHandler.cpp miniz.o ApkIconHandler.def -luuid -lole32 -loleaut32 -lshlwapi -lgdiplus -lgdi32 -static-libgcc -static-libstdc++ -Wl,--kill-at

if %ERRORLEVEL% NEQ 0 (
    echo Build failed.
    echo Restarting Explorer...
    start explorer.exe
    exit /b %ERRORLEVEL%
)

echo Registering new DLL...
regsvr32 /s ApkIconHandler.dll

echo Restarting Explorer...
start explorer.exe

echo Build successful: ApkIconHandler.dll
exit /b 0
