@echo off
setlocal

rem Keep the batch surface thin; Setup.ps1 owns setup behavior and exit codes.
powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0Setup.ps1" %*

exit /b %errorlevel%
