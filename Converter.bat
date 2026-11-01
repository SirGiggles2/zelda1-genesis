@echo off
rem Open the local HTML builder. Your ROM stays on this computer.
setlocal
set "HERE=%~dp0"
py -3 -c "import sys;sys.exit(sys.version_info < (3,11))" >NUL 2>NUL
if not errorlevel 1 (
  py -3 "%HERE%tools\converter\launch.py" %*
  goto :finished
)
python -c "import sys;sys.exit(sys.version_info < (3,11))" >NUL 2>NUL
if not errorlevel 1 (
  python "%HERE%tools\converter\launch.py" %*
  goto :finished
)
echo Python 3.11 or newer is required for this source package.
echo Install it from https://www.python.org/downloads/ and enable Add Python to PATH.
echo The packaged Zelda Genesis Builder app includes Python.
pause
exit /b 1
:finished
if errorlevel 1 pause
