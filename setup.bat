@echo off
setlocal

call "%~dp0scripts\setup\windows.bat" %*
exit /b %errorlevel%
