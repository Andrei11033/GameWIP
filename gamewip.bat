@echo off
setlocal

call powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\GameWIP.ps1" %*
exit /b %errorlevel%
