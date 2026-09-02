@echo off
rem ============================================================================
rem  Sol Engine - one entry point for running, gating, building and poking at
rem  the engine.
rem
rem  Two ways in, one dispatch table:
rem
rem    solengine                     the menu
rem    solengine <command> [args]    straight to it, for muscle memory and for
rem                                  scripts - the exit code is the command's
rem
rem  `solengine help` lists every command. Replaces run.bat, which only knew how
rem  to start the sandbox.
rem ============================================================================
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "DEBUG_EXE=build\bin\Debug\sandbox.exe"
set "RELEASE_EXE=build\bin\Release\game.exe"
set "DEBUG_LOG=build\bin\Debug\logs\log.txt"
set "RELEASE_LOG=build\bin\Release\logs\log.txt"
set "SHADER_CACHE=build\bin\Debug\.cache\shaders"
set "RUN_LOG=%TEMP%\solengine-run.log"
set "INTERACTIVE="

rem PowerShell 7 where it exists. 5.1's default execution policy is Restricted
rem and refuses an unsigned local script, so the fallback needs Bypass - the same
rem asymmetry README documents for tools/check-invariants.ps1.
where /q pwsh
if errorlevel 1 (
    set "PS=powershell -NoProfile -ExecutionPolicy Bypass"
) else (
    set "PS=pwsh -NoProfile"
)

call :ensure_vulkan_sdk

if "%~1"=="" (
    set "INTERACTIVE=1"
    goto menu
)

rem tokens=1,* splits the command from everything after it. Plain `shift` does
rem not update %*, which is the usual way this goes wrong.
for /f "tokens=1,*" %%a in ("%*") do (
    set "CMD=%%a"
    set "ARGS=%%b"
)
goto dispatch

rem ---------------------------------------------------------------------------
:menu
cls
echo.
echo   Sol Engine
echo   ==========
echo.
echo   RUN                              GATE
echo     1  sandbox (Debug)               10  gates (Debug)
echo     2  sandbox + GPU debug           11  gates + GPU debug and validation
echo     3  sandbox, custom args          12  gates (Release game)
echo     4  game (Release)                13  gates, CPU-only (no device)
echo                                      14  invariants
echo   BUILD                              15  invariants under PowerShell 5.1
echo     5  build Debug                   16  check - GPU gates + invariants
echo     6  build Release game
echo     7  configure                    LOOK
echo     8  rebuild (wipe build/)         17  environment report
echo     9  prerequisite check           18  newest log
echo                                      19  clear the shader cache
echo.
echo     h  every command, for the CLI    q  quit
echo.
set "CHOICE="
set /p CHOICE=^>
set "ARGS="
if "%CHOICE%"=="1"  set "CMD=run"
if "%CHOICE%"=="2"  set "CMD=run-gpu"
if "%CHOICE%"=="3"  goto ask_args
if "%CHOICE%"=="4"  set "CMD=game"
if "%CHOICE%"=="5"  set "CMD=build"
if "%CHOICE%"=="6"  set "CMD=build-release"
if "%CHOICE%"=="7"  set "CMD=configure"
if "%CHOICE%"=="8"  set "CMD=rebuild"
if "%CHOICE%"=="9"  set "CMD=prereqs"
if "%CHOICE%"=="10" set "CMD=gates"
if "%CHOICE%"=="11" set "CMD=gates-gpu"
if "%CHOICE%"=="12" set "CMD=gates-release"
if "%CHOICE%"=="13" set "CMD=gates-cpu"
if "%CHOICE%"=="14" set "CMD=invariants"
if "%CHOICE%"=="15" set "CMD=invariants-51"
if "%CHOICE%"=="16" set "CMD=check"
if "%CHOICE%"=="17" set "CMD=env"
if "%CHOICE%"=="18" set "CMD=logs"
if "%CHOICE%"=="19" set "CMD=clean-shaders"
if /i "%CHOICE%"=="h" set "CMD=help"
if /i "%CHOICE%"=="q" exit /b 0
if not defined CMD (
    echo   "%CHOICE%" is not on the menu.
    timeout /t 2 >nul
    goto menu
)
goto dispatch

:ask_args
echo.
echo   Anything the sandbox accepts. Examples:
echo     --gates
echo     --set r.aa=taa --set r.quality=high
echo     --set r.shadow_size=2048 --set r.exposure=-1
echo.
set "ARGS="
set /p ARGS=args^>
set "CMD=run"
goto dispatch

