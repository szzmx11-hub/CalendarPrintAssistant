@echo off
setlocal
cd /d "%~dp0"
set "SCM_PS=%~dp0installer\Install-GUI.ps1"

if not exist "%SCM_PS%" set "SCM_PS=%~dp0files\Install-GUI.ps1"

if not exist "%SCM_PS%" (
  echo SCM installer is incomplete.
  echo Missing Install-GUI.ps1 under installer or files.
  echo Please extract the complete ZIP before running this file.
  pause
  exit /b 2
)

if /I "%SCM_INSTALLER_SELFTEST%"=="1" (
  "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -STA -File "%SCM_PS%" -SelfTest
  exit /b %errorlevel%
)

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -STA -File "%SCM_PS%"
set "SCM_RC=%ERRORLEVEL%"
if not "%SCM_RC%"=="0" (
  echo.
  echo SCM V9.1.1 installer failed with exit code %SCM_RC%.
  echo Please take a screenshot of this window.
  pause
)
exit /b %SCM_RC%
