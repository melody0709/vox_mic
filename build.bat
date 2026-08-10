@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

set "MODE=build"
set "SIGNING=0"
set "PACKAGE_PORTABLE=0"
set "PACKAGE_MSI=0"
set "ENABLE_DPDFNET=0"
set "TEST_DPDFNET=0"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--rebuild"          ( set "MODE=rebuild" & shift & goto parse_args)
if /i "%~1"=="--clean"            ( set "MODE=clean" & shift & goto parse_args)
if /i "%~1"=="--package"          ( set "MODE=package" & shift & goto parse_args)
if /i "%~1"=="--package-portable" ( set "MODE=package" & set "PACKAGE_PORTABLE=1" & shift & goto parse_args)
if /i "%~1"=="--package-msi"      ( set "MODE=package" & set "PACKAGE_MSI=1" & shift & goto parse_args)
if /i "%~1"=="--dpdfnet"          ( set "ENABLE_DPDFNET=1" & shift & goto parse_args)
if /i "%~1"=="--test-dpdfnet"     ( set "TEST_DPDFNET=1" & shift & goto parse_args)
if /i "%~1"=="--require-signing"  ( set "SIGNING=1" & shift & goto parse_args)
echo ERROR: Unknown argument "%~1"
exit /b 2
:args_done

if "%TEST_DPDFNET%"=="1" if "%ENABLE_DPDFNET%"=="0" (
    echo ERROR: --test-dpdfnet requires --dpdfnet.
    exit /b 2
)

REM Locate Visual Studio 2022 (provides MSVC + CMake + Ninja)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_PATH="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_PATH=%%i"
    )
)
if not defined VS_PATH (
    for %%E in (Community Professional Enterprise BuildTools) do (
        if not defined VS_PATH if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\%%E"
        )
    )
)
if not defined VS_PATH (
    echo ERROR: Visual Studio 2022 with C++ Desktop Development not found.
    exit /b 1
)
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

set "BUILD_DIR=build\cmake\x64-release"
set "RUN_DIR=build\run\x64-release"
set "INSTALL_MANIFEST=%BUILD_DIR%\install_manifest.txt"

REM Validate existing build/ layout (whitelist enforcement) before doing anything.
if exist "build" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\validate_build_layout.ps1" -BuildRoot "%~dp0build" -RuntimeDirectory "%~dp0%RUN_DIR%" -InstallManifest "%~dp0%INSTALL_MANIFEST%" -SkipRuntime
    if errorlevel 1 exit /b 1
)

if "%MODE%"=="clean" (
    echo Cleaning build artifacts ^(keeping packages^)...
    if exist "build\cmake" (
        rmdir /s /q "build\cmake"
        if exist "build\cmake" ( echo ERROR: Could not remove build\cmake. Close any process using generated files. & exit /b 1 )
    )
    if exist "build\run" (
        rmdir /s /q "build\run"
        if exist "build\run" ( echo ERROR: Could not remove build\run. Close voxmic.exe or another process using the runtime payload. & exit /b 1 )
    )
    if exist "build\artifacts" (
        rmdir /s /q "build\artifacts"
        if exist "build\artifacts" ( echo ERROR: Could not remove build\artifacts. Close any process using generated reports. & exit /b 1 )
    )
    if exist "build\logs" (
        rmdir /s /q "build\logs"
        if exist "build\logs" ( echo ERROR: Could not remove build\logs. Close any process using generated logs. & exit /b 1 )
    )
    if exist "build\README.txt" (
        del /f /q "build\README.txt"
        if exist "build\README.txt" ( echo ERROR: Could not remove build\README.txt. & exit /b 1 )
    )
    echo Clean done.
    exit /b 0
)