rem ---------------------------------------------------------------------------
:dispatch
echo.
if /i "%CMD%"=="run"            goto do_run
if /i "%CMD%"=="run-gpu"        goto do_run_gpu
if /i "%CMD%"=="game"           goto do_game
if /i "%CMD%"=="gates"          goto do_gates
if /i "%CMD%"=="gates-gpu"      goto do_gates_gpu
if /i "%CMD%"=="gates-release"  goto do_gates_release
if /i "%CMD%"=="gates-cpu"      goto do_gates_cpu
if /i "%CMD%"=="invariants"     goto do_invariants
if /i "%CMD%"=="invariants-51"  goto do_invariants_51
if /i "%CMD%"=="check"          goto do_check
if /i "%CMD%"=="prereqs"        goto do_prereqs
if /i "%CMD%"=="configure"      goto do_configure
if /i "%CMD%"=="build"          goto do_build
if /i "%CMD%"=="build-release"  goto do_build_release
if /i "%CMD%"=="rebuild"        goto do_rebuild
if /i "%CMD%"=="env"            goto do_env
if /i "%CMD%"=="logs"           goto do_logs
if /i "%CMD%"=="clean-shaders"  goto do_clean_shaders
if /i "%CMD%"=="help"           goto do_help
if /i "%CMD%"=="-h"             goto do_help
if /i "%CMD%"=="--help"         goto do_help
echo Unknown command "%CMD%". Try: solengine help
exit /b 2

rem ---------------------------------------------------------------------------
:do_run
call :require_exe "%DEBUG_EXE%" Debug || goto finish
call :warn_if_stale
set "ENGINE_GPU_DEBUG="
echo Running sandbox (Debug) %ARGS%
"%DEBUG_EXE%" %ARGS%
set "CODE=%ERRORLEVEL%"
goto finish

:do_run_gpu
call :require_exe "%DEBUG_EXE%" Debug || goto finish
call :warn_if_stale
rem One switch, both backends: the D3D12 debug layer and, when the Vulkan SDK
rem installed the layer, VK_LAYER_KHRONOS_validation. Any message from either is
rem a build-breaking bug, not a warning to skip.
set "ENGINE_GPU_DEBUG=1"
echo Running sandbox (Debug) with ENGINE_GPU_DEBUG=1 %ARGS%
"%DEBUG_EXE%" %ARGS%
set "CODE=%ERRORLEVEL%"
goto finish

:do_game
call :require_exe "%RELEASE_EXE%" "Release game" || goto finish
set "ENGINE_GPU_DEBUG="
echo Running game (Release) %ARGS%
"%RELEASE_EXE%" %ARGS%
set "CODE=%ERRORLEVEL%"
goto finish

:do_gates
call :require_exe "%DEBUG_EXE%" Debug || goto finish
call :warn_if_stale
set "ENGINE_GPU_DEBUG="
call :run_gates "%DEBUG_EXE%" --gates
goto finish

:do_gates_gpu
call :require_exe "%DEBUG_EXE%" Debug || goto finish
call :warn_if_stale
set "ENGINE_GPU_DEBUG=1"
call :run_gates "%DEBUG_EXE%" --gates
goto finish

:do_gates_release
call :require_exe "%RELEASE_EXE%" "Release game" || goto finish
set "ENGINE_GPU_DEBUG="
call :run_gates "%RELEASE_EXE%" --gates
goto finish

:do_gates_cpu
call :require_exe "%DEBUG_EXE%" Debug || goto finish
rem The subset classified Cpu in kGates - no device needed. This is what the
rem Linux and sanitizer CI jobs run, so it is the one gate mode that says
rem something about code rather than about this machine's GPU.
set "ENGINE_GPU_DEBUG="
call :run_gates "%DEBUG_EXE%" --gates-cpu
goto finish

:do_invariants
%PS% -File tools\check-invariants.ps1 %ARGS%
set "CODE=%ERRORLEVEL%"
goto finish

:do_invariants_51
rem CI runs the script under both shells, so this is the local half of that.
powershell -NoProfile -ExecutionPolicy Bypass -File tools\check-invariants.ps1 %ARGS%
set "CODE=%ERRORLEVEL%"
goto finish

:do_check
rem The pair CLAUDE.md asks for before shipping: the gates with both debug
rem layers armed, and the invariants. Invariants second and reported separately,
rem because chaining them once pushed a red invariant behind a green gate run.
call :require_exe "%DEBUG_EXE%" Debug || goto finish
call :warn_if_stale
set "ENGINE_GPU_DEBUG=1"
call :run_gates "%DEBUG_EXE%" --gates
set "GATE_CODE=%CODE%"
echo.
%PS% -File tools\check-invariants.ps1
set "INV_CODE=%ERRORLEVEL%"
echo.
if "%GATE_CODE%"=="0" (echo   gates      pass) else (echo   gates      FAIL ^(exit %GATE_CODE%^))
if "%INV_CODE%"=="0"  (echo   invariants pass) else (echo   invariants FAIL ^(exit %INV_CODE%^))
set "CODE=0"
if not "%GATE_CODE%"=="0" set "CODE=1"
if not "%INV_CODE%"=="0" set "CODE=1"
goto finish

