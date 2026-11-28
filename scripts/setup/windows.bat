@echo off
setlocal

rem Run setup, then finish a confirmed build cleanup after PowerShell exits.
set "launch_token=%RANDOM%-%RANDOM%-%RANDOM%"
set "GAMEWIP_SETUP_LAUNCH_TOKEN=%launch_token%"

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0..\platform\windows\PowerShellBootstrap.ps1" ^
    "%~dp0Setup.ps1" %*
set "setup_exit_code=%errorlevel%"

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0platform\windows\FinalizeUninstall.ps1" ^
    -RepositoryRoot "%~dp0..\.." ^
    -LaunchToken "%launch_token%" ^
    -SetupExitCode %setup_exit_code%
set "finalize_exit_code=%errorlevel%"

if "%setup_exit_code%"=="0" if not "%finalize_exit_code%"=="0" set "setup_exit_code=%finalize_exit_code%"

exit /b %setup_exit_code%
