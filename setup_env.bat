@echo off
echo Setting up Visual Studio environment variables...

if not exist build mkdir build

:: Check if vcvarsall.bat exists in typical VS installation paths
set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022"

if exist "%VS_PATH%\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=%VS_PATH%\Community\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "%VS_PATH%\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=%VS_PATH%\Professional\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "%VS_PATH%\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=%VS_PATH%\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
) else (
    echo Error: vcvarsall.bat not found. Please check your Visual Studio installation.
    exit /b 1
)

:: Call vcvarsall.bat with x86 architecture (you can change to x64 if needed)
call "%VCVARSALL%" x64

:: Save all environment variables to env_cache.txt
echo Saving environment variables to build\env_cache.txt...
set > build\env_cache.txt

echo Environment setup complete.