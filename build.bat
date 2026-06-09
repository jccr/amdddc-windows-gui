@echo off
setlocal enabledelayedexpansion

:: Hardcoded fallback path verified on your system
set "FALLBACK_MSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
set "MSBUILD="

:: Try to locate vswhere.exe
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
        set "MSBUILD=%%i"
    )
)

:: Use fallback if vswhere query didn't find anything
if "!MSBUILD!"=="" (
    if exist "!FALLBACK_MSBUILD!" (
        set "MSBUILD=!FALLBACK_MSBUILD!"
    )
)

if "!MSBUILD!"=="" (
    echo Error: MSBuild.exe could not be found. Please ensure Build Tools is installed.
    pause
    exit /b 1
)

echo Using MSBuild: "!MSBUILD!"
echo Building solution in Release configuration...

"!MSBUILD!" "%~dp0amdddc-windows.sln" /p:Configuration=Release /p:Platform=x64

if %ERRORLEVEL% equ 0 (
    echo.
    echo Build Succeeded!
) else (
    echo.
    echo Build Failed!
)

pause
