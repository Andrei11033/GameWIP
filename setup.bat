@echo off
setlocal

if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
if "%~1"=="-?" goto help

call "%~dp0scripts\setup\windows.bat" %*
exit /b %errorlevel%

:help
call "%~dp0scripts\setup\windows.bat" help
exit /b %errorlevel%
