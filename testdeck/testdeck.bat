@echo off
rem Start DC Test Deck on Windows. Double-click this file.
rem
rem Everything is in launch.py; this only has to find a Python to run it with,
rem keep the console window open long enough to read any error, and run from
rem the checkout rather than wherever Explorer started us.
rem
rem Written with GOTO rather than IF blocks on purpose: %ERRORLEVEL% inside a
rem parenthesised block expands when the block is PARSED, not when it runs, so
rem the obvious nesting would test a stale value.
setlocal
cd /d "%~dp0"

set "PYEXE="

where py >nul 2>&1
if %ERRORLEVEL%==0 goto :have_py

where python >nul 2>&1
if %ERRORLEVEL%==0 goto :have_python

echo.
echo DC Test Deck needs Python 3.9 or newer, and none was found.
echo.
echo   1. Install it from https://www.python.org/downloads/
echo   2. Tick "Add python.exe to PATH" in the installer
echo   3. Run this file again
echo.
pause
exit /b 1

:have_py
set "PYEXE=py -3"
goto :run

:have_python
set "PYEXE=python"
goto :run

:run
%PYEXE% launch.py %*
set "RC=%ERRORLEVEL%"

rem A double-clicked window vanishes the instant the process ends, taking the
rem error with it. Only pause when there is something to read.
if not "%RC%"=="0" goto :held
exit /b 0

:held
echo.
echo Test Deck exited with code %RC%.
pause
exit /b %RC%
