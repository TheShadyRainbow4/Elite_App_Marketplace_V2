@echo off
setlocal

echo Compiling miniz...
gcc -O2 -c miniz.c -o miniz.o

echo Compiling C++ code and linking...
g++ -O2 -shared -o ApkIconHandler.dll ApkThumbnailProvider.cpp miniz.o ApkIconHandler.def -luuid -lole32 -loleaut32 -lshlwapi -lgdiplus -lgdi32 -static-libgcc -static-libstdc++ -Wl,--kill-at

if %ERRORLEVEL% NEQ 0 (
    echo Build failed.
    exit /b %ERRORLEVEL%
)

echo Build successful: ApkIconHandler.dll
exit /b 0
