@echo off
REM Create new Mira project - config, main.mira, build.bat (copied, uses miraPath from config)
REM Usage: new.bat [project_name_or_path]
REM   project_name = subdir under compiler; path = absolute path (e.g. E:\miratest)
setlocal enabledelayedexpansion

set "ARG=%~1"
if not defined ARG set "ARG=project"

set "ROOT=%~dp0"
if "%ARG:~1,1%"==":" (
  set "PROJECT=%~f1\"
) else (
  set "PROJECT=%ROOT%%ARG%\"
)

if exist "%PROJECT%" (
  echo Project already exists: %PROJECT%
  exit /b 1
)

echo Creating project: %PROJECT%
mkdir "%PROJECT%" 2>nul
mkdir "%PROJECT%config" 2>nul

REM Normalize ROOT (no trailing \) for JSON
set "ROOT_NORM=%ROOT%"
if "!ROOT_NORM:~-1!"=="\" set "ROOT_NORM=!ROOT_NORM:~0,-1!"

set "CFG_PATH=%PROJECT%config\project.json"
powershell -NoProfile -Command "$p=$env:ROOT_NORM; $path=$env:CFG_PATH; $j=@{platform='win64';defaultFile='main.mira';miraPath=$p}|ConvertTo-Json; $j|Set-Content -Path $path -Encoding UTF8"

(
echo main: {
echo     "Hello from %ARG%!" print
echo }
) > "%PROJECT%main.mira"

if not exist "%ROOT%build.bat" (
  echo Error: build.bat not found in %ROOT%
  exit /b 1
)
copy /Y "%ROOT%build.bat" "%PROJECT%build.bat"
if %ERRORLEVEL% neq 0 (
  echo Error: failed to copy build.bat
  exit /b 1
)

echo.
echo Done. Run: cd /d "%PROJECT%" ; build.bat

endlocal
exit /b 0
