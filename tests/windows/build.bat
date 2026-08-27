@echo off
rem Assembles one case's MASM with ml64, links it against the runtime, and runs
rem it. Driven by tests\remote-windows.sh, one case per invocation:
rem
rem   build.bat <name>        assembles work\<name>.asm and runs work\<name>.exe
rem
rem The two commands are shc's own, from src\Driver.cpp - if they drift apart,
rem this suite stops testing what shc does on this target:
rem
rem   ml64 /nologo /c /Fo<obj> <asm>
rem   link /nologo /subsystem:console /out:<exe> <obj> <runtime>
rem
rem Everything printed after ---RUN--- is taken as the program's output and
rem compared against the recorded file, so the marker goes after the last
rem toolchain message and before the program starts. Assembler and linker noise
rem above it is ignored; a failure there prints and never reaches the marker,
rem which is what makes an empty comparison a failure rather than a pass.
setlocal

if "%1"=="" (
    echo build.bat needs a case name
    exit /b 1
)

if not "%VSCMD_ARG_TGT_ARCH%"=="x64" call :findvcvars
if errorlevel 1 exit /b 1

cd /d %~dp0work
if not exist %1.asm (
    echo no %1.asm was sent
    exit /b 1
)

ml64 /nologo /c /Fo%1.obj %1.asm
if errorlevel 1 exit /b 1

link /nologo /subsystem:console /out:%1.exe %1.obj ..\shmrt.lib
if errorlevel 1 exit /b 1

echo ---RUN---
%1.exe
exit /b 0

:findvcvars
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
    echo no vswhere - Visual Studio 2022 does not look installed
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version [17.0^,18.0^) -property installationPath`) do set VSPATH=%%i
if "%VSPATH%"=="" (
    echo no Visual Studio 2022 found
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b 0
