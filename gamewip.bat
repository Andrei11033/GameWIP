@echo off
setlocal

rem Keep help aliases in the batch shim so PowerShell receives the same command
rem contract as every other invocation.
if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
if "%~1"=="-?" goto help

call powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\GameWIP.ps1" %*
exit /b %errorlevel%

rem Route help through the same entry point to keep output and exit codes aligned.
:help
call powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\GameWIP.ps1" help
exit /b %errorlevel%
