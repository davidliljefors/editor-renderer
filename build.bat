@echo off

if not exist build mkdir build

:: Check if env_cache.txt exists
if not exist build\env_cache.txt (
    echo Environment cache not found, running setup...
    call setup_env.bat
    if errorlevel 1 (
        echo Failed to setup environment
        exit /b 1
    )
) else (
    echo Loading cached environment variables...
    :: Load the cached environment variables
    for /f "tokens=*" %%i in (build\env_cache.txt) do (
        set "%%i"
    )
)

set COMPILER_FLAGS=/std:c++17 /EHsc /Zi /DEBUG /Od /MTd /D_DEBUG
set OUTPUT_DIR=build

:: Compile mh64.cpp
echo Compiling mh64.cpp...
cl.exe mh64.cpp %COMPILER_FLAGS% /Fo:%OUTPUT_DIR%\mh64.obj /c

if errorlevel 1 (
    echo Compilation of mh64.cpp failed
    exit /b 1
)

:: Compile Scene.cpp
echo Compiling Scene.cpp...
cl.exe Scene.cpp %COMPILER_FLAGS% /Fo:%OUTPUT_DIR%\Scene.obj /c

if errorlevel 1 (
    echo Compilation of Scene.cpp failed
    exit /b 1
)

:: Compile EditorRenderer.cpp
echo Compiling EditorRenderer.cpp...
cl.exe EditorRenderer.cpp %COMPILER_FLAGS% /Fo:%OUTPUT_DIR%\EditorRenderer.obj /c

if errorlevel 1 (
    echo Compilation of mh64.cpp failed
    exit /b 1
)

:: Compile main.cpp and link with mh64
echo Compiling main.cpp and linking...
cl.exe main.cpp %OUTPUT_DIR%\EditorRenderer.obj %OUTPUT_DIR%\Scene.obj %OUTPUT_DIR%\mh64.obj %COMPILER_FLAGS% /Fe:%OUTPUT_DIR%\main.exe /Fo:%OUTPUT_DIR%\main.obj

if errorlevel 1 (
    echo Compilation failed
    exit /b 1
) else (
    echo Compilation successful
)