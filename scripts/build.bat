@echo off
rem Usage: scripts\build.bat [dev^|release] [test]
rem Loads the MSVC environment, configures and builds with the CMake preset,
rem and runs the unit tests when "test" is given.
rem NOTE: keep this file ASCII-only; cmd.exe misparses UTF-8 text under a GBK code page.
setlocal
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=dev"

rem Machine defaults; override with environment variables of the same name.
if not defined VCPKG_ROOT set "VCPKG_ROOT=D:\vcpkg"
if not defined QT_ROOT set "QT_ROOT=D:\Qt\6.8.3\msvc2022_64"
if not defined FFMPEG_ROOT set "FFMPEG_ROOT=D:\deps\ffmpeg"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo [build] MSVC not found. Install Visual Studio 2022 Build Tools with the C++ workload.
    exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cd /d "%~dp0.."
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET% || exit /b 1
if /i "%~2"=="test" ctest --preset %PRESET% || exit /b 1
