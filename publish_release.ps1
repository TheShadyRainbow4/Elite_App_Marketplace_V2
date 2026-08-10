param(
    [string]$Version = ""
)
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Version)) {
    $dbObj = Get-Content "Manager_App/db.json" -Raw | ConvertFrom-Json
    foreach ($app in $dbObj.apps) {
        if ($app.package_name -eq "com.elitesoftware.appmarketplace.v2") {
            $sortedVers = $app.versions | Sort-Object { [version]($_.version -replace '^v?(\d+\.\d+)$', "`$1.0" -replace '^v?(\d+\.\d+\.\d+).*', "`$1") } -Descending
            $latestVer = $sortedVers[0].version
            $verParts = $latestVer.Split('.')
            $patch = [int]$verParts[2] + 1
            $Version = "v" + $verParts[0] + "." + $verParts[1] + "." + $patch
            break
        }
    }
    Write-Host "Auto-incremented version to $Version"
}
$rawVer = $Version.Replace("v", "")
$verParts = $rawVer.Split('.')
$verCode = 1
if ($verParts.Length -eq 3) {
    $verCode = [int]$verParts[0] * 10000 + [int]$verParts[1] * 100 + [int]$verParts[2]
}

Write-Host "Updating Android build.gradle version..."
$gradlePath = "Client_App/app/build.gradle"
$gradle = Get-Content $gradlePath
$gradle = $gradle -replace 'versionCode \d+', "versionCode $verCode"
$gradle = $gradle -replace 'versionName ".*"', "versionName ""$rawVer"""
$gradle | Set-Content $gradlePath

$gradlePath2 = "EliteWindowingComponents/app/build.gradle"
if (Test-Path $gradlePath2) {
    $gradle2 = Get-Content $gradlePath2
    $gradle2 = $gradle2 -replace 'versionCode \d+', "versionCode $verCode"
    $gradle2 = $gradle2 -replace 'versionName ".*"', "versionName ""$rawVer"""
    $gradle2 | Set-Content $gradlePath2
}

Write-Host "Building Android APK..."
cd Client_App
./build_apk.ps1
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: APK Build Failed! Aborting release." -ForegroundColor Red
    exit 1
}
cd ..

Write-Host "Building Elite Windowing Components APK..."
cd EliteWindowingComponents
./build_apk.ps1
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: Windowing APK Build Failed! Aborting release." -ForegroundColor Red
    exit 1
}
cd ..

Write-Host "Signing Android APKs..."
$toolsDir = "C:\AndroidBuildTools"
$apksigner = (Get-ChildItem -Path "$toolsDir\android-sdk\build-tools" -Filter "apksigner.bat" -Recurse | Select-Object -First 1).FullName

& $apksigner sign --ks "Z:\Local_APK_Store\Elite-EasySigner\EliteSoftware_Special.pfx" --ks-pass pass:Minecraft145!! --out "Client_App\app\build\outputs\apk\debug\app-release-signed.apk" "Client_App\app\build\outputs\apk\debug\app-debug.apk"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: APK Signing Failed! Aborting release." -ForegroundColor Red
    exit 1
}

& $apksigner sign --ks "Z:\Local_APK_Store\Elite-EasySigner\EliteSoftware_Special.pfx" --ks-pass pass:Minecraft145!! --out "EliteWindowingComponents\app\build\outputs\apk\debug\app-release-signed.apk" "EliteWindowingComponents\app\build\outputs\apk\debug\app-debug.apk"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: Windowing APK Signing Failed! Aborting release." -ForegroundColor Red
    exit 1
}

Write-Host "Injecting newest APK into Server's DB..."
$apkFileName = "Elite_App_Marketplace-Client_$Version.apk"
Copy-Item "Client_App\app\build\outputs\apk\debug\app-release-signed.apk" "Manager_App\apks\$apkFileName" -Force

