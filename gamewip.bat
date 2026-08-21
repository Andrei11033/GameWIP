@echo off
setlocal

if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
if "%~1"=="-?" goto help

call powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\GameWIP.ps1" %*
exit /b %errorlevel%

:help
call powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\GameWIP.ps1" help
exit /b %errorlevel%
