@echo off
setlocal EnableDelayedExpansion

if /I "%~1"=="links" (
    set "GAMEWIP_LINK_PYTHON=C:\MSYS2\ucrt64\bin\python.exe"
    if not exist "!GAMEWIP_LINK_PYTHON!" (
        echo GameWIP UCRT64 Python is unavailable at "!GAMEWIP_LINK_PYTHON!".
        echo Run .\setup.bat repair to install the project toolchain.
        exit /b 1
    )

    "!GAMEWIP_LINK_PYTHON!" "%~dp0.github\scripts\check-markdown-links.py" --root "%~dp0."
    exit /b !errorlevel!
)

call powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\GameWIP.ps1" %*
exit /b %errorlevel%