:do_prereqs
%PS% -File tools\check-prereqs.ps1 %ARGS%
set "CODE=%ERRORLEVEL%"
goto finish

:do_configure
cmake --preset vs2026 %ARGS%
set "CODE=%ERRORLEVEL%"
goto finish

:do_build
cmake --build --preset debug %ARGS%
set "CODE=%ERRORLEVEL%"
goto finish

:do_build_release
cmake --build --preset release-game %ARGS%
set "CODE=%ERRORLEVEL%"
goto finish

:do_rebuild
echo This deletes build\ and reconfigures from scratch.
set "CONFIRM="
set /p CONFIRM=Type yes to continue:
if /i not "%CONFIRM%"=="yes" (
    echo Cancelled.
    set "CODE=1"
    goto finish
)
if exist build rmdir /s /q build
cmake --preset vs2026
if errorlevel 1 (
    set "CODE=%ERRORLEVEL%"
    goto finish
)
cmake --build --preset debug
set "CODE=%ERRORLEVEL%"
goto finish

:do_clean_shaders
rem The shader disk cache is keyed on source, entry point, profile and target,
rem so it is normally invisible. It is worth a button because a warm cache can
rem serve a shader whose *compiler* is the thing under test - a warm SPIR-V
rem entry hid the whole DXC-loading path once, and passed while doing it.
set "CODE=0"
if not exist "%SHADER_CACHE%" (
    echo No shader cache at %SHADER_CACHE%
    goto finish
)
del /q "%SHADER_CACHE%\*.cso" 2>nul
echo Cleared %SHADER_CACHE%
goto finish

:do_logs
if not exist "%DEBUG_LOG%" (
    echo No log at %DEBUG_LOG% - run the sandbox once.
    set "CODE=1"
    goto finish
)
echo %DEBUG_LOG%
echo ----------------------------------------------------------------------
%PS% -Command "Get-Content -LiteralPath '%DEBUG_LOG%' -Tail 60"
set "CODE=0"
goto finish

rem ---------------------------------------------------------------------------
:do_env
echo   Sol Engine - environment
echo   ------------------------
if defined VULKAN_SDK (
    echo   VULKAN_SDK        %VULKAN_SDK%
) else (
    echo   VULKAN_SDK        not set - SPIR-V compilation and the Vulkan
    echo                     validation layer are both unavailable, so the
    echo                     SPIR-V gate skips by name rather than failing.
)
if defined ENGINE_DXC_SPIRV echo   ENGINE_DXC_SPIRV  %ENGINE_DXC_SPIRV%
echo.
call :report_cmake
call :report_vulkan
echo.
if exist "%DEBUG_EXE%" (
    %PS% -Command "$e=Get-Item -LiteralPath '%DEBUG_EXE%'; Write-Host ('  sandbox.exe       ' + $e.LastWriteTime)"
) else (
    echo   sandbox.exe       not built
)
if exist "%RELEASE_EXE%" (
    %PS% -Command "$e=Get-Item -LiteralPath '%RELEASE_EXE%'; Write-Host ('  game.exe          ' + $e.LastWriteTime)"
) else (
    echo   game.exe          not built
)
if exist "%SHADER_CACHE%" (
    %PS% -Command "$n=@(Get-ChildItem -LiteralPath '%SHADER_CACHE%' -Filter *.cso).Count; Write-Host ('  shader cache      ' + $n + ' entries')"
)
echo.
call :warn_if_stale
set "CODE=0"
goto finish

:report_cmake
where /q cmake
if errorlevel 1 (
    echo   cmake             not on PATH
    goto :eof
)
for /f "tokens=3" %%v in ('cmake --version 2^>nul ^| findstr /i /c:"cmake version"') do (
    echo   cmake             %%v
    goto :eof
)
goto :eof

:report_vulkan
where /q vulkaninfo
if errorlevel 1 (
    echo   vulkan loader     not on PATH - vulkan-1.dll ships with the GPU
    echo                     driver, so this usually means no Vulkan driver
    goto :eof
)
rem The instance version and the first device name. vulkaninfo --summary opens
rem with a banner, so the interesting lines have to be picked out by name.
for /f "tokens=4" %%a in ('vulkaninfo --summary 2^>nul ^| findstr /i /c:"Vulkan Instance Version"') do (
    echo   vulkan instance   %%a
)
for /f "tokens=2,*" %%a in ('vulkaninfo --summary 2^>nul ^| findstr /i /c:"deviceName"') do (
    echo   vulkan device     %%b
    goto :eof
)
goto :eof

