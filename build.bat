@echo off
REM Build Mira compiler and runtime (need: gcc, nasm on PATH)
REM Usage: build.bat [file.mira]
REM Output: out\ (.asm .obj build.log), exe in root
setlocal enabledelayedexpansion

cd /d %~dp0
set "PROJECT_DIR=%CD%"
set "MIRA_ROOT=%~dp0"

REM Read config: defaultFile, miraPath
if exist "config\project.json" (
  for /f "usebackq delims=" %%a in (`powershell -NoProfile -Command "(Get-Content 'config\project.json' -Raw | ConvertFrom-Json).defaultFile"`) do set "DEFAULT=%%a"
  for /f "usebackq delims=" %%a in (`powershell -NoProfile -Command "$j=Get-Content 'config\project.json' -Raw|ConvertFrom-Json;if($j.miraPath){$j.miraPath}"`) do set "MIRA_ROOT=%%a"
)

set "INPUT=%~1"
if not defined INPUT (
  if defined DEFAULT (set "INPUT=!DEFAULT!") else (set "INPUT=example.mira")
)

for %%F in ("!INPUT!") do set "BASENAME=%%~nF"
set "OUTDIR=out\"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

set "ASM=%OUTDIR%%BASENAME%.asm"
set "OBJ=%OUTDIR%%BASENAME%.obj"
set "EXE=%BASENAME%.exe"
set "LOG=%OUTDIR%build.log"

if exist "%LOG%" del "%LOG%"

REM Build compiler only when MIRA_ROOT = current dir (no miraPath in config)
if "!MIRA_ROOT:~-1!"=="\" set "MIRA_ROOT=!MIRA_ROOT:~0,-1!"
if "!PROJECT_DIR!\="=="!MIRA_ROOT!\=" (
  echo [1/3] Building compiler...
  echo [1/3] Building compiler...>>"%LOG%"
  gcc -Wall -Wextra -Wno-unused-parameter -I. -o mira.exe main.c lexer.c parser.c codegen.c >>"%LOG%" 2>&1
  if !ERRORLEVEL! neq 0 exit /b 1
  echo [2/3] Compiling !INPUT! to assembly...
  echo [2/3] Compiling !INPUT! to assembly...>>"%LOG%"
  mira.exe "!INPUT!" "!ASM!" >>"%LOG%" 2>&1
) else (
  echo [1/2] Compiling !INPUT! to assembly...
  echo [1/2] Compiling !INPUT! to assembly...>>"%LOG%"
  "!MIRA_ROOT!\mira.exe" "!INPUT!" "!ASM!" >>"%LOG%" 2>&1
)
if %ERRORLEVEL% neq 0 exit /b 1

if "!PROJECT_DIR!\="=="!MIRA_ROOT!\=" (
  echo [3/3] Assembling and linking...
  echo [3/3] Assembling and linking...>>"%LOG%"
) else (
  echo [2/2] Assembling and linking...
  echo [2/2] Assembling and linking...>>"%LOG%"
)
nasm -f win64 -o "!OBJ!" "!ASM!" >>"%LOG%" 2>&1
if %ERRORLEVEL% neq 0 exit /b 1
taskkill /IM "%BASENAME%.exe" /F >nul 2>&1
gcc -Wall -Wextra -o "!EXE!" "!OBJ!" "!MIRA_ROOT!\runtime.c" >>"%LOG%" 2>&1
if %ERRORLEVEL% neq 0 exit /b 1

echo Done. Run: !EXE!
echo Done. Run: !EXE!>>"%LOG%"
echo Exit code is the return value of main.
echo Exit code is the return value of main.>>"%LOG%"

endlocal
