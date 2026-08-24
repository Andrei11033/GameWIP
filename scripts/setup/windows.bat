@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0Windows.ps1" %*

exit /b %errorlevel%
