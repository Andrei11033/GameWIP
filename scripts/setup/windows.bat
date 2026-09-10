@echo off
setlocal

rem Keep the batch surface thin; Windows.ps1 owns setup behavior and exit codes.
powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0Windows.ps1" %*

exit /b %errorlevel%
