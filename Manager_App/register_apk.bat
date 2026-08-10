@echo off
setlocal

set "APP_PATH=%~dp0Elite_App_Marketplace-Server.v2.exe"
set "APP_PATH=%APP_PATH:\=\\%"

echo Registering .apk file association...
reg add "HKCU\Software\Classes\.apk" /ve /d "EliteAppMarketplace.APK" /f
reg add "HKCU\Software\Classes\EliteAppMarketplace.APK" /ve /d "Android Package Archive" /f
reg add "HKCU\Software\Classes\EliteAppMarketplace.APK\DefaultIcon" /ve /d "\"%~dp0Elite_App_Marketplace-Server.v2.exe\",0" /f
reg add "HKCU\Software\Classes\EliteAppMarketplace.APK\shell\open\command" /ve /d "\"%~dp0Elite_App_Marketplace-Server.v2.exe\" \"%%1\"" /f

echo Done!
pause
