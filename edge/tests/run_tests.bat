@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo   Building and Running Edge Mock Unit Tests
echo ===================================================

set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if "%VS_PATH%"=="" (
    echo [ERROR] Visual Studio C++ Compiler was not found.
    exit /b 1
)

echo Found Visual Studio at: %VS_PATH%
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"

echo Compiling test_edge_mock...
cl /nologo /W3 /D_CRT_SECURE_NO_WARNINGS ^
   /Iedge\app\include /Iedge\osal\include /Iedge\hal\include /Iedge\middleware\serializer /Iedge\middleware\telemetry ^
   edge\tests\unit\test_edge_mock.c ^
   edge\app\src\app_state_machine.c ^
   edge\middleware\serializer\telemetry_serializer.c ^
   edge\middleware\serializer\protobuf_serializer.c ^
   edge\middleware\telemetry\telemetry_buffer.c ^
   edge\osal\mock\osal_mock.c ^
   edge\hal\mock\hal_mock.c ^
   /Fe:edge\tests\test_edge_mock.exe



if %errorlevel% neq 0 (
    echo [ERROR] Compilation failed!
    exit /b %errorlevel%
)

echo.
echo Running test_edge_mock.exe...
edge\tests\test_edge_mock.exe
set TEST_RESULT=%errorlevel%

REM Cleanup build artifacts
del edge\tests\*.obj 2>nul

exit /b %TEST_RESULT%
