@echo off
setlocal EnableExtensions

rem ---------------------------------------------------------------------------
rem Sole build target: builds\Debug.md.
rem
rem Wraps tools\debug\build_debug.py. Uses SGDK startup, Title A4 RAM ABI,
rem and RoomRom runtime exports linked into one ROM. Does not link the
rem RoomRom fake RAM module and does not define ROOMROM_BUILD.
rem ---------------------------------------------------------------------------
for %%I in ("%~dp0.") do set "ROOT=%%~fI"

python "%ROOT%\tools\debug\build_debug.py"
exit /b %ERRORLEVEL%
