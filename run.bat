@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "SANDBOX=build\bin\Debug\sandbox.exe"
set "ENGINE_GPU_DEBUG="

echo.
echo  Sol Engine
echo  ----------
echo  GPU debug: type y, 1, or gpu  (blank = off)
echo  Extra args: anything passed to sandbox.exe  (example: --gates)
echo.

set /p GPU_DEBUG=GPU debug? 
set /p EXTRA_ARGS=Extra args: 

if /i "%GPU_DEBUG%"=="y"   set "ENGINE_GPU_DEBUG=1"
if /i "%GPU_DEBUG%"=="yes" set "ENGINE_GPU_DEBUG=1"
if /i "%GPU_DEBUG%"=="1"   set "ENGINE_GPU_DEBUG=1"
if /i "%GPU_DEBUG%"=="on"  set "ENGINE_GPU_DEBUG=1"
if /i "%GPU_DEBUG%"=="gpu" set "ENGINE_GPU_DEBUG=1"

if not exist "%SANDBOX%" (
    echo.
    echo Missing %SANDBOX%
    echo Build first: cmake --build build --config Debug --target sandbox
    echo.
    pause
    exit /b 1
)

echo.
if defined ENGINE_GPU_DEBUG (
    echo Starting with ENGINE_GPU_DEBUG=1 %EXTRA_ARGS%
) else (
    echo Starting %EXTRA_ARGS%
)
echo.

"%SANDBOX%" %EXTRA_ARGS%
set "EXIT_CODE=%ERRORLEVEL%"

echo.
echo Exit code %EXIT_CODE%
pause
exit /b %EXIT_CODE%
