@echo off
rem Double-click to set up Python dependencies if needed, then start the app.
setlocal EnableExtensions
title PSVitaAlive Catalog Browser - setup
cd /d "%~dp0"

set "APP=local_psvitaalive_client.py"
set "VENV=.venv"
set "PY="

call :try_python py -3
if not defined PY call :try_python python
if not defined PY call :try_python python3
if not defined PY goto :no_python

echo Using Python: %PY%

if exist "%VENV%\Scripts\python.exe" goto :have_venv
echo Creating a private Python environment in %VENV% ...
%PY% -m venv "%VENV%"
if errorlevel 1 goto :venv_failed
:have_venv

if not exist "%VENV%\installed_requirements.txt" goto :need_install
fc /b requirements.txt "%VENV%\installed_requirements.txt" >nul 2>nul
if errorlevel 1 goto :need_install
"%VENV%\Scripts\python.exe" -c "import PySide6, requests" >nul 2>nul
if errorlevel 1 goto :need_install
echo Dependencies are up to date.
goto :deps_done

:need_install
echo Installing dependencies. This can take a minute the first time ...
"%VENV%\Scripts\python.exe" -m pip install --quiet --disable-pip-version-check -r requirements.txt
if errorlevel 1 goto :pip_failed
copy /y requirements.txt "%VENV%\installed_requirements.txt" >nul

:deps_done
echo Starting PSVitaAlive Catalog Browser ...
"%VENV%\Scripts\python.exe" "%APP%"
if errorlevel 1 (
    echo.
    echo The app stopped with an error. See the message above.
    pause
)
exit /b 0

:try_python
rem Usage: call :try_python COMMAND [ARGS]. Sets PY if COMMAND is Python 3.10 or newer.
where %1 >nul 2>nul
if errorlevel 1 exit /b 0
%* -c "import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>nul
if errorlevel 1 exit /b 0
set "PY=%*"
exit /b 0

:no_python
echo.
echo ERROR: Python 3.10 or newer was not found on this computer.
where winget >nul 2>nul
if errorlevel 1 goto :no_python_manual
echo.
set "ANS="
set /p "ANS=Install Python 3.12 now with winget? [Y/N]: "
if /i not "%ANS%"=="Y" goto :no_python_manual
echo.
echo Installing Python 3.12. This can take a few minutes ...
winget install -e --id Python.Python.3.12 --accept-package-agreements --accept-source-agreements
if errorlevel 1 goto :no_python_manual
echo.
echo Python 3.12 is installed. Close this window, then double-click
echo install_and_run.bat again so Windows finds the new Python.
echo.
pause
exit /b 0

:no_python_manual
echo.
echo  1. Download Python from https://www.python.org/downloads/
echo  2. Run the installer and tick "Add python.exe to PATH" on the first screen.
echo  3. Double-click this file again.
echo.
pause
exit /b 1

:venv_failed
echo.
echo ERROR: Could not create the Python environment in %VENV%.
echo Try deleting the %VENV% folder, then run this file again.
echo.
pause
exit /b 1

:pip_failed
echo.
echo ERROR: Installing the dependencies failed.
echo Check your internet connection and run this file again.
echo If it keeps failing, delete the %VENV% folder and try again.
echo.
pause
exit /b 1
