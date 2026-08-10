@echo off
setlocal

rem Check for Administrator privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo Requesting Administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

echo Unregistering APK Shell Extension (Thumbnail Provider and Icon Handler)...

reg delete "HKCR\CLSID\{1B851216-724B-4D6F-96AF-C6ACED29BDB8}" /f
reg delete "HKCR\apkfile\ShellEx\IconHandler" /f
reg delete "HKCR\apkfile\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}" /f

echo Restarting explorer to apply changes...
taskkill /f /im explorer.exe
start explorer.exe

echo Unregistration complete.
