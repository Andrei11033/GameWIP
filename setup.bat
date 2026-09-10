@echo off
setlocal

rem Keep help aliases in the batch shim; setup behavior and exit codes belong to
rem scripts/setup/windows.bat and its PowerShell implementation.
if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
if "%~1"=="-?" goto help

call "%~dp0scripts\setup\windows.bat" %*
exit /b %errorlevel%

rem Use the normal setup path for help so the documented command surface remains
rem identical whether setup is launched directly or through this wrapper.
:help
call "%~dp0scripts\setup\windows.bat" help
exit /b %errorlevel%