$windowingApkName = "EliteWindowingComponents_$Version.apk"
Copy-Item "EliteWindowingComponents\app\build\outputs\apk\debug\app-release-signed.apk" "Manager_App\apks\$windowingApkName" -Force

Write-Host "Building Windows Server EXE..."
cd Manager_App
Stop-Process -Name "Elite_App_Marketplace-Server.v2" -ErrorAction SilentlyContinue
Stop-Process -Name "LocalAPKStore" -ErrorAction SilentlyContinue
cmd /c "windres resource.rc -O coff -o resource.res && gcc -O2 -c miniz.c -o miniz.o && g++ -O2 -mwindows -std=c++17 -o Elite_App_Marketplace-Server.v2.exe main.cpp miniz.o resource.res -lcomctl32 -lws2_32 -lwinhttp -lgdiplus -lole32 -static -static-libgcc -static-libstdc++"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: Windows Server EXE Build Failed! Aborting release." -ForegroundColor Red
    exit 1
}
cd ..

Write-Host "Building Shell Extension DLL..."
cd ShellExtension
cmd /c "build.bat"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: Shell Extension Build Failed! Aborting release." -ForegroundColor Red
    exit 1
}
cd ..

Write-Host "Updating db.json..."
$dbPath = "Manager_App/db.json"
$dbStr = Get-Content $dbPath -Raw
$dbObj = $dbStr | ConvertFrom-Json
foreach ($app in $dbObj.apps) {
    if ($app.package_name -eq "com.elitesoftware.appmarketplace.v2") {
        $app.name = "Elite Marketplace v2"
        $found = $false
        foreach ($v in $app.versions) {
            if ($v.version -eq $rawVer) {
                $found = $true
            }
        }
        if (-not $found) {
            $newVer = @{
                "version" = $rawVer
                "file" = $apkFileName
            }
            $app.versions = @($newVer) + $app.versions
        }
    }
    if ($app.package_name -eq "com.elitesoftware.popupwindowmanager" -or $app.package_name -eq "com.elitesoftware.geminiwidget") {
        $app.package_name = "com.elitesoftware.popupwindowmanager"
        $app.name = "ElitePopupWindow_manager"
        $foundWindowing = $false
        foreach ($v in $app.versions) {
            if ($v.version -eq $rawVer) {
                $foundWindowing = $true
            }
        }
        if (-not $foundWindowing) {
            $newVerWindowing = @{
                "version" = $rawVer
                "file" = $windowingApkName
            }
            $app.versions = @($newVerWindowing) + $app.versions
        }
    }
}
$dbObj | ConvertTo-Json -Depth 10 | Set-Content $dbPath

Write-Host "Committing and Pushing to Git..."
git rm -r --cached --ignore-unmatch "*.apk" "*.exe" "*.dll" "*.res" "*.o" "*.zip" "*.bin" -q 2>$null
git add .
git commit -m "Auto-build and release $Version"
git push origin master

Write-Host "Creating GitHub Release..."
$gh = "C:\Reunion7_Windows\Program Files\GitHub CLI\gh.exe"
$properApkName = "Elite_App_Marketplace-Client_$Version.apk"
$properWindowApkName = "EliteWindowingComponents_$Version.apk"
Copy-Item "Client_App\app\build\outputs\apk\debug\app-release-signed.apk" $properApkName -Force
Copy-Item "EliteWindowingComponents\app\build\outputs\apk\debug\app-release-signed.apk" $properWindowApkName -Force

& $gh release create "$Version" "Manager_App\Elite_App_Marketplace-Server.v2.exe" "ShellExtension\ApkIconHandler.dll" "IcoToPngConverter\IcoToPngConverter.exe" $properApkName $properWindowApkName --title "Elite App Marketplace $Version" --notes "Automated release." --target master

Remove-Item $properApkName
Remove-Item $properWindowApkName

Write-Host "Restarting Server..."
cd Manager_App
Start-Process -FilePath "Elite_App_Marketplace-Server.v2.exe"
cd ..

Write-Host "Publish complete for $Version"