if "%MODE%"=="rebuild" (
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

set "DPDFNET_DEPS_DIR=%~dp0%BUILD_DIR%\_deps\dpdfnet"
if "%ENABLE_DPDFNET%"=="1" (
    echo Preparing verified DPDFNet dependencies...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\prepare_dpdfnet_deps.ps1" -OutputDirectory "%~dp0%BUILD_DIR%\_deps" -Force
    if errorlevel 1 ( echo DPDFNet dependency preparation FAILED. & exit /b 1 )
)

echo Configuring ^(CMake x64-release^)...
cmake --preset x64-release -DVOXMIC_ENABLE_DPDFNET=%ENABLE_DPDFNET% -DVOXMIC_REQUIRE_DPDFNET_PAYLOAD=%ENABLE_DPDFNET% -DVOXMIC_DPDFNET_DEPS_DIR="%DPDFNET_DEPS_DIR%"
if errorlevel 1 ( echo CMake configure FAILED. & exit /b 1 )

echo Building ^(Ninja^)...
cmake --build --preset x64-release
if errorlevel 1 ( echo Build FAILED. & exit /b 1 )

echo Installing runtime payload to %RUN_DIR%...
cmake --install "%BUILD_DIR%"
if errorlevel 1 ( echo Install FAILED. & exit /b 1 )

REM Ensure auxiliary build/ subdirs exist for the whitelist validator.
if not exist "build\artifacts" mkdir "build\artifacts"
if not exist "build\logs"      mkdir "build\logs"
if not exist "build\packages"  mkdir "build\packages"

REM Write build/README.txt (whitelist expects this file).
if not exist "build\README.txt" (
    echo VoxMic generated output> "build\README.txt"
    echo.>> "build\README.txt"
    echo cmake\x64-release\  - disposable CMake/Ninja cache, objects, and package staging>> "build\README.txt"
    echo run\x64-release\    - sole supported runnable development payload>> "build\README.txt"
    echo packages\           - verified MSI and Portable release assets; preserved by --clean>> "build\README.txt"
    echo artifacts\          - generated package verification, test, and diagnostic reports>> "build\README.txt"
    echo logs\               - explicit build and test logs>> "build\README.txt"
    echo third_party\dpdfnet\ - source-controlled DPDFNet model/runtime payload; build output is disposable>> "build\README.txt"
    echo.>> "build\README.txt"
    echo The top-level whitelist is cmake, run, packages, artifacts, logs, and README.txt.>> "build\README.txt"
    echo Unexpected build-root files fail validation. Do not store source files, backups, or user data here.>> "build\README.txt"
)

REM Validate the freshly installed runtime payload against runtime-manifest.json.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\validate_build_layout.ps1" -BuildRoot "%~dp0build" -RuntimeDirectory "%~dp0%RUN_DIR%" -InstallManifest "%~dp0%INSTALL_MANIFEST%"
if errorlevel 1 exit /b 1

if "%TEST_DPDFNET%"=="1" (
    echo.
    echo Running DPDFNet smoke tests...
    "%BUILD_DIR%\dpdfnet_smoke.exe" "%RUN_DIR%" "%RUN_DIR%\models\dpdfnet2_48khz_hr.onnx"
    if errorlevel 1 ( echo DPDFNet streaming smoke FAILED. & exit /b 1 )
    "%BUILD_DIR%\dpdfnet_fallback_smoke.exe" "%RUN_DIR%" "%RUN_DIR%\models\missing-for-test.onnx"
    if errorlevel 1 ( echo DPDFNet fallback smoke FAILED. & exit /b 1 )
    "%BUILD_DIR%\dpdfnet_pipeline_switch_smoke.exe" "%RUN_DIR%" "%RUN_DIR%\models\dpdfnet2_48khz_hr.onnx"
    if errorlevel 1 ( echo DPDFNet pipeline switch smoke FAILED. & exit /b 1 )
    echo DPDFNet smoke tests passed.
)

echo.
echo Build successful: %RUN_DIR%\voxmic.exe
if "%ENABLE_DPDFNET%"=="1" echo DPDFNet payload: enabled
if "%ENABLE_DPDFNET%"=="0" echo DPDFNet payload: disabled ^(RNNoise-only^)
echo.

if not "%MODE%"=="package" goto show_usage

REM Read product version from the freshly generated runtime-manifest.json.
set "VOXMIC_PRODUCT_VERSION="
for /f "usebackq tokens=*" %%v in (`powershell -NoProfile -Command "(Get-Content -Raw '%~dp0%RUN_DIR%\runtime-manifest.json' | ConvertFrom-Json).version"`) do set "VOXMIC_PRODUCT_VERSION=%%v"
if not defined VOXMIC_PRODUCT_VERSION (
    echo ERROR: could not read product version from runtime-manifest.json.
    exit /b 1
)

set "PKG_MODE=All"
if "%PACKAGE_PORTABLE%"=="1" if "%PACKAGE_MSI%"=="0" set "PKG_MODE=Portable"
if "%PACKAGE_MSI%"=="1" if "%PACKAGE_PORTABLE%"=="0" set "PKG_MODE=Msi"

set "SIGN_ARG="
if "%SIGNING%"=="1" set "SIGN_ARG=-RequireSigning"

echo Packaging ^(Mode=%PKG_MODE% Version=%VOXMIC_PRODUCT_VERSION% Sign=%SIGNING%^)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package_voxmic.ps1" -Mode %PKG_MODE% -BuildRoot "%~dp0build" -RuntimeDirectory "%~dp0%RUN_DIR%" -InstallManifest "%~dp0%INSTALL_MANIFEST%" -ProductVersion "%VOXMIC_PRODUCT_VERSION%" %SIGN_ARG%
if errorlevel 1 ( echo Packaging FAILED. & exit /b 1 )
echo Packaging done. See build\packages\
exit /b 0

:show_usage
echo Usage:
echo   build.bat                        Incremental build + install
echo   build.bat --rebuild              Clean rebuild
echo   build.bat --clean                Clean cmake/run/artifacts/logs ^(keeps packages^)
echo   build.bat --package              Build + Portable + MSI
echo   build.bat --package-portable     Build + Portable only
echo   build.bat --package-msi          Build + MSI only
echo   build.bat --dpdfnet              Enable DPDFNet runtime/model payload
echo   build.bat --dpdfnet --test-dpdfnet  Build and run DPDFNet smoke tests
echo   build.bat --require-signing ...  Sign release ^(needs cert env vars^)
echo.
echo Run: %RUN_DIR%\voxmic.exe
exit /b 0
