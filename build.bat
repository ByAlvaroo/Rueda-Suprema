@echo off
rem Rueda Suprema - compilar con Visual Studio (Build Tools 2019/2022 con "Desarrollo para el escritorio con C++")
rem HECHO POR @BYALVARO__
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if not errorlevel 1 goto :compile
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" goto :findvs
echo No encuentro Visual Studio. Instala "Build Tools for Visual Studio" con "Desarrollo para el escritorio con C++".
if not defined CI pause
exit /b 1
:findvs
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul

:compile
if not exist build mkdir build
rc /nologo /i res /fo build\app.res res\app.rc || goto :error
set FLAGS=/nologo /O1 /MT /EHsc /std:c++17 /W3 /DUNICODE /D_UNICODE /utf-8
cl %FLAGS% /c src\main.cpp src\config.cpp src\audio.cpp src\radial.cpp src\settings.cpp /Fobuild\ || goto :error
cl /nologo /O1 /MT /EHsc /std:c++20 /W3 /DUNICODE /D_UNICODE /utf-8 /permissive- /c src\media.cpp /Fobuild\ || goto :error
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /MANIFEST:NO /OUT:RuedaSuprema.exe build\*.obj build\app.res || goto :error
echo.
echo  Listo: RuedaSuprema.exe
exit /b 0
:error
echo.
echo  Error al compilar.
if not defined CI pause
exit /b 1
