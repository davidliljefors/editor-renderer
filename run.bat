@echo off

call build.bat
if errorlevel 1 (
    echo Build failed
    exit /b 1
)

start "" build/main.exe