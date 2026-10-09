@echo off
setlocal EnableExtensions

rem ---------------------------------------------------------------------------
rem Sole build target: builds\Zelda.md.
rem
rem Wraps tools\build\build_rom.py. Uses SGDK startup, Title A4 RAM ABI,
rem and engine runtime exports linked into one ROM. Does not link the
rem engine fake RAM module and does not define ROOMROM_BUILD.
rem ---------------------------------------------------------------------------
for %%I in ("%~dp0.") do set "ROOT=%%~fI"

python "%ROOT%\tools\build\build_rom.py"
exit /b %ERRORLEVEL%
