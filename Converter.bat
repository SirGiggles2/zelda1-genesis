@echo off
rem Zelda Genesis converter: drop your Zelda NES ROM and the Zelda Redux
rem v3.3.3 patch (or its release .zip) onto this file, or double-click it.
rem Front end for tools\converter\build.py (which compiles via Build.bat's
rem tools\build\build_rom.py); it is not a separate build target.
setlocal
set "HERE=%~dp0"
where pythonw >NUL 2>NUL
if errorlevel 1 (
  echo Python 3.11 or newer is required: https://www.python.org/downloads/  ^(tick "Add python.exe to PATH"^)
  pause
  exit /b 1
)
start "" pythonw "%HERE%tools\converter\gui.py" %*