rem ---------------------------------------------------------------------------
:do_help
echo   solengine ^<command^> [args]     - or no command for the menu
echo.
echo   run             sandbox, Debug. Trailing args go to the exe.
echo   run-gpu         the same with ENGINE_GPU_DEBUG=1 - D3D12 debug layer
echo                   and the Vulkan validation layer.
echo   game            game.exe, Release.
echo.
echo   gates           sandbox --gates.
echo   gates-gpu       --gates with both debug layers armed. This is the one
echo                   to run before shipping GPU work.
echo   gates-release   --gates on the Release game build.
echo   gates-cpu       --gates-cpu, the device-free subset CI runs on Linux.
echo   invariants      tools\check-invariants.ps1 - no compiler, no GPU.
echo   invariants-51   the same under Windows PowerShell 5.1, as CI does.
echo   check           gates-gpu then invariants, reported separately.
echo.
echo   configure       cmake --preset vs2026
echo   build           cmake --build --preset debug
echo   build-release   cmake --build --preset release-game
echo   rebuild         wipe build\, configure, build Debug. Asks first.
echo   prereqs         tools\check-prereqs.ps1
echo.
echo   env             SDKs, tool versions, binary ages, staleness.
echo   logs            tail the newest engine log.
echo   clean-shaders   empty the shader disk cache.
echo.
echo   Examples
echo     solengine gates-gpu
echo     solengine run --set r.aa=taa --set r.quality=high
echo     solengine check
set "CODE=0"
goto finish

rem ---------------------------------------------------------------------------
:require_exe
if exist %1 exit /b 0
echo Missing %~1
echo.
if /i "%~2"=="Debug" (
    echo Build it:  solengine build
) else (
    echo Build it:  solengine build-release
)
set "CODE=1"
exit /b 1

:warn_if_stale
rem A stale binary has produced a confidently wrong answer here before - the
rem gates pass, against code that is no longer on disk. Cheap to check, and the
rem one thing a launcher can say that the engine cannot say about itself.
if not exist "%DEBUG_EXE%" goto :eof
%PS% -Command "$exe = Get-Item -LiteralPath '%DEBUG_EXE%'; $newest = Get-ChildItem packages -Recurse -File -Include *.cpp,*.hpp,*.h,*.hlsl,*.hlsli,CMakeLists.txt -EA 0 | Where-Object { $_.FullName -notmatch 'third_party' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if ($newest -and $newest.LastWriteTime -gt $exe.LastWriteTime) { Write-Host ''; Write-Host ('  STALE: ' + $newest.Name + ' is newer than sandbox.exe. Run: solengine build') -ForegroundColor Yellow; Write-Host '' }"
goto :eof

:run_gates
rem Teed through PowerShell so the run stays live on screen *and* every line is
rem counted. The gate commands take few or no extra arguments, which is why
rem they can afford PowerShell's argument parsing - `run` and `game` invoke the
rem exe directly instead, so anything can be passed to those.
%PS% -Command "& '%~1' %2 %3 %4 %5 %6 %7 %8 %9 2>&1 | Tee-Object -FilePath '%RUN_LOG%'; exit $LASTEXITCODE"
set "CODE=%ERRORLEVEL%"
call :gate_summary "%RUN_LOG%"
goto :eof

:gate_summary
rem Counted from the teed run log, not from the engine's own log file:
rem install_file_logger runs partway through startup, so the engine log is
rem missing every gate before it and the count reads low.
if not exist %1 goto :eof
echo.
%PS% -Command "$t = Get-Content -LiteralPath %1 -Raw; $p = ([regex]::Matches($t, '\(pass\)')).Count; $f = ([regex]::Matches($t, '\(FAIL\)')).Count; $s = ([regex]::Matches($t, '\(skip\)')).Count; $c = if ($f -gt 0) { 'Red' } else { 'Green' }; Write-Host ('  ' + $p + ' pass, ' + $f + ' FAIL, ' + $s + ' skip') -ForegroundColor $c"
goto :eof

:ensure_vulkan_sdk
rem VULKAN_SDK is set machine-wide by the SDK installer, but a shell opened
rem before that install never sees it - which looks exactly like "no SPIR-V
rem compiler" and sends you reading the wrong code. Read it back from the
rem machine environment when the process does not have it.
if defined VULKAN_SDK goto :eof
for /f "tokens=2,*" %%a in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v VULKAN_SDK 2^>nul ^| findstr /i /c:"VULKAN_SDK"') do set "VULKAN_SDK=%%b"
goto :eof

rem ---------------------------------------------------------------------------
:finish
if not defined CODE set "CODE=0"
echo.
if not "%CODE%"=="0" echo Exit code %CODE%
if defined INTERACTIVE (
    echo.
    pause
    set "CMD="
    set "CODE="
    goto menu
)
exit /b %CODE%
