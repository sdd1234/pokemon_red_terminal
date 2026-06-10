@echo off
chcp 65001 >nul 2>&1
cd /d "%~dp0"
setlocal EnableDelayedExpansion

REM ============================================================
REM  Pokemon Red - Terminal Edition Build Script
REM  - Auto-detects g++ from PATH or common MinGW install locations
REM  - Builds and launches the game in one step
REM ============================================================

REM ====== Locate g++ ======
REM 1) Prefer modern MinGW-w64 (C++17 inline variables need g++ 7+)
REM    This avoids old MinGW.org GCC 6.3 in PATH
set "GXX="
for %%P in (
    "C:\mingw64\bin\g++.exe"
    "C:\msys64\mingw64\bin\g++.exe"
    "C:\msys64\ucrt64\bin\g++.exe"
    "C:\Program Files\mingw64\bin\g++.exe"
    "C:\Program Files (x86)\mingw64\bin\g++.exe"
    "%LOCALAPPDATA%\Programs\mingw64\bin\g++.exe"
) do (
    if exist %%~P (
        set "GXX=%%~P"
        REM Prepend g++ directory to PATH so runtime DLLs are found
        set "PATH=%%~dpP;!PATH!"
        goto :found
    )
)

REM 2) Fallback: PATH g++ (may be too old, will error during build)
where g++ >nul 2>&1
if %errorlevel% equ 0 (
    set "GXX=g++"
    goto :found
)

REM 3) Last resort: old MinGW.org (likely fails on C++17 inline vars)
if exist "C:\MinGW\bin\g++.exe" (
    set "GXX=C:\MinGW\bin\g++.exe"
    set "PATH=C:\MinGW\bin;!PATH!"
    goto :found
)

echo [ERROR] g++.exe not found.
echo.
echo Please install MinGW-w64:
echo   - https://www.msys2.org/    (MSYS2 - recommended)
echo   - https://winlibs.com/      (Portable)
echo.
echo After install, either:
echo   1) Add the bin directory to PATH environment variable
echo      (e.g. C:\mingw64\bin)
echo   2) Or extract to C:\mingw64
echo.
pause
exit /b 1

:found
echo ============================================
echo  PokemonRed - Build
echo ============================================
echo Compiler: %GXX%
"%GXX%" --version | findstr /i "g++"

REM Kill any running game instance to release the exe file lock
taskkill /F /IM PokemonRed.exe >nul 2>&1

if not exist build mkdir build

echo.
echo Compiling...
"%GXX%" -std=c++17 ^
    src\main.cpp ^
    src\engine\renderer.cpp src\engine\input.cpp src\engine\audio.cpp ^
    src\game\game.cpp src\game\battle.cpp src\game\overworld.cpp ^
    -o build\PokemonRed.exe ^
    -lwinmm -I src ^
    -mconsole -DUNICODE -D_UNICODE ^
    -static -static-libgcc -static-libstdc++

set BUILDRC=%errorlevel%

if %BUILDRC% neq 0 (
    echo.
    echo [FAILED] Build failed, exit code %BUILDRC%
    pause
    exit /b 1
)

if not exist build\PokemonRed.exe (
    echo [FAILED] PokemonRed.exe was not produced
    pause
    exit /b 1
)

REM Copy runtime DLLs as fallback (in case -static linking fails)
for %%D in (libwinpthread-1.dll libgcc_s_seh-1.dll libstdc++-6.dll) do (
    for /f "delims=" %%F in ('where %%D 2^>nul') do (
        if not exist "build\%%D" copy /Y "%%F" build\ >nul 2>&1
    )
)

REM Copy sound assets next to the exe (audio.cpp 가 exe 기준으로 sounds\ 를 찾음)
if exist sounds (
    if not exist build\sounds mkdir build\sounds
    copy /Y sounds\*.wav build\sounds\ >nul 2>&1
)

echo.
echo [OK] Build succeeded
echo ============================================
echo  Launching game...
echo ============================================
echo.

REM Launch in a NEW console window so behavior matches EXE double-click
REM (avoids inheriting build.bat's parent cmd console size/font)
start "PokemonRed" build\PokemonRed.exe
