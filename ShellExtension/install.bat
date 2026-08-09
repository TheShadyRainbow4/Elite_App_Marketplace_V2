@echo off
setlocal

rem Check for Administrator privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo Requesting Administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

set "DLL_PATH=%~dp0ApkIconHandler.dll"
set "DLL_PATH=%DLL_PATH:\=\\%"

echo Registering APK Thumbnail Provider...

reg add "HKCR\CLSID\{1B851216-724B-4D6F-96AF-C6ACED29BDB8}" /ve /d "APK Thumbnail Provider" /f
reg add "HKCR\CLSID\{1B851216-724B-4D6F-96AF-C6ACED29BDB8}\InprocServer32" /ve /d "%DLL_PATH%" /f
reg add "HKCR\CLSID\{1B851216-724B-4D6F-96AF-C6ACED29BDB8}\InprocServer32" /v ThreadingModel /d "Apartment" /f

reg add "HKCR\.apk" /ve /d "apkfile" /f
reg add "HKCR\apkfile\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}" /ve /d "{1B851216-724B-4D6F-96AF-C6ACED29BDB8}" /f

echo Restarting explorer to apply changes...
taskkill /f /im explorer.exe
start explorer.exe

echo Registration complete.
pause
