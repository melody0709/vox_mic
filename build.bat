@echo off
setlocal

cd /d "%~dp0"

echo Building AudioSource Win (Raw WASAPI)...
echo.

REM Try to find Visual Studio
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_PATH=%%i"
    )
)

if defined VS_PATH (
    call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
) else (
    REM Fallback to common paths
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    ) else (
        echo ERROR: Visual Studio 2022 not found!
        echo Please install Visual Studio 2022 with C++ Desktop Development workload.
        exit /b 1
    )
)

echo Compiling...
cl /O2 /EHsc /std:c++17 ^
    /W4 /WX- ^
    src\main.cpp ^
    src\wasapi_output.cpp ^
    src\device_enum.cpp ^
    src\socket_client.cpp ^
    src\adb_control.cpp ^
    src\tray_icon.cpp ^
    /Fe:audiosource.exe ^
    /link ws2_32.lib ole32.lib mmdevapi.lib shell32.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful: audiosource.exe
    echo.
    echo Usage:
    echo   audiosource.exe                 Start audio bridge
    echo   audiosource.exe --list-devices  List audio devices
    echo   audiosource.exe --help          Show help
) else (
    echo.
    echo Build FAILED!
)

endlocal
